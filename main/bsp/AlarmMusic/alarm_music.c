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

static const char *TAG = "alarm_music";

static TaskHandle_t s_alarm_music_task = NULL;
static SemaphoreHandle_t s_alarm_music_sem = NULL;
static volatile bool s_alarm_music_stop = false;
static volatile uint8_t s_last_key_code = XL9555_KEY_NONE;

/**
 * @brief 闹钟音乐任务：从低音量逐渐增大，每30秒增大一次，直到按KEY2停止
 */
static void alarm_music_task_fn(void *arg)
{
    const uint8_t max_volume = 33;
    const uint8_t min_volume = 3;
    const uint8_t volume_step = 3;
    const uint32_t volume_increase_period = 30000;  /* 30秒 */

    uint8_t current_volume = min_volume;
    uint32_t last_increase_time = 0;

    while (1)
    {
        /* 等待闹钟触发信号 */
        if (xSemaphoreTake(s_alarm_music_sem, portMAX_DELAY) == pdTRUE)
        {
            ESP_LOGI(TAG, "闹钟音乐启动，开始渐进式音量增大");
            s_alarm_music_stop = false;
            s_last_key_code = XL9555_KEY_NONE;
            current_volume = min_volume;
            last_increase_time = xTaskGetTickCount();

            /* 启动音乐播放 */
            if (audio_player_start() != ESP_OK)
            {
                ESP_LOGE(TAG, "启动音乐播放失败");
                continue;
            }

            /* 设置初始音量 */
            audio_hw_set_volume(current_volume);

            /* 音乐持续播放并逐渐增大音量 */
            while (!s_alarm_music_stop)
            {
                uint32_t now = xTaskGetTickCount();
                uint32_t elapsed = (now - last_increase_time) * portTICK_PERIOD_MS;

                /* 每30秒增大一次音量 */
                if (elapsed >= volume_increase_period && current_volume < max_volume)
                {
                    current_volume += volume_step;
                    if (current_volume > max_volume)
                    {
                        current_volume = max_volume;
                    }
                    audio_hw_set_volume(current_volume);
                    ESP_LOGI(TAG, "音量增大到 %u", current_volume);
                    last_increase_time = now;
                }

                /* 检查KEY2停止（支持多次按键检测） */
                uint8_t key = xl9555_keys_scan(0);
                if (key == XL9555_KEY2 || s_last_key_code == XL9555_KEY2)
                {
                    ESP_LOGI(TAG, "按下KEY2，闹钟停止");
                    s_alarm_music_stop = true;
                    s_last_key_code = XL9555_KEY_NONE;
                    break;
                }

                vTaskDelay(pdMS_TO_TICKS(100));
            }

            /* 停止音乐播放 */
            audio_player_stop();
            
            /* 恢复音量到默认值 */
            audio_hw_set_volume(20);
            ESP_LOGI(TAG, "闹钟音乐结束");
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
    xSemaphoreGive(s_alarm_music_sem);
}
