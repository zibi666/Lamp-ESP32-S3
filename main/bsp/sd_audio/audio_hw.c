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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif
extern void* board_get_i2c_bus(void);  // 从 board.cc 获取 I2C 总线
extern void board_audio_enable_output(int enable);
extern void board_audio_set_output_volume(int volume_0_100);
extern void board_audio_set_output_volume_runtime(int volume_0_100);
extern int board_audio_output_sample_rate(void);
extern int board_audio_write_samples(const int16_t* data, int samples);
extern void app_audio_notify_external_output(void);
extern int board_audio_begin_external_playback(int sample_rate, int channels);
extern void board_audio_end_external_playback(void);
#ifdef __cplusplus
}
#endif

static const char *TAG = "audio_hw";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static uint8_t s_volume = 20;
static uint32_t s_sample_rate_hz = 0;
static uint8_t s_bits_per_sample = 0;
static uint8_t s_channels = 0;
static SemaphoreHandle_t s_audio_hw_buf_mutex = NULL;
static bool s_external_playback = false;

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
    if (!s_audio_hw_buf_mutex) {
        s_audio_hw_buf_mutex = xSemaphoreCreateMutex();
        if (!s_audio_hw_buf_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    return audio_hw_i2c_init();
}

esp_err_t audio_hw_configure(uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels)
{
    s_sample_rate_hz = sample_rate_hz;
    s_bits_per_sample = bits_per_sample;
    s_channels = channels;

    if (bits_per_sample != 16) {
        ESP_LOGE(TAG, "unsupported bits_per_sample=%u", (unsigned)bits_per_sample);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (channels == 0 || channels > 2) {
        ESP_LOGE(TAG, "unsupported channels=%u", (unsigned)channels);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (sample_rate_hz != 0) {
        int ok = board_audio_begin_external_playback((int)sample_rate_hz, (int)channels);
        s_external_playback = (ok != 0);
        int out_sr = board_audio_output_sample_rate();
        if (out_sr > 0 && (int)sample_rate_hz != out_sr) {
            ESP_LOGW(TAG, "wav sample_rate=%lu, codec sample_rate=%d", (unsigned long)sample_rate_hz, out_sr);
        }
    }
    return ESP_OK;
}

esp_err_t audio_hw_start(void)
{
    board_audio_enable_output(1);
    int vol = (int)((s_volume * 100U) / 33U);
    board_audio_set_output_volume_runtime(vol);
    app_audio_notify_external_output();
    return ESP_OK;
}

void audio_hw_stop(void)
{
    if (s_external_playback) {
        board_audio_end_external_playback();
        s_external_playback = false;
    }
}

size_t audio_hw_write(const uint8_t *data, size_t len, TickType_t timeout_ticks)
{
    (void)timeout_ticks;
    if (!data || len < sizeof(int16_t) || s_bits_per_sample != 16 || s_channels == 0) {
        return 0;
    }
    size_t samples = len / sizeof(int16_t);
    if (samples == 0) {
        return 0;
    }
    int written = board_audio_write_samples((const int16_t*)data, (int)samples);
    if (written <= 0) {
        return 0;
    }
    app_audio_notify_external_output();
    return (size_t)written * sizeof(int16_t);
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
    int vol = (int)((s_volume * 100U) / 33U);
    board_audio_set_output_volume_runtime(vol);
    return ESP_OK;
}

uint8_t audio_hw_get_volume(void)
{
    return s_volume;
}
