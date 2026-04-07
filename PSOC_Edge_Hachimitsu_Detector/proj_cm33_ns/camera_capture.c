#include "camera_capture.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cy_utils.h"
#include "cyabs_rtos.h"
#include "usb_camera_host.h"

#define CAMERA_CAPTURE_B64_PREFIX           ""
#define CAMERA_CAPTURE_BMP_BUFFER_SIZE       USB_CAMERA_SNAPSHOT_BMP_BYTES
#define CAMERA_CAPTURE_BASE64_BUFFER_SIZE    (4U * ((CAMERA_CAPTURE_BMP_BUFFER_SIZE + 2U) / 3U) + 4U)
#define CAMERA_CAPTURE_REQUEST_TIMEOUT_MS    (2500U)
#define CAMERA_CAPTURE_INITIAL_TIMEOUT_MS    (30000U)
#define CAMERA_CAPTURE_FAILURE_COOLDOWN      (5U)
#define CAMERA_CAPTURE_WARMUP_RETRY_MS       (5000U)
#define CAMERA_CAPTURE_WARMUP_STACK_SIZE     (4096U)
#define CAMERA_CAPTURE_WARMUP_PRIORITY       (3U)

CY_SECTION(".cy_gpu_buf")
static uint8_t camera_bmp_buffer[CAMERA_CAPTURE_BMP_BUFFER_SIZE];
CY_SECTION(".cy_gpu_buf")
static char camera_base64_buffer[CAMERA_CAPTURE_BASE64_BUFFER_SIZE];
static bool camera_capture_initialized = false;
static bool camera_capture_warning_printed = false;
static bool camera_cached_snapshot_available = false;
static bool camera_warmup_warning_printed = false;
static uint8_t camera_failure_cooldown = 0U;
static cy_thread_t camera_warmup_thread;
static bool camera_warmup_started = false;

static size_t base64_encode_bytes(const uint8_t *input, size_t input_len, char *output, size_t output_size);
static bool prepare_snapshot_payload(AddMeowDto *dto, bool reused_frame);
static void camera_capture_warmup_task(void *arg);

cy_rslt_t camera_capture_init(void)
{
    cy_rslt_t result = usb_camera_host_init();

    camera_capture_initialized = (result == CY_RSLT_SUCCESS);
    camera_capture_warning_printed = false;
    camera_warmup_warning_printed = false;

    if (camera_capture_initialized)
    {
        printf("[CAMERA] Snapshot hook initialized. Waiting for USB camera frames.\n");
        if (!camera_warmup_started)
        {
            if (cy_rtos_thread_create(&camera_warmup_thread,
                                      camera_capture_warmup_task,
                                      "CAM Warmup",
                                      NULL,
                                      CAMERA_CAPTURE_WARMUP_STACK_SIZE,
                                      CAMERA_CAPTURE_WARMUP_PRIORITY,
                                      NULL) == CY_RSLT_SUCCESS)
            {
                camera_warmup_started = true;
                printf("[CAMERA] Background warmup task started.\n");
            }
            else
            {
                printf("[CAMERA] Failed to start background warmup task.\n");
            }
        }
    }
    else
    {
        printf("[CAMERA] Snapshot hook init failed\n");
    }

    return result;
}

bool camera_capture_fill_meow_payload(AddMeowDto *dto, const ipc_msg_t *msg)
{
    size_t bmp_len = 0U;
    uint32_t timeout_ms;
    bool snapshot_ready = false;
    bool request_fresh_snapshot = false;

    if (dto == NULL)
    {
        return false;
    }

    dto->imageBase64 = NULL;
    dto->imageContentType = NULL;

    if (!camera_capture_initialized)
    {
        return false;
    }

    request_fresh_snapshot = ((msg != NULL) && (msg->request_snapshot != 0U));

    if (!camera_cached_snapshot_available)
    {
        if (!camera_warmup_warning_printed)
        {
            printf("[CAMERA] Warmup cache is not ready yet, skipping image upload for this event.\n");
            camera_warmup_warning_printed = true;
        }
        return false;
    }

    if (!request_fresh_snapshot)
    {
        if (usb_camera_host_get_snapshot_bmp(camera_bmp_buffer,
                                             sizeof(camera_bmp_buffer),
                                             &bmp_len,
                                             NULL,
                                             NULL))
        {
            camera_capture_warning_printed = false;
            return prepare_snapshot_payload(dto, true);
        }

        return false;
    }

    if (camera_failure_cooldown > 0U)
    {
        camera_failure_cooldown--;
        if (!camera_capture_warning_printed)
        {
            printf("[CAMERA] Snapshot retry cooldown active, skipping new camera request.\n");
            camera_capture_warning_printed = true;
        }
        return false;
    }

    printf("[CAMERA] Requesting on-demand snapshot\n");
    timeout_ms = camera_cached_snapshot_available
                     ? CAMERA_CAPTURE_REQUEST_TIMEOUT_MS
                     : CAMERA_CAPTURE_INITIAL_TIMEOUT_MS;

    snapshot_ready = usb_camera_host_request_snapshot(timeout_ms);
    if (snapshot_ready &&
        usb_camera_host_get_snapshot_bmp(camera_bmp_buffer,
                                         sizeof(camera_bmp_buffer),
                                         &bmp_len,
                                         NULL,
                                         NULL))
    {
        camera_capture_warning_printed = false;
        camera_warmup_warning_printed = false;
        camera_failure_cooldown = 0U;
        camera_cached_snapshot_available = true;
        return prepare_snapshot_payload(dto, false);
    }

    if (!snapshot_ready)
    {
        camera_failure_cooldown = CAMERA_CAPTURE_FAILURE_COOLDOWN;
    }
    else
    {
        printf("[CAMERA] Snapshot completed but no frame buffer was produced.\n");
    }

    if (camera_cached_snapshot_available &&
        usb_camera_host_get_snapshot_bmp(camera_bmp_buffer,
                                         sizeof(camera_bmp_buffer),
                                         &bmp_len,
                                         NULL,
                                         NULL))
    {
        printf("[CAMERA] Reusing last successful snapshot because a fresh frame was unavailable.\n");
        camera_capture_warning_printed = false;
        return prepare_snapshot_payload(dto, true);
    }

    if (!camera_capture_warning_printed)
    {
        printf("[CAMERA] Snapshot request timed out or camera stream was unavailable.\n");
        camera_capture_warning_printed = true;
    }
    return false;
}

static bool prepare_snapshot_payload(AddMeowDto *dto, bool reused_frame)
{
    size_t bmp_len = 0U;
    uint16_t width = 0U;
    uint16_t height = 0U;
    size_t base64_len;

    if (!usb_camera_host_get_snapshot_bmp(camera_bmp_buffer,
                                          sizeof(camera_bmp_buffer),
                                          &bmp_len,
                                          &width,
                                          &height))
    {
        return false;
    }

    base64_len = base64_encode_bytes(camera_bmp_buffer,
                                     bmp_len,
                                     camera_base64_buffer,
                                     sizeof(camera_base64_buffer));
    if (base64_len == 0U)
    {
        printf("[CAMERA] Failed to base64-encode snapshot\n");
        return false;
    }

    dto->imageBase64 = camera_base64_buffer;
    dto->imageContentType = "image/bmp";
    if (reused_frame)
    {
        printf("[CAMERA] Reused cached snapshot %ux%u (%lu bytes BMP)\n",
               (unsigned)width,
               (unsigned)height,
               (unsigned long)bmp_len);
    }
    else
    {
        printf("[CAMERA] Snapshot prepared %ux%u (%lu bytes BMP)\n",
               (unsigned)width,
               (unsigned)height,
               (unsigned long)bmp_len);
    }
    return true;
}

static void camera_capture_warmup_task(void *arg)
{
    size_t bmp_len = 0U;

    (void)arg;

    cy_rtos_delay_milliseconds(2000U);

    while (camera_capture_initialized && !camera_cached_snapshot_available)
    {
        if (usb_camera_host_request_snapshot(CAMERA_CAPTURE_INITIAL_TIMEOUT_MS) &&
            usb_camera_host_get_snapshot_bmp(camera_bmp_buffer,
                                             sizeof(camera_bmp_buffer),
                                             &bmp_len,
                                             NULL,
                                             NULL))
        {
            camera_cached_snapshot_available = true;
            camera_capture_warning_printed = false;
            camera_warmup_warning_printed = false;
            camera_failure_cooldown = 0U;
            printf("[CAMERA] Warmup snapshot cached for future uploads.\n");
            break;
        }

        if (usb_camera_host_get_snapshot_bmp(camera_bmp_buffer,
                                             sizeof(camera_bmp_buffer),
                                             &bmp_len,
                                             NULL,
                                             NULL))
        {
            camera_cached_snapshot_available = true;
            camera_capture_warning_printed = false;
            camera_warmup_warning_printed = false;
            camera_failure_cooldown = 0U;
            printf("[CAMERA] Warmup captured a late frame after timeout; caching it for future uploads.\n");
            break;
        }

        printf("[CAMERA] Warmup snapshot attempt did not produce a frame. Retrying...\n");
        cy_rtos_delay_milliseconds(CAMERA_CAPTURE_WARMUP_RETRY_MS);
    }

    cy_rtos_exit_thread();
}

static size_t base64_encode_bytes(const uint8_t *input, size_t input_len, char *output, size_t output_size)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t input_index = 0U;
    size_t output_index = 0U;

    if ((input == NULL) || (output == NULL) || (output_size == 0U))
    {
        return 0U;
    }

    while (input_index < input_len)
    {
        const size_t remaining = input_len - input_index;
        const uint32_t octet_a = input[input_index++];
        const uint32_t octet_b = (remaining > 1U) ? input[input_index++] : 0U;
        const uint32_t octet_c = (remaining > 2U) ? input[input_index++] : 0U;
        const uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        if ((output_index + 4U) >= output_size)
        {
            return 0U;
        }

        output[output_index++] = table[(triple >> 18) & 0x3FU];
        output[output_index++] = table[(triple >> 12) & 0x3FU];
        output[output_index++] = (remaining > 1U) ? table[(triple >> 6) & 0x3FU] : '=';
        output[output_index++] = (remaining > 2U) ? table[triple & 0x3FU] : '=';
    }

    output[output_index] = '\0';
    return output_index;
}
