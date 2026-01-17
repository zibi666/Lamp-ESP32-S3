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
extern void app_audio_set_external_playback(int active);  // 设置外部播放状态
extern int board_audio_begin_external_playback(int sample_rate, int channels);
extern void board_audio_end_external_playback(void);
extern int board_get_saved_volume(void);  // 获取智能体保存的音量
#ifdef __cplusplus
}
#endif

static const char *TAG = "audio_hw";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static uint8_t s_volume = 60;           // 默认音量 0-100
static uint8_t s_runtime_volume = 60;   // 运行时音量（闹钟/助眠使用，独立于智能体）
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
    ESP_LOGI(TAG, "audio_hw_configure: sample_rate=%lu, bits=%u, channels=%u",
             (unsigned long)sample_rate_hz, bits_per_sample, channels);
    
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
        // 先设置外部播放标志，防止 AudioService 在 I2S 重配置期间尝试重新启用输入
        app_audio_set_external_playback(1);
        
        int ok = board_audio_begin_external_playback((int)sample_rate_hz, (int)channels);
        s_external_playback = (ok != 0);
        ESP_LOGI(TAG, "audio_hw_configure: begin_external_playback returned %d", ok);
        
        if (!ok) {
            // 如果失败，恢复标志
            app_audio_set_external_playback(0);
        }
        
        int out_sr = board_audio_output_sample_rate();
        if (out_sr > 0 && (int)sample_rate_hz != out_sr) {
            ESP_LOGW(TAG, "wav sample_rate=%lu, codec sample_rate=%d", (unsigned long)sample_rate_hz, out_sr);
        }
    }
    return ESP_OK;
}

esp_err_t audio_hw_start(void)
{
    ESP_LOGI(TAG, "audio_hw_start: sample_rate=%lu, bits=%u, channels=%u, external_playback=%d",
             (unsigned long)s_sample_rate_hz, s_bits_per_sample, s_channels, s_external_playback);
    
    // 通知 AudioService 外部播放开始，防止超时禁用输出
    app_audio_set_external_playback(1);
    
    board_audio_enable_output(1);
    board_audio_set_output_volume_runtime((int)s_volume);
    app_audio_notify_external_output();
    ESP_LOGI(TAG, "audio_hw_start: output enabled, volume=%u", s_volume);
    return ESP_OK;
}

void audio_hw_stop(void)
{
    // 通知 AudioService 外部播放结束
    app_audio_set_external_playback(0);
    
    if (s_external_playback) {
        board_audio_end_external_playback();
        s_external_playback = false;
    }
}

size_t audio_hw_write(const uint8_t *data, size_t len, TickType_t timeout_ticks)
{
    (void)timeout_ticks;
    if (!data || len < sizeof(int16_t) || s_bits_per_sample != 16 || s_channels == 0) {
        ESP_LOGW(TAG, "audio_hw_write: invalid params: data=%p, len=%zu, bits=%u, channels=%u",
                 data, len, s_bits_per_sample, s_channels);
        return 0;
    }
    size_t samples = len / sizeof(int16_t);
    if (samples == 0) {
        return 0;
    }
    
    static int write_count = 0;
    if (write_count == 0) {
        ESP_LOGI(TAG, "audio_hw_write: first write, samples=%zu", samples);
    }
    write_count++;
    
    int written = board_audio_write_samples((const int16_t*)data, (int)samples);
    if (written <= 0) {
        if (write_count % 100 == 0) {
            ESP_LOGW(TAG, "audio_hw_write: write_samples returned %d", written);
        }
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
    if (volume > 100) {
        volume = 100;
    }
    s_volume = volume;
    board_audio_set_output_volume_runtime((int)s_volume);
    return ESP_OK;
}

uint8_t audio_hw_get_volume(void)
{
    return s_volume;
}

/**
 * @brief 设置运行时音量（闹钟/助眠使用），不影响智能体保存的音量
 */
void audio_hw_set_volume_runtime(uint8_t volume)
{
    if (volume > 100) {
        volume = 100;
    }
    s_runtime_volume = volume;
    board_audio_set_output_volume_runtime((int)s_runtime_volume);
}

/**
 * @brief 恢复智能体保存的音量
 */
void audio_hw_restore_volume(void)
{
    int saved_vol = board_get_saved_volume();
    if (saved_vol < 0) {
        saved_vol = 70;  // 默认智能体音量
    }
    s_volume = (uint8_t)saved_vol;
    board_audio_set_output_volume_runtime((int)s_volume);
    ESP_LOGI(TAG, "Volume restored to %d", s_volume);
}
