/*
 * entity.h
 *
 *  Created on: 2025年12月6日
 *      Author: 14838
 */

#ifndef MODEL_ENTITY_H_
#define MODEL_ENTITY_H_

#include "cJSON.h"
#include <stdint.h>
#define EQUIPMENT_UUID       "7637DAA7-13BD-8266-1CF0-25135B4AA8B3"

typedef struct {
    char equipmentId[128];
    float confidence;
    double latitude;
    double longitude;
    int64_t timestamp;
    const char *imageBase64;
    const char *imageContentType;
} AddMeowDto;

char* meow_dto_to_json(AddMeowDto *dto);
int json_to_meow_dto(const char *json_str, AddMeowDto *dto);

typedef struct {
    char equipmentId[128];
    int64_t timestamp;
    const char *imageBase64;
    const char *imageContentType;
} AttachMeowImageDto;

char* attach_meow_image_dto_to_json(AttachMeowImageDto *dto);

typedef struct {
    char host[128];
    int code;
    char errorMessage[1024];
    void *data;        // 泛型内容交给用户处理
} ResponseResult;

char* response_result_to_json(ResponseResult *responseResult);
int json_to_response_result(const char *json_str, ResponseResult *responseResult, void (*data_parser)(cJSON *json_data, void **out_data));

#endif /* MODEL_ENTITY_H_ */
