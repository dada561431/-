#include "usb_camera_host.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "USBH.h"
#include "USBH_Util.h"
#include "USBH_VIDEO.h"
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"
#include "cy_utils.h"
#include "FreeRTOS.h"
#include "task.h"

#define USB_CAMERA_NUM_FRAME_BUFFERS       (2U)
#define USB_CAMERA_MAX_VIDEO_INTERFACES    (4U)
#define USB_CAMERA_TASK_STACK_SIZE_BYTES   (8192U)
#define USB_CAMERA_TASK_PRIORITY           (configMAX_PRIORITIES - 2)
#define USB_CAMERA_USBH_ISR_PRIORITY       (configMAX_PRIORITIES - 1)
#define USB_CAMERA_FRAME_INTERVAL_0P3MP    (2000000UL)
#define USB_CAMERA_FRAME_INTERVAL_LOGI     (1000000UL)
#define USB_CAMERA_FORMAT                  USBH_VIDEO_VS_FORMAT_UNCOMPRESSED
#define USB_CAMERA_REOPEN_RETRIES          (10U)
#define USB_CAMERA_REOPEN_DELAY_MS         (300U)
#define USB_CAMERA_MAX_STREAM_ERRORS       (10U)
#define USB_CAMERA_STREAM_KEEPALIVE_MS     (45000U)

#define USB_CAMERA_LOGITECH_VID            (0x046DU)
#define USB_CAMERA_LOGITECH_C920_PID       (0x08E5U)
#define USB_CAMERA_LOGITECH_C920E_PID      (0x08B6U)
#define USB_CAMERA_HBVCAM_0P3_VID          (0x058FU)
#define USB_CAMERA_HBVCAM_0P3_PID          (0x5608U)

static cy_thread_t usbh_main_task_handle;
static cy_thread_t usbh_isr_task_handle;
static cy_thread_t usb_camera_task_handle;
static cy_queue_t video_mail_box;
static cy_queue_t device_state_mail_box;
static cy_mutex_t latest_frame_mutex;
static cy_mutex_t snapshot_request_mutex;
static cy_semaphore_t snapshot_ready_semaphore;
static USBH_NOTIFICATION_HOOK camera_notification_hook;

CY_SECTION(".cy_gpu_buf")
static uint8_t frame_buffers[USB_CAMERA_NUM_FRAME_BUFFERS][USB_CAMERA_FRAME_BYTES];
static uint8_t latest_frame_index = 0U;
static uint8_t capture_frame_index = 0U;
static bool latest_frame_ready = false;
static volatile uint8_t device_connected = 0U;
static bool usb_camera_host_initialized = false;
static bool usb_camera_connected = false;
static bool usb_camera_supported = false;
static volatile bool usb_camera_stream_active = false;
static uint32_t usb_camera_stream_error_count = 0U;
static volatile bool snapshot_request_pending = false;
static volatile bool snapshot_capture_success = false;
static volatile uint8_t usb_camera_device_index = 0xFFU;
static USBH_VIDEO_DEVICE_HANDLE usb_camera_device_handle;
static USBH_VIDEO_INTERFACE_INFO usb_camera_interface_info;
static bool usb_camera_device_opened = false;
static volatile TickType_t usb_camera_stream_keepalive_deadline = 0U;

#define USB_CAMERA_SIGNAL_DEVICE_REMOVED      (0xFFU)
#define USB_CAMERA_SIGNAL_TRANSFER_ERROR      (0xFEU)
#define USB_CAMERA_SIGNAL_SNAPSHOT_READY      (0xA5U)
#define USB_CAMERA_SIGNAL_SNAPSHOT_ABORT      (0xA4U)

static void usb_camera_task(void *arg);
static void usb_camera_add_remove_device_cb(void *context, U8 dev_index, USBH_DEVICE_EVENT event);
static void usb_camera_on_data_cb(USBH_VIDEO_DEVICE_HANDLE h_device,
                                  USBH_VIDEO_STREAM_HANDLE h_stream,
                                  USBH_STATUS status,
                                  const U8 *p_data,
                                  unsigned num_bytes,
                                  U32 flags,
                                  void *p_user_data_context);
static void usb_camera_handle_device(U8 dev_index);
static void usb_camera_clear_latest_frame(void);
static bool usb_camera_select_stream(USBH_VIDEO_DEVICE_HANDLE h_device,
                                     uint32_t frame_interval,
                                     USBH_VIDEO_STREAM_CONFIG *stream_info,
                                     uint32_t *selected_interval);
static uint32_t usb_camera_frame_interval_for_device(uint16_t vendor_id, uint16_t product_id);
static bool usb_camera_open_device(U8 dev_index);
static void usb_camera_close_device(void);
static void usb_camera_build_bmp_from_latest(uint8_t *out_bmp,
                                             size_t out_bmp_size,
                                             size_t *out_bmp_len,
                                             uint16_t *out_width,
                                             uint16_t *out_height);
static void yuyv_to_rgb_pixel(const uint8_t *frame, uint16_t x, uint16_t y,
                              uint8_t *r, uint8_t *g, uint8_t *b);
static uint8_t clamp_u8(int32_t value);

cy_rslt_t usb_camera_host_init(void)
{
    cy_rslt_t result;

    if (usb_camera_host_initialized)
    {
        return CY_RSLT_SUCCESS;
    }

    memset(frame_buffers, 0, sizeof(frame_buffers));
    latest_frame_ready = false;
    usb_camera_connected = false;
    usb_camera_supported = false;
    latest_frame_index = 0U;
    capture_frame_index = 0U;
    memset(&usb_camera_device_handle, 0, sizeof(usb_camera_device_handle));
    memset(&usb_camera_interface_info, 0, sizeof(usb_camera_interface_info));
    usb_camera_device_opened = false;

    result = cy_rtos_mutex_init(&latest_frame_mutex, false);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("[CAMERA] Failed to init latest frame mutex\n");
        return result;
    }

    result = cy_rtos_queue_init(&video_mail_box, sizeof(U8), USB_CAMERA_MAX_VIDEO_INTERFACES);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("[CAMERA] Failed to init video mailbox\n");
        return result;
    }

    result = cy_rtos_queue_init(&device_state_mail_box, sizeof(U8), 1U);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("[CAMERA] Failed to init camera device state mailbox\n");
        return result;
    }

    result = cy_rtos_mutex_init(&snapshot_request_mutex, false);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("[CAMERA] Failed to init snapshot request mutex\n");
        return result;
    }

    result = cy_rtos_semaphore_init(&snapshot_ready_semaphore, 1U, 0U);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("[CAMERA] Failed to init snapshot ready semaphore\n");
        return result;
    }

    result = cy_rtos_thread_create(&usb_camera_task_handle,
                                   usb_camera_task,
                                   "CM33 USB Camera",
                                   NULL,
                                   USB_CAMERA_TASK_STACK_SIZE_BYTES,
                                   USB_CAMERA_TASK_PRIORITY,
                                   NULL);
    if (result == CY_RSLT_SUCCESS)
    {
        usb_camera_host_initialized = true;
        printf("[CAMERA] USB host camera task started\n");
    }
    else
    {
        printf("[CAMERA] Failed to start USB camera task\n");
    }

    return result;
}

bool usb_camera_host_request_snapshot(uint32_t timeout_ms)
{
    cy_rslt_t result;
    U8 dev_index;

    if ((!usb_camera_host_initialized) || (device_connected == 0U) || (usb_camera_device_index == 0xFFU))
    {
        return false;
    }

    if (cy_rtos_mutex_get(&snapshot_request_mutex, CY_RTOS_NEVER_TIMEOUT) != CY_RSLT_SUCCESS)
    {
        return false;
    }

    while (cy_rtos_semaphore_get(&snapshot_ready_semaphore, 0U) == CY_RSLT_SUCCESS)
    {
    }

    if (usb_camera_stream_active)
    {
        usb_camera_stream_keepalive_deadline =
            xTaskGetTickCount() + pdMS_TO_TICKS(USB_CAMERA_STREAM_KEEPALIVE_MS);

        if (latest_frame_ready)
        {
            cy_rtos_mutex_set(&snapshot_request_mutex);
            return true;
        }

        snapshot_capture_success = false;
        snapshot_request_pending = true;
        result = cy_rtos_semaphore_get(&snapshot_ready_semaphore, timeout_ms);
        snapshot_request_pending = false;
        cy_rtos_mutex_set(&snapshot_request_mutex);
        return ((result == CY_RSLT_SUCCESS) && snapshot_capture_success);
    }

    cy_rtos_queue_reset(&device_state_mail_box);
    snapshot_capture_success = false;
    snapshot_request_pending = true;
    dev_index = usb_camera_device_index;

    result = cy_rtos_queue_put(&video_mail_box, &dev_index, 0U);
    if (result != CY_RSLT_SUCCESS)
    {
        snapshot_request_pending = false;
        cy_rtos_mutex_set(&snapshot_request_mutex);
        return false;
    }

    result = cy_rtos_semaphore_get(&snapshot_ready_semaphore, timeout_ms);
    if (result != CY_RSLT_SUCCESS)
    {
        snapshot_request_pending = false;
        snapshot_capture_success = false;
    }
    snapshot_request_pending = false;
    cy_rtos_mutex_set(&snapshot_request_mutex);

    return ((result == CY_RSLT_SUCCESS) && snapshot_capture_success);
}

bool usb_camera_host_get_snapshot_bmp(uint8_t *out_bmp,
                                      size_t out_bmp_size,
                                      size_t *out_bmp_len,
                                      uint16_t *out_width,
                                      uint16_t *out_height)
{
    if ((!usb_camera_host_initialized) || (!latest_frame_ready) || (out_bmp == NULL))
    {
        return false;
    }

    if (cy_rtos_mutex_get(&latest_frame_mutex, 1000U) != CY_RSLT_SUCCESS)
    {
        return false;
    }

    usb_camera_build_bmp_from_latest(out_bmp, out_bmp_size, out_bmp_len, out_width, out_height);

    cy_rtos_mutex_set(&latest_frame_mutex);
    return true;
}

bool usb_camera_host_is_stream_active(void)
{
    return (usb_camera_stream_active && usb_camera_supported);
}

static void usb_camera_clear_latest_frame(void)
{
    if (cy_rtos_mutex_get(&latest_frame_mutex, 1000U) == CY_RSLT_SUCCESS)
    {
        latest_frame_ready = false;
        cy_rtos_mutex_set(&latest_frame_mutex);
    }
    else
    {
        latest_frame_ready = false;
    }
}

static bool usb_camera_open_device(U8 dev_index)
{
    USBH_STATUS status = USBH_STATUS_ERROR;
    uint32_t open_attempt;

    if (usb_camera_device_opened)
    {
        return true;
    }

    memset(&usb_camera_device_handle, 0, sizeof(usb_camera_device_handle));
    memset(&usb_camera_interface_info, 0, sizeof(usb_camera_interface_info));

    for (open_attempt = 0U; open_attempt < USB_CAMERA_REOPEN_RETRIES; ++open_attempt)
    {
        status = USBH_VIDEO_Open(dev_index, &usb_camera_device_handle);
        if (status == USBH_STATUS_SUCCESS)
        {
            status = USBH_VIDEO_GetInterfaceInfo(usb_camera_device_handle, &usb_camera_interface_info);
            if (status == USBH_STATUS_SUCCESS)
            {
                usb_camera_device_opened = true;
                return true;
            }

            printf("[CAMERA] USBH_VIDEO_GetInterfaceInfo retry %lu/%lu failed: %s\n",
                   (unsigned long)(open_attempt + 1U),
                   (unsigned long)USB_CAMERA_REOPEN_RETRIES,
                   USBH_GetStatusStr(status));
            USBH_VIDEO_Close(usb_camera_device_handle);
            memset(&usb_camera_device_handle, 0, sizeof(usb_camera_device_handle));
        }
        else
        {
            printf("[CAMERA] USBH_VIDEO_Open retry %lu/%lu failed: %s\n",
                   (unsigned long)(open_attempt + 1U),
                   (unsigned long)USB_CAMERA_REOPEN_RETRIES,
                   USBH_GetStatusStr(status));
        }

        if (device_connected == 0U)
        {
            break;
        }

        cy_rtos_delay_milliseconds(USB_CAMERA_REOPEN_DELAY_MS);
    }

    return false;
}

static void usb_camera_close_device(void)
{
    if (usb_camera_device_opened)
    {
        USBH_VIDEO_Close(usb_camera_device_handle);
    }

    memset(&usb_camera_device_handle, 0, sizeof(usb_camera_device_handle));
    memset(&usb_camera_interface_info, 0, sizeof(usb_camera_interface_info));
    usb_camera_device_opened = false;
}

static void usb_camera_task(void *arg)
{
    U8 dev_index;
    cy_rslt_t result;

    (void)arg;

    USBH_Init();

    result = cy_rtos_create_thread(&usbh_main_task_handle,
                                   (void *)USBH_Task,
                                   "USBH_Task",
                                   NULL,
                                   USB_CAMERA_TASK_STACK_SIZE_BYTES,
                                   USB_CAMERA_TASK_PRIORITY,
                                   NULL);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("[CAMERA] Failed to start USBH main task\n");
        return;
    }

    result = cy_rtos_create_thread(&usbh_isr_task_handle,
                                   (void *)USBH_ISRTask,
                                   "USBH_ISR",
                                   NULL,
                                   USB_CAMERA_TASK_STACK_SIZE_BYTES,
                                   USB_CAMERA_USBH_ISR_PRIORITY,
                                   NULL);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("[CAMERA] Failed to start USBH ISR task\n");
        return;
    }

    if (USBH_VIDEO_Init() != USBH_STATUS_SUCCESS)
    {
        printf("[CAMERA] USBH_VIDEO_Init failed\n");
        return;
    }

    if (USBH_VIDEO_AddNotification(&camera_notification_hook,
                                   usb_camera_add_remove_device_cb,
                                   NULL) != USBH_STATUS_SUCCESS)
    {
        printf("[CAMERA] Failed to add USB camera notification hook\n");
        return;
    }

    for (;;)
    {
        if (cy_rtos_queue_get(&video_mail_box, &dev_index, CY_RTOS_NEVER_TIMEOUT) == CY_RSLT_SUCCESS)
        {
            usb_camera_handle_device(dev_index);
        }
    }
}

static void usb_camera_add_remove_device_cb(void *context, U8 dev_index, USBH_DEVICE_EVENT event)
{
    U8 signal = USB_CAMERA_SIGNAL_DEVICE_REMOVED;

    (void)context;

    if (event == USBH_DEVICE_EVENT_ADD)
    {
        device_connected = 1U;
        usb_camera_connected = true;
        usb_camera_stream_error_count = 0U;
        usb_camera_device_index = dev_index;
        usb_camera_close_device();
        snapshot_request_pending = false;
        snapshot_capture_success = false;
        usb_camera_clear_latest_frame();
        printf("[CAMERA] USB camera device added index=%u\n", (unsigned)dev_index);
    }
    else if (event == USBH_DEVICE_EVENT_REMOVE)
    {
        device_connected = 0U;
        usb_camera_connected = false;
        usb_camera_supported = false;
        usb_camera_device_index = 0xFFU;
        usb_camera_close_device();
        snapshot_request_pending = false;
        snapshot_capture_success = false;
        (void)cy_rtos_queue_put(&device_state_mail_box, &signal, 0U);
        (void)cy_rtos_semaphore_set(&snapshot_ready_semaphore);
        printf("[CAMERA] USB camera device removed index=%u\n", (unsigned)dev_index);
    }
}

static void usb_camera_on_data_cb(USBH_VIDEO_DEVICE_HANDLE h_device,
                                  USBH_VIDEO_STREAM_HANDLE h_stream,
                                  USBH_STATUS status,
                                  const U8 *p_data,
                                  unsigned num_bytes,
                                  U32 flags,
                                  void *p_user_data_context)
{
    static size_t frame_bytes = 0U;
    static uint8_t drop_frame = 0U;
    USBH_STATUS restart_status = USBH_STATUS_SUCCESS;
    I8 is_stream_stopped = 0;

    (void)h_device;
    (void)p_user_data_context;

    if (status != USBH_STATUS_SUCCESS)
    {
        U8 signal = USB_CAMERA_SIGNAL_TRANSFER_ERROR;
        frame_bytes = 0U;
        drop_frame = 0U;

        if (snapshot_capture_success)
        {
            return;
        }

        if (snapshot_request_pending)
        {
            usb_camera_stream_error_count++;

            if ((status != USBH_STATUS_DEVICE_REMOVED) &&
                (usb_camera_stream_error_count < 3U))
            {
                restart_status = USBH_VIDEO_GetStreamState(h_stream, &is_stream_stopped);
                if ((restart_status == USBH_STATUS_SUCCESS) && (is_stream_stopped == 1))
                {
                    restart_status = USBH_VIDEO_RestartStream(h_stream);
                    if (restart_status == USBH_STATUS_SUCCESS)
                    {
                        printf("[CAMERA] Restarted USB stream while waiting for snapshot frame\n");
                        return;
                    }
                }
            }

            snapshot_request_pending = false;
            snapshot_capture_success = false;
            (void)cy_rtos_queue_put(&device_state_mail_box, &signal, 0U);
            return;
        }

        usb_camera_stream_error_count++;

        if ((status != USBH_STATUS_DEVICE_REMOVED) &&
            (usb_camera_stream_error_count < USB_CAMERA_MAX_STREAM_ERRORS))
        {
            restart_status = USBH_VIDEO_GetStreamState(h_stream, &is_stream_stopped);
            if (restart_status == USBH_STATUS_SUCCESS)
            {
                if (is_stream_stopped == 1)
                {
                    restart_status = USBH_VIDEO_RestartStream(h_stream);
                    if (restart_status == USBH_STATUS_SUCCESS)
                    {
                        printf("[CAMERA] Restarted USB stream after transfer error\n");
                        usb_camera_stream_error_count = 0U;
                    }
                    else
                    {
                        printf("[CAMERA] USBH_VIDEO_RestartStream failed: %s\n",
                               USBH_GetStatusStr(restart_status));
                    }
                }
                else
                {
                    printf("[CAMERA] USB stream reported transfer error while still running\n");
                    restart_status = USBH_STATUS_SUCCESS;
                }
            }
            else
            {
                printf("[CAMERA] USBH_VIDEO_GetStreamState failed: %s\n",
                       USBH_GetStatusStr(restart_status));
            }
        }

        if ((status == USBH_STATUS_DEVICE_REMOVED) ||
            (usb_camera_stream_error_count >= USB_CAMERA_MAX_STREAM_ERRORS) ||
            (restart_status != USBH_STATUS_SUCCESS))
        {
            (void)cy_rtos_queue_put(&device_state_mail_box, &signal, 0U);
        }
        return;
    }

    usb_camera_stream_error_count = 0U;

    if ((frame_bytes + num_bytes) > USB_CAMERA_FRAME_BYTES)
    {
        drop_frame = 1U;
        frame_bytes = 0U;
    }

    if (!drop_frame)
    {
        memcpy(&frame_buffers[capture_frame_index][frame_bytes], p_data, num_bytes);
        frame_bytes += num_bytes;
    }

    if ((flags & USBH_UVC_END_OF_FRAME) == USBH_UVC_END_OF_FRAME)
    {
        if ((!drop_frame) && (frame_bytes == USB_CAMERA_FRAME_BYTES))
        {
            if (cy_rtos_mutex_get(&latest_frame_mutex, 1000U) == CY_RSLT_SUCCESS)
            {
                latest_frame_index = capture_frame_index;
                latest_frame_ready = true;
                capture_frame_index = (uint8_t)((capture_frame_index + 1U) % USB_CAMERA_NUM_FRAME_BUFFERS);
                cy_rtos_mutex_set(&latest_frame_mutex);
            }

            if (snapshot_request_pending)
            {
                U8 signal = USB_CAMERA_SIGNAL_SNAPSHOT_READY;
                snapshot_capture_success = true;
                snapshot_request_pending = false;
                (void)cy_rtos_queue_put(&device_state_mail_box, &signal, 0U);
            }
        }

        frame_bytes = 0U;
        drop_frame = 0U;
    }

    (void)USBH_VIDEO_Ack(h_stream);
}

static void usb_camera_handle_device(U8 dev_index)
{
    USBH_VIDEO_STREAM_CONFIG stream_info;
    USBH_VIDEO_STREAM_HANDLE stream;
    USBH_STATUS status;
    uint32_t frame_interval;
    uint32_t selected_interval = 0U;
    U8 signal = 0U;

    memset(&stream, 0, sizeof(stream));
    memset(&stream_info, 0, sizeof(stream_info));

    if (!usb_camera_open_device(dev_index))
    {
        return;
    }

    frame_interval = usb_camera_frame_interval_for_device(usb_camera_interface_info.VendorId,
                                                          usb_camera_interface_info.ProductId);
    if (frame_interval == 0U)
    {
        printf("[CAMERA] Unsupported USB camera VID=0x%04X PID=0x%04X\n",
               usb_camera_interface_info.VendorId,
               usb_camera_interface_info.ProductId);
        usb_camera_close_device();
        return;
    }

    if (!usb_camera_select_stream(usb_camera_device_handle, frame_interval, &stream_info, &selected_interval))
    {
        printf("[CAMERA] Unable to find supported 320x240 YUYV stream\n");
        usb_camera_close_device();
        return;
    }

    status = USBH_VIDEO_OpenStream(usb_camera_device_handle, &stream_info, &stream);
    if (status != USBH_STATUS_SUCCESS)
    {
        printf("[CAMERA] USBH_VIDEO_OpenStream failed: %s\n", USBH_GetStatusStr(status));
        usb_camera_close_device();
        return;
    }

    usb_camera_supported = true;
    usb_camera_stream_active = true;
    usb_camera_stream_error_count = 0U;
    usb_camera_stream_keepalive_deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(USB_CAMERA_STREAM_KEEPALIVE_MS);
    printf("[CAMERA] USB stream started %ux%u @ %.2f fps\n",
           (unsigned)USB_CAMERA_FRAME_WIDTH,
           (unsigned)USB_CAMERA_FRAME_HEIGHT,
           (selected_interval != 0U) ? (10000000.0f / (float)selected_interval) : 0.0f);

    cy_rtos_queue_reset(&device_state_mail_box);
    while (device_connected != 0U)
    {
        if (cy_rtos_queue_get(&device_state_mail_box, &signal, 10U) == CY_RSLT_SUCCESS)
        {
            if (signal == USB_CAMERA_SIGNAL_SNAPSHOT_READY)
            {
                usb_camera_stream_keepalive_deadline =
                    xTaskGetTickCount() + pdMS_TO_TICKS(USB_CAMERA_STREAM_KEEPALIVE_MS);
                (void)cy_rtos_semaphore_set(&snapshot_ready_semaphore);
                signal = 0U;
                continue;
            }
            break;
        }

        if ((!snapshot_request_pending) &&
            ((int32_t)(xTaskGetTickCount() - usb_camera_stream_keepalive_deadline) >= 0))
        {
            break;
        }
    }

    (void)USBH_VIDEO_CloseStream(stream);
    usb_camera_supported = false;
    usb_camera_stream_active = false;
    printf("[CAMERA] USB stream stopped\n");
    usb_camera_close_device();

    if ((signal == USB_CAMERA_SIGNAL_TRANSFER_ERROR) ||
        (signal == USB_CAMERA_SIGNAL_DEVICE_REMOVED) ||
        (signal == USB_CAMERA_SIGNAL_SNAPSHOT_ABORT))
    {
        snapshot_capture_success = false;
        if (latest_frame_ready)
        {
            printf("[CAMERA] Preserving last successful snapshot for reuse after stream stop.\n");
        }
    }

    (void)cy_rtos_semaphore_set(&snapshot_ready_semaphore);
}

static bool usb_camera_select_stream(USBH_VIDEO_DEVICE_HANDLE h_device,
                                     uint32_t frame_interval,
                                     USBH_VIDEO_STREAM_CONFIG *stream_info,
                                     uint32_t *selected_interval)
{
    USBH_VIDEO_INPUT_HEADER_INFO input_header_info;
    USBH_VIDEO_FORMAT_INFO format_info;
    USBH_VIDEO_FRAME_INFO frame_info;
    USBH_STATUS status;
    unsigned format_index;
    unsigned frame_index;
    unsigned interval_index;
    unsigned frame_descriptor_count;
    bool fallback_found = false;
    unsigned fallback_format_index = 0U;
    unsigned fallback_frame_index = 0U;
    unsigned fallback_interval_index = 0U;
    uint32_t fallback_interval = 0U;

    status = USBH_VIDEO_GetInputHeader(h_device, &input_header_info);
    if (status != USBH_STATUS_SUCCESS)
    {
        return false;
    }

    for (format_index = 0U; format_index < input_header_info.bNumFormats; format_index++)
    {
        status = USBH_VIDEO_GetFormatInfo(h_device, format_index, &format_info);
        if (status != USBH_STATUS_SUCCESS)
        {
            continue;
        }

        if (format_info.FormatType != USB_CAMERA_FORMAT)
        {
            continue;
        }

        frame_descriptor_count = format_info.u.UncompressedFormat.bNumFrameDescriptors;
        for (frame_index = 0U; frame_index < frame_descriptor_count; frame_index++)
        {
            status = USBH_VIDEO_GetFrameInfo(h_device, format_index, frame_index, &frame_info);
            if (status != USBH_STATUS_SUCCESS)
            {
                continue;
            }

            if ((frame_info.wWidth != USB_CAMERA_FRAME_WIDTH) ||
                (frame_info.wHeight != USB_CAMERA_FRAME_HEIGHT))
            {
                continue;
            }

            for (interval_index = 0U; interval_index < frame_info.bFrameIntervalType; interval_index++)
            {
                if (frame_info.u.dwFrameInterval[interval_index] == frame_interval)
                {
                    memset(stream_info, 0, sizeof(*stream_info));
                    stream_info->Flags = 0U;
                    stream_info->FormatIdx = format_index;
                    stream_info->FrameIdx = frame_index;
                    stream_info->FrameIntervalIdx = interval_index + 1U;
                    stream_info->pfDataCallback = usb_camera_on_data_cb;
                    if (selected_interval != NULL)
                    {
                        *selected_interval = frame_info.u.dwFrameInterval[interval_index];
                    }
                    return true;
                }

                if ((!fallback_found) || (frame_info.u.dwFrameInterval[interval_index] > fallback_interval))
                {
                    fallback_found = true;
                    fallback_format_index = format_index;
                    fallback_frame_index = frame_index;
                    fallback_interval_index = interval_index + 1U;
                    fallback_interval = frame_info.u.dwFrameInterval[interval_index];
                }
            }
        }
    }

    if (!fallback_found)
    {
        return false;
    }

    memset(stream_info, 0, sizeof(*stream_info));
    stream_info->Flags = 0U;
    stream_info->FormatIdx = fallback_format_index;
    stream_info->FrameIdx = fallback_frame_index;
    stream_info->FrameIntervalIdx = fallback_interval_index;
    stream_info->pfDataCallback = usb_camera_on_data_cb;
    if (selected_interval != NULL)
    {
        *selected_interval = fallback_interval;
    }
    printf("[CAMERA] Preferred frame interval unavailable, falling back to slowest supported mode\n");
    return true;
}

static uint32_t usb_camera_frame_interval_for_device(uint16_t vendor_id, uint16_t product_id)
{
    if ((vendor_id == USB_CAMERA_LOGITECH_VID) &&
        ((product_id == USB_CAMERA_LOGITECH_C920_PID) ||
         (product_id == USB_CAMERA_LOGITECH_C920E_PID)))
    {
        return USB_CAMERA_FRAME_INTERVAL_LOGI;
    }

    if ((vendor_id == USB_CAMERA_HBVCAM_0P3_VID) &&
        (product_id == USB_CAMERA_HBVCAM_0P3_PID))
    {
        return USB_CAMERA_FRAME_INTERVAL_0P3MP;
    }

    return 0U;
}

static void usb_camera_build_bmp_from_latest(uint8_t *out_bmp,
                                             size_t out_bmp_size,
                                             size_t *out_bmp_len,
                                             uint16_t *out_width,
                                             uint16_t *out_height)
{
    const uint8_t *frame;
    const uint32_t row_stride = ((USB_CAMERA_SNAPSHOT_WIDTH * 3U) + 3U) & ~3U;
    const uint32_t pixel_bytes = row_stride * USB_CAMERA_SNAPSHOT_HEIGHT;
    const uint32_t file_size = 54U + pixel_bytes;
    uint32_t x;
    uint32_t y;

    if (out_bmp_size < file_size)
    {
        if (out_bmp_len != NULL)
        {
            *out_bmp_len = 0U;
        }
        return;
    }

    frame = frame_buffers[latest_frame_index];
    memset(out_bmp, 0, file_size);

    out_bmp[0] = 'B';
    out_bmp[1] = 'M';
    out_bmp[2] = (uint8_t)(file_size & 0xFFU);
    out_bmp[3] = (uint8_t)((file_size >> 8) & 0xFFU);
    out_bmp[4] = (uint8_t)((file_size >> 16) & 0xFFU);
    out_bmp[5] = (uint8_t)((file_size >> 24) & 0xFFU);
    out_bmp[10] = 54U;
    out_bmp[14] = 40U;
    out_bmp[18] = (uint8_t)(USB_CAMERA_SNAPSHOT_WIDTH & 0xFFU);
    out_bmp[19] = (uint8_t)((USB_CAMERA_SNAPSHOT_WIDTH >> 8) & 0xFFU);
    out_bmp[22] = (uint8_t)(USB_CAMERA_SNAPSHOT_HEIGHT & 0xFFU);
    out_bmp[23] = (uint8_t)((USB_CAMERA_SNAPSHOT_HEIGHT >> 8) & 0xFFU);
    out_bmp[26] = 1U;
    out_bmp[28] = 24U;
    out_bmp[34] = (uint8_t)(pixel_bytes & 0xFFU);
    out_bmp[35] = (uint8_t)((pixel_bytes >> 8) & 0xFFU);
    out_bmp[36] = (uint8_t)((pixel_bytes >> 16) & 0xFFU);
    out_bmp[37] = (uint8_t)((pixel_bytes >> 24) & 0xFFU);

    for (y = 0U; y < USB_CAMERA_SNAPSHOT_HEIGHT; y++)
    {
        uint8_t *row = &out_bmp[54U + ((USB_CAMERA_SNAPSHOT_HEIGHT - 1U - y) * row_stride)];
        for (x = 0U; x < USB_CAMERA_SNAPSHOT_WIDTH; x++)
        {
            const uint16_t src_x = (uint16_t)((x * USB_CAMERA_FRAME_WIDTH) / USB_CAMERA_SNAPSHOT_WIDTH);
            const uint16_t src_y = (uint16_t)((y * USB_CAMERA_FRAME_HEIGHT) / USB_CAMERA_SNAPSHOT_HEIGHT);
            uint8_t r;
            uint8_t g;
            uint8_t b;

            yuyv_to_rgb_pixel(frame, src_x, src_y, &r, &g, &b);
            row[(x * 3U) + 0U] = b;
            row[(x * 3U) + 1U] = g;
            row[(x * 3U) + 2U] = r;
        }
    }

    if (out_bmp_len != NULL)
    {
        *out_bmp_len = file_size;
    }
    if (out_width != NULL)
    {
        *out_width = USB_CAMERA_SNAPSHOT_WIDTH;
    }
    if (out_height != NULL)
    {
        *out_height = USB_CAMERA_SNAPSHOT_HEIGHT;
    }
}

static void yuyv_to_rgb_pixel(const uint8_t *frame, uint16_t x, uint16_t y,
                              uint8_t *r, uint8_t *g, uint8_t *b)
{
    const uint32_t pair_x = (uint32_t)(x & (uint16_t)~1U);
    const uint32_t idx = ((uint32_t)y * USB_CAMERA_FRAME_WIDTH + pair_x) * 2U;
    const int32_t y0 = frame[idx + 0U];
    const int32_t u = frame[idx + 1U] - 128;
    const int32_t y1 = frame[idx + 2U];
    const int32_t v = frame[idx + 3U] - 128;
    const int32_t y_value = ((x & 1U) == 0U) ? y0 : y1;
    const int32_t c = y_value - 16;
    const int32_t d = u;
    const int32_t e = v;
    const int32_t r_tmp = (298 * c + 409 * e + 128) >> 8;
    const int32_t g_tmp = (298 * c - 100 * d - 208 * e + 128) >> 8;
    const int32_t b_tmp = (298 * c + 516 * d + 128) >> 8;

    *r = clamp_u8(r_tmp);
    *g = clamp_u8(g_tmp);
    *b = clamp_u8(b_tmp);
}

static uint8_t clamp_u8(int32_t value)
{
    if (value < 0)
    {
        return 0U;
    }
    if (value > 255)
    {
        return 255U;
    }
    return (uint8_t)value;
}
