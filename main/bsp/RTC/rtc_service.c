#include "rtc_service.h"

#include <sys/time.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define RTC_TAG "rtc_service"
#define SYNC_SUCCESS_BIT (1 << 0)  /* NTP同步成功 */
#define SYNC_FAIL_BIT    (1 << 1)  /* NTP同步失败 */

static TaskHandle_t s_rtc_sync_task = NULL;
static EventGroupHandle_t s_rtc_event_group = NULL;

static void rtc_prepare_timezone(void)
{
    setenv("TZ", "CST-8", 1); /* 中国标准时间，UTC+8 */
    tzset();
}

void rtc_set_time(int year, int mon, int mday, int hour, int min, int sec)
{
    struct tm time_set;
    time_t second;

    time_set.tm_year  = year - 1900;
    time_set.tm_mon   = mon - 1;
    time_set.tm_mday  = mday;
    time_set.tm_hour  = hour;
    time_set.tm_min   = min;
    time_set.tm_sec   = sec;
    time_set.tm_isdst = -1;

    second = mktime(&time_set);

    struct timeval val = {
        .tv_sec = second,
        .tv_usec = 0
    };

    settimeofday(&val, NULL);
}

bool rtc_get_time(rtc_calendar_t *out_calendar)
{
    if (!out_calendar) {
        return false;
    }

    time_t second;
    struct tm time_block;

    time(&second);
    localtime_r(&second, &time_block);

    out_calendar->year  = time_block.tm_year + 1900;
    out_calendar->month = time_block.tm_mon + 1;
    out_calendar->date  = time_block.tm_mday;
    out_calendar->week  = time_block.tm_wday;
    out_calendar->hour  = time_block.tm_hour;
    out_calendar->min   = time_block.tm_min;
    out_calendar->sec   = time_block.tm_sec;
    return true;
}

bool rtc_time_is_valid(void)
{
    time_t second;
    time(&second);
    return (second > 1600000000); /* ~2020-09-13 */
}

static bool s_sntp_initialized = false;

/**
 * @brief 初始化SNTP（只需要初始化一次）
 */
static void sntp_init_once(void)
{
    if (s_sntp_initialized) {
        return;
    }
    
    rtc_prepare_timezone();
    
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    /* Configure multiple NTP servers for maximum compatibility */
    /* China - Alibaba Cloud (优先使用国内服务器) */
    esp_sntp_setservername(0, "ntp.aliyun.com");
    
    /* China - Tencent Cloud */
    esp_sntp_setservername(1, "time1.cloud.tencent.com");
    esp_sntp_setservername(2, "time2.cloud.tencent.com");
    
    /* China - Government & Open Source */
    esp_sntp_setservername(3, "cn.ntp.org.cn");
    esp_sntp_setservername(4, "ntp.ntsc.ac.cn");
    
    /* Global/International */
    esp_sntp_setservername(5, "pool.ntp.org");
    esp_sntp_setservername(6, "time.windows.com");
    
    esp_sntp_init();
    s_sntp_initialized = true;
    
    ESP_LOGI(RTC_TAG, "SNTP initialized");
}

bool rtc_sync_time_from_ntp(uint32_t wait_ms)
{
    /* 如果时间已经有效，直接返回成功 */
    if (rtc_time_is_valid()) {
        ESP_LOGI(RTC_TAG, "Time already valid, skip sync");
        return true;
    }
    
    /* 初始化SNTP（只初始化一次） */
    sntp_init_once();

    uint32_t elapsed = 0;
    const uint32_t step = 500;
    while (elapsed < wait_ms) {
        /* 检查SNTP同步状态 */
        sntp_sync_status_t st = esp_sntp_get_sync_status();
        if (st == SNTP_SYNC_STATUS_COMPLETED) {
            rtc_calendar_t now;
            rtc_get_time(&now);
            ESP_LOGI(RTC_TAG, "NTP synced: %04u-%02u-%02u %02u:%02u:%02u", now.year, now.month, now.date,
                     now.hour, now.min, now.sec);
            return true;
        }
        
        /* 也检查时间是否已经有效（SNTP可能在后台完成同步） */
        if (rtc_time_is_valid()) {
            rtc_calendar_t now;
            rtc_get_time(&now);
            ESP_LOGI(RTC_TAG, "Time valid (background sync): %04u-%02u-%02u %02u:%02u:%02u", now.year, now.month, now.date,
                     now.hour, now.min, now.sec);
            return true;
        }
        
        vTaskDelay(pdMS_TO_TICKS(step));
        elapsed += step;
    }

    /* 最后再检查一次时间是否有效 */
    if (rtc_time_is_valid()) {
        rtc_calendar_t now;
        rtc_get_time(&now);
        ESP_LOGI(RTC_TAG, "Time valid after wait: %04u-%02u-%02u %02u:%02u:%02u", now.year, now.month, now.date,
                 now.hour, now.min, now.sec);
        return true;
    }
    
    ESP_LOGW(RTC_TAG, "NTP sync timeout");
    return false;
}

/**
 * @brief WiFi连接事件回调，在WiFi连接后触发NTP同步
 */
static void rtc_wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(RTC_TAG, "WiFi connected, trigger NTP sync now");
        if (s_rtc_event_group) {
            xEventGroupSetBits(s_rtc_event_group, SYNC_SUCCESS_BIT);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(RTC_TAG, "WiFi disconnected");
    }
}

/**
 * @brief NTP同步任务：简化逻辑，时间有效则不重试
 */
static void rtc_sync_task(void *arg)
{
    if (!s_rtc_event_group) {
        s_rtc_event_group = xEventGroupCreate();
    }

    /* 注册WiFi事件监听 */
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &rtc_wifi_event_handler, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &rtc_wifi_event_handler, NULL);

    /* 等待一小段时间让WiFi先连接 */
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    /* 初始同步 */
    ESP_LOGI(RTC_TAG, "Initial NTP sync attempt");
    rtc_sync_time_from_ntp(15000);

    while (1) {
        /* 检查时间是否已有效 */
        if (rtc_time_is_valid()) {
            /* 时间有效，10分钟后定期同步 */
            ESP_LOGI(RTC_TAG, "Time is valid, next sync in 10 minutes");
            vTaskDelay(pdMS_TO_TICKS(10 * 60 * 1000));
            
            /* 定期同步（刷新时间） */
            ESP_LOGI(RTC_TAG, "Periodic NTP sync");
            rtc_sync_time_from_ntp(15000);
        } else {
            /* 时间无效，等待WiFi事件或10秒后重试 */
            ESP_LOGI(RTC_TAG, "Time invalid, waiting for sync...");
            xEventGroupWaitBits(s_rtc_event_group,
                               SYNC_SUCCESS_BIT,
                               pdTRUE,
                               pdFALSE,
                               pdMS_TO_TICKS(10000));
            
            /* 尝试同步 */
            rtc_sync_time_from_ntp(15000);
        }
    }
}

esp_err_t rtc_start_periodic_sync(uint32_t interval_ms)
{
    if (s_rtc_sync_task) {
        return ESP_OK;
    }

    BaseType_t r = xTaskCreate(rtc_sync_task, "rtc_ntp_sync", 3072, NULL, 4, &s_rtc_sync_task);
    if (r != pdPASS) {
        s_rtc_sync_task = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t rtc_do_sync_now(uint32_t wait_ms)
{
    if (wait_ms < 1000) {
        wait_ms = 30000;
    }
    return rtc_sync_time_from_ntp(wait_ms) ? ESP_OK : ESP_FAIL;
}

void rtc_stop_periodic_sync(void)
{
    if (s_rtc_sync_task) {
        vTaskDelete(s_rtc_sync_task);
        s_rtc_sync_task = NULL;
    }
    
    if (s_rtc_event_group) {
        vEventGroupDelete(s_rtc_event_group);
        s_rtc_event_group = NULL;
    }
}
