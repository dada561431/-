/*
 * time_utils.c
 *
 * 时间工具函数实现
 * 提供 Unix timestamp 与 Cypress RTC 时间结构之间的转换功能
 */

#include "time_utils.h"
#include "cy_rtc.h"
#include <time.h>
#include <stdint.h>
#include <stdio.h>

// 使用映射数组将标准库的星期几映射到 RTC 常量
static const uint32_t day_of_week_map[] = {
    CY_RTC_SUNDAY,    // 0
    CY_RTC_MONDAY,    // 1
    CY_RTC_TUESDAY,   // 2
    CY_RTC_WEDNESDAY, // 3
    CY_RTC_THURSDAY,  // 4
    CY_RTC_FRIDAY,    // 5
    CY_RTC_SATURDAY   // 6
};

// 使用映射数组将标准库的月份映射到 RTC 常量
static const uint32_t month_map[] = {
    CY_RTC_JANUARY,   // 0
    CY_RTC_FEBRUARY,  // 1
    CY_RTC_MARCH,     // 2
    CY_RTC_APRIL,     // 3
    CY_RTC_MAY,       // 4
    CY_RTC_JUNE,      // 5
    CY_RTC_JULY,      // 6
    CY_RTC_AUGUST,    // 7
    CY_RTC_SEPTEMBER,// 8
    CY_RTC_OCTOBER,   // 9
    CY_RTC_NOVEMBER,  // 10
    CY_RTC_DECEMBER   // 11
};

/**
 * 将 Unix timestamp (int64_t) 转换为 cy_stc_rtc_config_t 结构体
 * 
 * @param timestamp Unix 时间戳（自 1970-01-01 00:00:00 UTC 起的秒数）
 * @param rtc_config 输出的 RTC 配置结构体指针
 * @return 0 表示成功，-1 表示失败
 */
cy_rslt_t timestamp_to_rtc_config(int64_t timestamp, cy_stc_rtc_config_t *rtc_config) {
	if (rtc_config == NULL) {
		return CY_RSLT_TYPE_ERROR;
	}
	// 时区偏移
	time_t unix_time = (time_t)(timestamp + TZ_OFFSET_HOUR * 3600);
	struct tm *time_info = gmtime(&unix_time);
	
	if (time_info == NULL) {
		return CY_RSLT_TYPE_ERROR;
	}
	
	// 将 struct tm 转换为 cy_stc_rtc_config_t
	rtc_config->sec = (uint32_t)time_info->tm_sec;
	rtc_config->min = (uint32_t)time_info->tm_min;
	rtc_config->hour = (uint32_t)time_info->tm_hour;
	rtc_config->amPm = (time_info->tm_hour < 12) ? CY_RTC_AM : CY_RTC_PM;
	rtc_config->hrFormat = CY_RTC_24_HOURS;
	
	// 转换星期几 (tm_wday: 0=Sunday, RTC: 0=Sunday)
	if (time_info->tm_wday < 7) {
		rtc_config->dayOfWeek = day_of_week_map[time_info->tm_wday];
	} else {
		rtc_config->dayOfWeek = CY_RTC_SUNDAY;  // 默认值
	}
	
	rtc_config->date = (uint32_t)time_info->tm_mday;
	
	// 转换月份 (tm_mon: 0=January, RTC: 0=January)
	if (time_info->tm_mon < 12) {
		rtc_config->month = month_map[time_info->tm_mon];
	} else {
		rtc_config->month = CY_RTC_JANUARY;  // 默认值
	}
	
	// 转换年份 (tm_year: years since 1900, RTC: years since 2000)
	// 例如：2024年 -> tm_year=124, RTC year=24
	rtc_config->year = (uint32_t)(time_info->tm_year - 100);
	
	return CY_RSLT_SUCCESS;
}

void rtc_config_to_timestamp(int64_t* timestamp, cy_stc_rtc_config_t *rtc_config) {
    if (!timestamp || !rtc_config) {
        return;
    }
    struct tm time_info;
    time_info.tm_sec  = rtc_config->sec;
    time_info.tm_min  = rtc_config->min;
    time_info.tm_hour = rtc_config->hour;
    time_info.tm_mday = rtc_config->date;
    // ========= 修正月字段 =========
    time_info.tm_mon  = rtc_config->month - 1;  // RTC 1~12 → tm 0~11
    time_info.tm_year = rtc_config->year + 100; // RTC 从2000, tm从1900
    time_info.tm_isdst = 0;
    *timestamp = (mktime(&time_info) - TZ_OFFSET_HOUR * 3600) * 1000;
}