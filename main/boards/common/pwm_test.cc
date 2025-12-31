#include "pwm_test.h"
#include <driver/ledc.h>
#include <esp_err.h>
#include <algorithm>

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO_20       (20) // IO20: 黄光
#define LEDC_OUTPUT_IO_19       (19) // IO19: 白光
#define LEDC_CHANNEL_0          LEDC_CHANNEL_0
#define LEDC_CHANNEL_1          LEDC_CHANNEL_1
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY          (5000)

static bool pwm_inited = false;
static const int kBrightnessMin = 0;
static const int kBrightnessMax = 100;
static const int kTemperatureMin = 0;
static const int kTemperatureMax = 100;

static int brightness_percent = 50;
static int temperature_percent = 50;

static int ClampPercent(int value, int min_val, int max_val) {
    return std::max(min_val, std::min(max_val, value));
}

static void ApplyLampPwm() {
    if (!pwm_inited) {
        return;
    }

    const uint32_t max_duty = (1U << LEDC_DUTY_RES) - 1;
    uint32_t base_duty = (max_duty * (uint32_t)brightness_percent) / 100;
    uint32_t yellow_duty = (base_duty * (uint32_t)temperature_percent) / 100;
    uint32_t white_duty = base_duty - yellow_duty;

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, yellow_duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_1, white_duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_1);
}

static void InitLampPwm() {
    if (pwm_inited) {
        return;
    }

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel[2] = {
        {
            .gpio_num       = LEDC_OUTPUT_IO_20,
            .speed_mode     = LEDC_MODE,
            .channel        = LEDC_CHANNEL_0,
            .intr_type      = LEDC_INTR_DISABLE,
            .timer_sel      = LEDC_TIMER,
            .duty           = 0,
            .hpoint         = 0
        },
        {
            .gpio_num       = LEDC_OUTPUT_IO_19,
            .speed_mode     = LEDC_MODE,
            .channel        = LEDC_CHANNEL_1,
            .intr_type      = LEDC_INTR_DISABLE,
            .timer_sel      = LEDC_TIMER,
            .duty           = 0,
            .hpoint         = 0
        },
    };

    for (int ch = 0; ch < 2; ch++) {
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel[ch]));
    }

    pwm_inited = true;
    ApplyLampPwm();
}

void StartPwmTest() {
    InitLampPwm();
}

void LampAdjustBrightness(int delta_percent) {
    InitLampPwm();
    brightness_percent = ClampPercent(brightness_percent + delta_percent, kBrightnessMin, kBrightnessMax);
    ApplyLampPwm();
}

void LampAdjustTemperature(int delta_percent) {
    InitLampPwm();
    temperature_percent = ClampPercent(temperature_percent + delta_percent, kTemperatureMin, kTemperatureMax);
    ApplyLampPwm();
}
