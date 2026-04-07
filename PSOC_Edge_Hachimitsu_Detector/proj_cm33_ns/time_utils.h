/*
 * time_utils.h
 *
 * 时间工具函数头文件
 * 提供 Unix timestamp 与 Cypress RTC 时间结构之间的转换功能
 */

#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include "cy_rtc.h"
#include <stdint.h>
#include "cy_result.h"

#define TZ_OFFSET_HOUR          8

cy_rslt_t timestamp_to_rtc_config(int64_t timestamp, cy_stc_rtc_config_t *rtc_config);
void rtc_config_to_timestamp(int64_t* timestamp, cy_stc_rtc_config_t *rtc_config);

#endif /* TIME_UTILS_H */

