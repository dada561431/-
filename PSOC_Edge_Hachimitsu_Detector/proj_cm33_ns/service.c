/*
 * service.c
 */

#include "service.h"
#include "camera_capture.h"
#include "cy_rtc.h"
#include "shared_memory.h"
#include "cJSON.h"
#include "model/entity.h"
#include "cy_http_client_api.h"
#include "time_utils.h"
#include "cy_utils.h"
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "cycfg_clocks.h"

#define MEOW_ADD "/meow/add"
#define MEOW_ATTACH_IMAGE "/meow/attach-image"
#define MEOW_ATTACH_YUYV "/meow/attach-yuyv"
#define MEOW_ATTACH_AUDIO "/meow/attach-audio"
#define RTC_SYNC_PATH "/rtc/get"
#define MEOW_ADD_JSON_BUFFER_SIZE (512U)
#define ATTACH_IMAGE_JSON_BUFFER_SIZE (65536U)
#define ATTACH_YUYV_PATH_BUFFER_SIZE (256U)
#define ATTACH_AUDIO_PATH_BUFFER_SIZE (256U)

static char meow_add_json_buffer[MEOW_ADD_JSON_BUFFER_SIZE];
CY_SECTION(".cy_gpu_buf")
static char attach_image_json_buffer[ATTACH_IMAGE_JSON_BUFFER_SIZE];
static char attach_yuyv_path_buffer[ATTACH_YUYV_PATH_BUFFER_SIZE];
static char attach_audio_path_buffer[ATTACH_AUDIO_PATH_BUFFER_SIZE];
CY_SECTION(".cy_gpu_buf")
static ipc_msg_t current_hachimitsu_msg;

static bool build_meow_add_json(const AddMeowDto *dto,
                                char *buffer,
                                size_t buffer_size);
static bool build_attach_image_json(const AttachMeowImageDto *dto,
                                    char *buffer,
                                    size_t buffer_size);
static void send_hachimitsu_audio(const ipc_msg_t *msg,
                                  const char *equipment_id,
                                  int64_t timestamp);
static void send_hachimitsu_image(const ipc_msg_t *msg,
                                  const char *equipment_id,
                                  int64_t timestamp);
static bool format_int64_decimal(int64_t value,
                                 char *buffer,
                                 size_t buffer_size);

void send_hachimitsu_log(void)
{
    ipc_msg_t *msg = &current_hachimitsu_msg;

    while (get_msg(msg))
    {
        cy_stc_rtc_config_t curr_date_time;
        int64_t timestamp = 0;
        AddMeowDto addMeowDto = {0};
        cy_http_client_response_t response;

        Cy_RTC_GetDateAndTime(&curr_date_time);
        rtc_config_to_timestamp(&timestamp, &curr_date_time);

        snprintf(addMeowDto.equipmentId, sizeof(addMeowDto.equipmentId), "%s", EQUIPMENT_UUID);
        addMeowDto.confidence = msg->confidence;
        addMeowDto.latitude = 1.14;
        addMeowDto.longitude = 5.14;
        addMeowDto.timestamp = timestamp;

        if (!build_meow_add_json(&addMeowDto,
                                 meow_add_json_buffer,
                                 sizeof(meow_add_json_buffer)))
        {
            printf("[HTTP Task] Failed to build meow upload JSON\n");
            if (!write_msg(msg))
            {
                printf("[HTTP Task] Requeue failed, dropping hachimitsu event\n");
            }

            break;
        }

        printf("[HTTP Task] Meow upload JSON length=%lu\n",
               (unsigned long)strlen(meow_add_json_buffer));

        if (fetch_https_client_method(CY_HTTP_CLIENT_METHOD_POST, MEOW_ADD, meow_add_json_buffer, &response) != CY_RSLT_SUCCESS)
        {
            printf("[HTTP Task] Upload failed, requeueing hachimitsu event\n");
            if (!write_msg(msg))
            {
                printf("[HTTP Task] Requeue failed, dropping hachimitsu event\n");
            }

            break;
        }

        printf("[HTTP Task] Meow upload completed\n");
        send_hachimitsu_audio(msg, addMeowDto.equipmentId, addMeowDto.timestamp);
        send_hachimitsu_image(msg, addMeowDto.equipmentId, addMeowDto.timestamp);
    }
}

static bool build_meow_add_json(const AddMeowDto *dto,
                                char *buffer,
                                size_t buffer_size)
{
    int written;
    char timestamp_buffer[24];

    if ((dto == NULL) || (buffer == NULL) || (buffer_size == 0U))
    {
        return false;
    }

    if (!format_int64_decimal(dto->timestamp, timestamp_buffer, sizeof(timestamp_buffer)))
    {
        return false;
    }

    written = snprintf(buffer,
                       buffer_size,
                       "{\"equipmentId\":\"%s\",\"confidence\":%.6f,\"latitude\":%.8f,\"longitude\":%.8f,\"timestamp\":%s}",
                       dto->equipmentId,
                       (double)dto->confidence,
                       dto->latitude,
                       dto->longitude,
                       timestamp_buffer);

    return (written >= 0) && ((size_t)written < buffer_size);
}

static void send_hachimitsu_audio(const ipc_msg_t *msg,
                                  const char *equipment_id,
                                  int64_t timestamp)
{
    cy_http_client_response_t response;
    uint32_t sample_count;
    uint32_t payload_len;
    char timestamp_buffer[24];
    int path_len;

    if ((msg == NULL) || (equipment_id == NULL))
    {
        return;
    }

    sample_count = msg->audio_sample_count;
    if ((sample_count == 0U) || (sample_count > IPC_MSG_AUDIO_SAMPLE_COUNT))
    {
        printf("[HTTP Task] No analysis audio available for upload\n");
        return;
    }

    payload_len = sample_count * sizeof(msg->audio_samples[0]);
    if (!format_int64_decimal(timestamp, timestamp_buffer, sizeof(timestamp_buffer)))
    {
        printf("[HTTP Task] Failed to format audio attach timestamp\n");
        return;
    }

    path_len = snprintf(attach_audio_path_buffer,
                        sizeof(attach_audio_path_buffer),
                        "%s?equipmentId=%s&timestamp=%s&sampleRate=%lu&bitsPerSample=%u&channelCount=%u&sampleCount=%lu&format=PCM_S16LE",
                        MEOW_ATTACH_AUDIO,
                        equipment_id,
                        timestamp_buffer,
                        (unsigned long)msg->audio_sample_rate,
                        (unsigned)msg->audio_bits_per_sample,
                        (unsigned)msg->audio_channel_count,
                        (unsigned long)sample_count);

    if ((path_len < 0) || ((size_t)path_len >= sizeof(attach_audio_path_buffer)))
    {
        printf("[HTTP Task] Audio upload path is too long\n");
        return;
    }

    printf("[HTTP Task] Analysis audio upload length=%lu samples=%lu duration=%lums\n",
           (unsigned long)payload_len,
           (unsigned long)sample_count,
           (unsigned long)msg->audio_duration_ms);

    if (fetch_https_client_binary_method(CY_HTTP_CLIENT_METHOD_POST,
                                         attach_audio_path_buffer,
                                         "application/octet-stream",
                                         (const uint8_t *)msg->audio_samples,
                                         payload_len,
                                         &response) != CY_RSLT_SUCCESS)
    {
        printf("[HTTP Task] Analysis audio upload failed\n");
    }
    else
    {
        printf("[HTTP Task] Analysis audio upload completed\n");
    }
}

static void send_hachimitsu_image(const ipc_msg_t *msg,
                                  const char *equipment_id,
                                  int64_t timestamp)
{
    AddMeowDto snapshotDto = {0};
    AttachMeowImageDto attachDto = {0};
    cy_http_client_response_t response;
    const uint8_t *yuyv_frame = NULL;
    size_t yuyv_len = 0U;
    uint16_t yuyv_width = 0U;
    uint16_t yuyv_height = 0U;
    uint16_t yuyv_source_stride = 0U;
    bool yuyv_reused_frame = false;
    bool yuyv_frame_borrowed = false;
    char timestamp_buffer[24];
    int path_len;
    ipc_msg_t fallback_msg;

    if (msg == NULL)
    {
        return;
    }

    yuyv_frame_borrowed = camera_capture_get_yuyv_snapshot(msg,
                                                           &yuyv_frame,
                                                           &yuyv_len,
                                                           &yuyv_width,
                                                           &yuyv_height,
                                                           &yuyv_source_stride,
                                                           &yuyv_reused_frame);
    if (yuyv_frame_borrowed &&
        format_int64_decimal(timestamp, timestamp_buffer, sizeof(timestamp_buffer)))
    {
        path_len = snprintf(attach_yuyv_path_buffer,
                            sizeof(attach_yuyv_path_buffer),
                            "%s?equipmentId=%s&timestamp=%s&width=%u&height=%u&sourceStride=%u&pixelFormat=YUYV",
                            MEOW_ATTACH_YUYV,
                            equipment_id,
                            timestamp_buffer,
                            (unsigned)yuyv_width,
                            (unsigned)yuyv_height,
                            (unsigned)yuyv_source_stride);
        if ((path_len >= 0) && ((size_t)path_len < sizeof(attach_yuyv_path_buffer)))
        {
            printf("[HTTP Task] Raw YUYV upload length=%lu stride=%u reused=%u\n",
                   (unsigned long)yuyv_len,
                   (unsigned)yuyv_source_stride,
                   yuyv_reused_frame ? 1U : 0U);
            if (fetch_https_client_binary_method(CY_HTTP_CLIENT_METHOD_POST,
                                                 attach_yuyv_path_buffer,
                                                 "application/octet-stream",
                                                 yuyv_frame,
                                                 (uint32_t)yuyv_len,
                                                 &response) == CY_RSLT_SUCCESS)
            {
                camera_capture_release_yuyv_snapshot();
                printf("[HTTP Task] Raw YUYV image upload completed\n");
                return;
            }

            printf("[HTTP Task] Raw YUYV image upload failed, trying thumbnail fallback\n");
        }
        else
        {
            printf("[HTTP Task] Raw YUYV upload path is too long\n");
        }
    }

    if (yuyv_frame_borrowed)
    {
        camera_capture_release_yuyv_snapshot();
    }

    fallback_msg = *msg;
    fallback_msg.request_snapshot = 0U;
    snprintf(snapshotDto.equipmentId, sizeof(snapshotDto.equipmentId), "%s", equipment_id);
    snapshotDto.timestamp = timestamp;
    if (!camera_capture_fill_meow_payload(&snapshotDto, &fallback_msg))
    {
        return;
    }

    printf("[CAMERA] Snapshot attached to delayed image upload\n");
    snprintf(attachDto.equipmentId, sizeof(attachDto.equipmentId), "%s", equipment_id);
    attachDto.timestamp = timestamp;
    attachDto.imageBase64 = snapshotDto.imageBase64;
    attachDto.imageContentType = snapshotDto.imageContentType;

    if (!build_attach_image_json(&attachDto,
                                 attach_image_json_buffer,
                                 sizeof(attach_image_json_buffer)))
    {
        printf("[HTTP Task] Failed to build delayed image upload JSON\n");
        return;
    }

    printf("[HTTP Task] Delayed image upload JSON length=%lu\n",
           (unsigned long)strlen(attach_image_json_buffer));

    if (fetch_https_client_method(CY_HTTP_CLIENT_METHOD_POST,
                                  MEOW_ATTACH_IMAGE,
                                  attach_image_json_buffer,
                                  &response) != CY_RSLT_SUCCESS)
    {
        printf("[HTTP Task] Delayed image upload failed\n");
    }
    else
    {
        printf("[HTTP Task] Delayed image upload completed\n");
    }
}

static bool build_attach_image_json(const AttachMeowImageDto *dto,
                                    char *buffer,
                                    size_t buffer_size)
{
    int written;
    char timestamp_buffer[24];

    if ((dto == NULL) || (buffer == NULL) || (buffer_size == 0U))
    {
        return false;
    }

    if (!format_int64_decimal(dto->timestamp, timestamp_buffer, sizeof(timestamp_buffer)))
    {
        return false;
    }

    written = snprintf(buffer,
                       buffer_size,
                       "{\"equipmentId\":\"%s\",\"timestamp\":%s,\"imageBase64\":\"%s\",\"imageContentType\":\"%s\"}",
                       dto->equipmentId,
                       timestamp_buffer,
                       (dto->imageBase64 != NULL) ? dto->imageBase64 : "",
                       (dto->imageContentType != NULL) ? dto->imageContentType : "");
    return (written >= 0) && ((size_t)written < buffer_size);
}

static bool format_int64_decimal(int64_t value,
                                 char *buffer,
                                 size_t buffer_size)
{
    char reverse_digits[24];
    size_t digit_count = 0U;
    size_t write_index = 0U;
    uint64_t magnitude;
    bool negative = false;

    if ((buffer == NULL) || (buffer_size == 0U))
    {
        return false;
    }

    if (value < 0)
    {
        negative = true;
        magnitude = (uint64_t)(-(value + 1)) + 1U;
    }
    else
    {
        magnitude = (uint64_t)value;
    }

    do
    {
        reverse_digits[digit_count++] = (char)('0' + (magnitude % 10U));
        magnitude /= 10U;
    } while ((magnitude != 0U) && (digit_count < sizeof(reverse_digits)));

    if ((magnitude != 0U) || ((digit_count + (negative ? 1U : 0U) + 1U) > buffer_size))
    {
        return false;
    }

    if (negative)
    {
        buffer[write_index++] = '-';
    }

    while (digit_count > 0U)
    {
        buffer[write_index++] = reverse_digits[--digit_count];
    }

    buffer[write_index] = '\0';
    return true;
}

void parse_rtc_resp(cJSON *json_data, void **out_data)
{
    int64_t *timestamp;

    if (!cJSON_IsNumber(json_data))
    {
        *out_data = NULL;
        return;
    }

    timestamp = malloc(sizeof(int64_t));
    if (timestamp == NULL)
    {
        *out_data = NULL;
        return;
    }

    *timestamp = (int64_t) json_data->valuedouble;
    *out_data = timestamp;
}

void sync_rtc(void)
{
    cy_http_client_response_t response;
    ResponseResult resp;
    int64_t timestamp;
    cy_stc_rtc_config_t rtc_config;
    cy_stc_rtc_config_t curr_date_time;

    if (fetch_https_client_method(CY_HTTP_CLIENT_METHOD_GET, RTC_SYNC_PATH, "", &response) != CY_RSLT_SUCCESS)
    {
        printf("[HTTP Task] Failed to sync RTC from server\n");
        return;
    }

    json_to_response_result((const char *) response.body, &resp, parse_rtc_resp);

    if (resp.data == NULL)
    {
        printf("[HTTP Task] Failed to parse timestamp\n");
        return;
    }

    timestamp = *((int64_t *) resp.data) / 1000;
    free(resp.data);

    {
        char timestamp_buffer[24];

        if (format_int64_decimal(timestamp, timestamp_buffer, sizeof(timestamp_buffer)))
        {
            printf("[HTTP Task] Receive Server Time: %s\n", timestamp_buffer);
        }
        else
        {
            printf("[HTTP Task] Receive Server Time conversion failed\n");
        }
    }

    if (timestamp_to_rtc_config(timestamp, &rtc_config) != 0)
    {
        printf("[HTTP Task] Failed to convert timestamp\n");
        return;
    }

    Cy_RTC_SetDateAndTime(&rtc_config);
    Cy_RTC_GetDateAndTime(&curr_date_time);
    printf("[HTTP Task] RTC Time set successfully: 20%ld-%ld-%ld %ld:%ld:%ld\n",
           curr_date_time.year,
           curr_date_time.month,
           curr_date_time.date,
           curr_date_time.hour,
           curr_date_time.min,
           curr_date_time.sec);
}
