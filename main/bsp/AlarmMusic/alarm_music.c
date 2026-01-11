#include "alarm_music.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "audio_player.h"
#include "audio_hw.h"
#include "xl9555_keys.h"
#include "app_controller.h"

static const char *TAG = "alarm_music";

static TaskHandle_t s_alarm_music_task = NULL;
static SemaphoreHandle_t s_alarm_music_sem = NULL;
static volatile bool s_alarm_music_stop = false;
static volatile uint8_t s_last_key_code = XL9555_KEY_NONE;
static volatile bool s_alarm_music_ringing = false;
static volatile TickType_t s_suppress_until_tick = 0;

/**
 * @brief 闹钟音乐任务：从低音量逐渐增大，每30秒增大一次，直到按KEY2停止
 */
static void alarm_music_task_fn(void *arg)
{
    const uint8_t max_volume = 33;
    const uint8_t alarm_start_volume = 6;
    const uint8_t rem_volume = 18;
    const uint8_t deep_volume_step = 2;
    const uint32_t deep_increase_period_ms = 20000;
    const uint32_t wake_stop_delay_ms = 30000;

    uint8_t current_volume = alarm_start_volume;
    TickType_t last_deep_increase_tick = 0;
    TickType_t wake_seen_tick = 0;
    sleep_stage_t last_stage = SLEEP_STAGE_UNKNOWN;
    bool stopped_by_key2 = false;

    while (1)
    {
        /* 等待闹钟触发信号 */
        if (xSemaphoreTake(s_alarm_music_sem, portMAX_DELAY) == pdTRUE)
        {
            stopped_by_key2 = false;
            ESP_LOGI(TAG, "闹钟音乐启动，开始渐进式音量增大");
            s_alarm_music_stop = false;
            s_last_key_code = XL9555_KEY_NONE;
            current_volume = alarm_start_volume;
            last_deep_increase_tick = xTaskGetTickCount();
            wake_seen_tick = 0;
            last_stage = SLEEP_STAGE_UNKNOWN;

            /* 启动音乐播放 */
            if (audio_player_start() != ESP_OK)
            {
                ESP_LOGE(TAG, "启动音乐播放失败");
                continue;
            }
            s_alarm_music_ringing = true;

            /* 设置初始音量 */
            audio_hw_set_volume(current_volume);

            /* 音乐持续播放并逐渐增大音量 */
            while (!s_alarm_music_stop)
            {
                TickType_t now = xTaskGetTickCount();
                sleep_stage_t stage = app_controller_get_current_sleep_stage();

                if (stage != last_stage) {
                    if (stage == SLEEP_STAGE_WAKE) {
                        wake_seen_tick = now;
                    }
                    if (stage == SLEEP_STAGE_NREM || stage == SLEEP_STAGE_UNKNOWN) {
                        last_deep_increase_tick = now;
                    }
                    last_stage = stage;
                }

                if (stage == SLEEP_STAGE_WAKE) {
                    if (wake_seen_tick == 0) {
                        wake_seen_tick = now;
                    } else if ((now - wake_seen_tick) * portTICK_PERIOD_MS >= wake_stop_delay_ms) {
                        ESP_LOGI(TAG, "检测到清醒超过%u ms，闹钟停止", (unsigned)wake_stop_delay_ms);
                        s_alarm_music_stop = true;
                        break;
                    }
                } else {
                    wake_seen_tick = 0;
                }

                if (stage == SLEEP_STAGE_REM) {
                    if (current_volume != rem_volume) {
                        current_volume = rem_volume;
                        audio_hw_set_volume(current_volume);
                        ESP_LOGI(TAG, "REM阶段，音量调整到 %u", current_volume);
                    }
                } else if ((stage == SLEEP_STAGE_NREM || stage == SLEEP_STAGE_UNKNOWN) && current_volume < max_volume) {
                    if ((now - last_deep_increase_tick) * portTICK_PERIOD_MS >= deep_increase_period_ms) {
                        uint8_t next = (uint8_t)(current_volume + deep_volume_step);
                        if (next > max_volume) {
                            next = max_volume;
                        }
                        if (next != current_volume) {
                            current_volume = next;
                            audio_hw_set_volume(current_volume);
                            ESP_LOGI(TAG, "深睡/未知阶段，音量增大到 %u", current_volume);
                        }
                        last_deep_increase_tick = now;
                    }
                }

                /* 检查KEY2停止（支持多次按键检测） */
                uint8_t key = xl9555_keys_scan(0);
                if (key == XL9555_KEY2 || s_last_key_code == XL9555_KEY2)
                {
                    ESP_LOGI(TAG, "按下KEY2，闹钟停止");
                    stopped_by_key2 = true;
                    s_suppress_until_tick = now + pdMS_TO_TICKS(70000);
                    s_alarm_music_stop = true;
                    s_last_key_code = XL9555_KEY_NONE;
                    break;
                }

                vTaskDelay(pdMS_TO_TICKS(100));
            }

            /* 停止音乐播放 */
            audio_player_stop();
            s_alarm_music_ringing = false;
            
            /* 恢复音量到默认值 */
            audio_hw_set_volume(20);
            ESP_LOGI(TAG, "闹钟音乐结束");

            while (xSemaphoreTake(s_alarm_music_sem, 0) == pdTRUE) {
            }

            if (stopped_by_key2) {
                TickType_t remain = 0;
                TickType_t now = xTaskGetTickCount();
                if (s_suppress_until_tick > now) {
                    remain = s_suppress_until_tick - now;
                }
                ESP_LOGI(TAG, "KEY2已停止，抑制重复响铃 %u ms", (unsigned)(remain * portTICK_PERIOD_MS));
            }
        }
    }
}

esp_err_t alarm_music_init(void)
{
    if (s_alarm_music_sem)
    {
        return ESP_OK;  /* 已初始化 */
    }

    s_alarm_music_sem = xSemaphoreCreateBinary();
    if (!s_alarm_music_sem)
    {
        ESP_LOGE(TAG, "创建信号量失败");
        return ESP_FAIL;
    }

    s_alarm_music_stop = false;
    return ESP_OK;
}

esp_err_t alarm_music_start(void)
{
    if (!s_alarm_music_sem)
    {
        ESP_LOGE(TAG, "闹钟音乐模块未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_alarm_music_task)
    {
        return ESP_OK;  /* 已启动 */
    }

    BaseType_t res = xTaskCreate(alarm_music_task_fn, "alarm_music", 4096, NULL, 5, &s_alarm_music_task);
    if (res != pdPASS)
    {
        ESP_LOGE(TAG, "创建闹钟音乐任务失败");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void alarm_music_stop(void)
{
    s_alarm_music_stop = true;
    if (s_alarm_music_task)
    {
        while (s_alarm_music_task)
        {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

void alarm_music_ring_callback(const alarm_info_t *alarm, void *ctx)
{
    (void)ctx;
    (void)alarm;
    
    if (!s_alarm_music_sem)
    {
        ESP_LOGE(TAG, "闹钟音乐信号量未初始化");
        return;
    }
    
    /* 触发闹钟音乐 */
    TickType_t now = xTaskGetTickCount();
    if (s_alarm_music_ringing) {
        return;
    }
    if (s_suppress_until_tick != 0 && now < s_suppress_until_tick) {
        return;
    }
    xSemaphoreGive(s_alarm_music_sem);
}
