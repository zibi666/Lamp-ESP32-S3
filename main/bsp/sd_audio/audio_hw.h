#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"

#define AUDIO_I2C_PORT        I2C_NUM_0
#define AUDIO_I2C_SCL         GPIO_NUM_42
#define AUDIO_I2C_SDA         GPIO_NUM_41
#define AUDIO_I2C_CLK_HZ      400000

#define AUDIO_I2S_PORT        I2S_NUM_0
#define AUDIO_I2S_BCLK        GPIO_NUM_46
#define AUDIO_I2S_LRCK        GPIO_NUM_9
#define AUDIO_I2S_DOUT        GPIO_NUM_10
#define AUDIO_I2S_DIN         GPIO_NUM_14
#define AUDIO_I2S_MCLK        GPIO_NUM_3
#define AUDIO_I2S_MCLK_MULT   256

#define AUDIO_CODEC_ADDR      0x10

/**
 * @brief 初始化共享 I2C 总线
 * @return ESP_OK 成功
 */
esp_err_t audio_hw_i2c_init(void);

/**
 * @brief 获取 I2C 总线句柄
 * @return I2C master bus handle，未初始化时返回 NULL
 */
i2c_master_bus_handle_t audio_hw_get_i2c_bus(void);

esp_err_t audio_hw_init(void);
esp_err_t audio_hw_configure(uint32_t sample_rate_hz, uint8_t bits_per_sample, uint8_t channels);
esp_err_t audio_hw_start(void);
void audio_hw_stop(void);
size_t audio_hw_write(const uint8_t *data, size_t len, TickType_t timeout_ticks);
void audio_hw_deinit(void);
esp_err_t audio_hw_set_volume(uint8_t volume); /* 0-33 (ES8388 scale) */
uint8_t audio_hw_get_volume(void);
i2c_master_bus_handle_t audio_hw_get_i2c_bus(void);
