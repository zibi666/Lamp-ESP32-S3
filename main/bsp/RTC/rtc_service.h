#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t date;
    uint8_t week;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
} rtc_calendar_t;

void rtc_set_time(int year, int mon, int mday, int hour, int min, int sec);
bool rtc_get_time(rtc_calendar_t *out_calendar);
bool rtc_sync_time_from_ntp(uint32_t wait_ms);
esp_err_t rtc_start_periodic_sync(uint32_t interval_ms);
void rtc_stop_periodic_sync(void);
bool rtc_time_is_valid(void);
esp_err_t rtc_do_sync_now(uint32_t wait_ms);

#ifdef __cplusplus
}
#endif
