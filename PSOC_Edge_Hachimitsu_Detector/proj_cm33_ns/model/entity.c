/*
 * entity.c
 */
#include "entity.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int format_int64_decimal(char *buffer, size_t buffer_size, int64_t value);

char* meow_dto_to_json(AddMeowDto *dto)
{
    cJSON *root = cJSON_CreateObject();
    char *json_str;

    cJSON_AddStringToObject(root, "equipmentId", dto->equipmentId);
    cJSON_AddNumberToObject(root, "confidence", dto->confidence);
    cJSON_AddNumberToObject(root, "latitude", dto->latitude);
    cJSON_AddNumberToObject(root, "longitude", dto->longitude);
    cJSON_AddNumberToObject(root, "timestamp", dto->timestamp);
    if ((dto->imageBase64 != NULL) && (dto->imageBase64[0] != '\0'))
    {
        cJSON_AddStringToObject(root, "imageBase64", dto->imageBase64);
    }
    if ((dto->imageContentType != NULL) && (dto->imageContentType[0] != '\0'))
    {
        cJSON_AddStringToObject(root, "imageContentType", dto->imageContentType);
    }

    json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

int json_to_meow_dto(const char *json_str, AddMeowDto *dto)
{
    cJSON *root = cJSON_Parse(json_str);
    cJSON *equipmentId;
    cJSON *confidence;
    cJSON *latitude;
    cJSON *longitude;
    cJSON *timestamp;

    if (root == NULL)
    {
        return -1;
    }

    equipmentId = cJSON_GetObjectItem(root, "equipmentId");
    confidence = cJSON_GetObjectItem(root, "confidence");
    latitude = cJSON_GetObjectItem(root, "latitude");
    longitude = cJSON_GetObjectItem(root, "longitude");
    timestamp = cJSON_GetObjectItem(root, "timestamp");

    if (!cJSON_IsString(equipmentId) || !cJSON_IsNumber(confidence) ||
        !cJSON_IsNumber(latitude) || !cJSON_IsNumber(longitude) ||
        !cJSON_IsNumber(timestamp))
    {
        cJSON_Delete(root);
        return -2;
    }

    strcpy(dto->equipmentId, equipmentId->valuestring);
    dto->confidence = (float)confidence->valuedouble;
    dto->latitude = latitude->valuedouble;
    dto->longitude = longitude->valuedouble;
    dto->timestamp = (int64_t)timestamp->valuedouble;
    dto->imageBase64 = NULL;
    dto->imageContentType = NULL;

    cJSON_Delete(root);
    return 0;
}

char* attach_meow_image_dto_to_json(AttachMeowImageDto *dto)
{
    const char *equipment_id = "";
    const char *image_base64 = "";
    const char *image_content_type = "";
    char timestamp_buffer[24];
    size_t json_len;
    char *json_str;
    int written;

    if (dto == NULL)
    {
        return NULL;
    }

    if (dto->equipmentId[0] != '\0')
    {
        equipment_id = dto->equipmentId;
    }
    if (dto->imageBase64 != NULL)
    {
        image_base64 = dto->imageBase64;
    }
    if (dto->imageContentType != NULL)
    {
        image_content_type = dto->imageContentType;
    }

    if (format_int64_decimal(timestamp_buffer, sizeof(timestamp_buffer), dto->timestamp) != 0)
    {
        return NULL;
    }

    json_len = strlen(equipment_id) + strlen(image_base64) + strlen(image_content_type) + 128U;
    json_str = (char *)malloc(json_len);
    if (json_str == NULL)
    {
        return NULL;
    }

    written = snprintf(json_str,
                       json_len,
                       "{\"equipmentId\":\"%s\",\"timestamp\":%s,\"imageBase64\":\"%s\",\"imageContentType\":\"%s\"}",
                       equipment_id,
                       timestamp_buffer,
                       image_base64,
                       image_content_type);
    if ((written < 0) || ((size_t)written >= json_len))
    {
        free(json_str);
        return NULL;
    }

    return json_str;
}

static int format_int64_decimal(char *buffer, size_t buffer_size, int64_t value)
{
    char reverse_digits[24];
    size_t digit_count = 0U;
    size_t write_index = 0U;
    uint64_t magnitude;
    int negative = 0;

    if ((buffer == NULL) || (buffer_size == 0U))
    {
        return -1;
    }

    if (value < 0)
    {
        negative = 1;
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
        return -1;
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
    return 0;
}

char* response_result_to_json(ResponseResult *responseResult)
{
    cJSON *root = cJSON_CreateObject();
    char *json_str;

    cJSON_AddStringToObject(root, "host", responseResult->host);
    cJSON_AddNumberToObject(root, "code", responseResult->code);
    cJSON_AddStringToObject(root, "errorMessage", responseResult->errorMessage);
    cJSON_AddItemToObject(root, "data", responseResult->data);

    json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

int json_to_response_result(
    const char *json_str,
    ResponseResult *responseResult,
    void (*data_parser)(cJSON *json_data, void **out_data))
{
    cJSON *root = cJSON_Parse(json_str);
    cJSON *host;
    cJSON *code;
    cJSON *errorMessage;
    cJSON *data;

    if (root == NULL)
    {
        return -1;
    }

    host = cJSON_GetObjectItem(root, "host");
    code = cJSON_GetObjectItem(root, "code");
    errorMessage = cJSON_GetObjectItem(root, "errorMessage");
    data = cJSON_GetObjectItem(root, "data");

    if (!cJSON_IsNumber(code))
    {
        cJSON_Delete(root);
        return -2;
    }
    responseResult->code = (int)code->valuedouble;

    if (cJSON_IsString(errorMessage) && (errorMessage->valuestring != NULL))
    {
        strcpy(responseResult->errorMessage, errorMessage->valuestring);
    }
    else
    {
        responseResult->errorMessage[0] = '\0';
    }

    if (cJSON_IsString(host) && (host->valuestring != NULL))
    {
        strcpy(responseResult->host, host->valuestring);
    }
    else
    {
        responseResult->host[0] = '\0';
    }

    if ((data_parser != NULL) && (data != NULL) && !cJSON_IsNull(data))
    {
        data_parser(data, &responseResult->data);
    }
    else
    {
        responseResult->data = NULL;
    }

    cJSON_Delete(root);
    return 0;
}
