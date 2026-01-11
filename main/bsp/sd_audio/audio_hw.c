/**
 * @file audio_hw.c
 * @brief 音频硬件接口实现
 * 
 * 本文件提供 I2C 总线访问接口供 XL9555 等外设使用。
 * 音频播放功能由 main 项目的 ES8388 驱动处理。
 */

#include "audio_hw.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif
extern void* board_get_i2c_bus(void);  // 从 board.cc 获取 I2C 总线
#ifdef __cplusplus
}
#endif

static const char *TAG = "audio_hw";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static uint8_t s_volume = 20;

esp_err_t audio_hw_i2c_init(void)
{
    if (s_i2c_bus) {
        return ESP_OK;
    }

    /* 使用主项目已初始化的 I2C 总线 */
    s_i2c_bus = (i2c_master_bus_handle_t)board_get_i2c_bus();
    if (!s_i2c_bus) {
        ESP_LOGE(TAG, "Failed to get I2C bus from main project");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Using I2C bus from main project");
    return ESP_OK;
}

i2c_master_bus_handle_t audio_hw_get_i2c_bus(void)
{
    return s_i2c_bus;
}

/* 以下为兼容性存根函数，实际音频播放由 main 项目音频系统处理 */

esp_err_t audio_hw_init(void)
{
    ESP_LOGI(TAG, "音频播放使用 main 项目 ES8388 驱动");
    return audio_hw_i2c_init();
}

esp_err_t audio_hw_configure(uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels)
{
    /* 实际配置由 main 项目音频系统处理 */
    return ESP_OK;
}

esp_err_t audio_hw_start(void)
{
    return ESP_OK;
}

void audio_hw_stop(void)
{
}

size_t audio_hw_write(const uint8_t *data, size_t len, TickType_t timeout_ticks)
{
    /* 实际写入由 main 项目音频系统处理 */
    return len;
}

void audio_hw_deinit(void)
{
}

esp_err_t audio_hw_set_volume(uint8_t volume)
{
    if (volume > 33) {
        volume = 33;
    }
    s_volume = volume;
    /* 实际音量控制由 main 项目音频系统处理 */
    return ESP_OK;
}

uint8_t audio_hw_get_volume(void)
{
    return s_volume;
}
