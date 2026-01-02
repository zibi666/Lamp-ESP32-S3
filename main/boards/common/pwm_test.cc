#include "pwm_test.h"
#include <driver/ledc.h>
#include <esp_err.h>
#include <algorithm>
#include <stdio.h>

#include "audio/transport/audio_uploader.h"
#include "audio/audio_codec.h"
#include "boards/common/board.h"

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO_20       (20) // IO20: 黄光
#define LEDC_OUTPUT_IO_19       (19) // IO19: 白光
#define LEDC_CHANNEL_0          LEDC_CHANNEL_0
#define LEDC_CHANNEL_1          LEDC_CHANNEL_1
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY          (5000)

static bool pwm_inited = false;
static bool fade_inited = false;
static const int kBrightnessMin = 0;
static const int kBrightnessMax = 100;
static const int kTemperatureMin = 0;
static const int kTemperatureMax = 100;

static int brightness_percent = 50;
static int temperature_percent = 50;
static int last_reported_brightness = -1;
static int last_reported_temperature = -1;
static uint32_t current_yellow_duty = 0;
static uint32_t current_white_duty = 0;

static int ClampPercent(int value, int min_val, int max_val) {
    return std::max(min_val, std::min(max_val, value));
}

static int ComputeColorTemperatureK(int temperature_pct) {
    const int kMinTempK = 2700;
    const int kMaxTempK = 6500;
    const int range = kMaxTempK - kMinTempK;
    return kMinTempK + (temperature_pct * range + 50) / 100;
}

static uint32_t AbsDiffU32(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static uint32_t ComputeFadeTimeMs(uint32_t delta, uint32_t max_duty) {
    const uint32_t kFadeMinMs = 120;
    const uint32_t kFadeMaxMs = 600;
    if (max_duty == 0) {
        return kFadeMinMs;
    }
    return kFadeMinMs + (delta * (kFadeMaxMs - kFadeMinMs)) / max_duty;
}

static void SendLampStatus(bool force) {
    char payload[32];
    int color_temp_k = ComputeColorTemperatureK(temperature_percent);
    if (!force &&
        brightness_percent == last_reported_brightness &&
        color_temp_k == last_reported_temperature) {
        return;
    }
    int len = snprintf(payload, sizeof(payload), "(%d,%d)", brightness_percent, color_temp_k);
    if (len > 0 && len < (int)sizeof(payload)) {
        if (audio_uploader_send_text(payload)) {
            last_reported_brightness = brightness_percent;
            last_reported_temperature = color_temp_k;
        }
    }
}

static void OnWsConnected() {
    SendLampStatus(true);
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec) {
        return;
    }
    char payload[24];
    int len = snprintf(payload, sizeof(payload), "(volume,%d)", codec->output_volume());
    if (len > 0 && len < (int)sizeof(payload)) {
        audio_uploader_send_text(payload);
    }
}

static void ApplyLampPwm() {
    if (!pwm_inited) {
        return;
    }

    const uint32_t max_duty = (1U << LEDC_DUTY_RES) - 1;
    uint32_t base_duty = (max_duty * (uint32_t)brightness_percent) / 100;
    uint32_t yellow_percent = (uint32_t)(100 - temperature_percent);
    uint32_t yellow_duty = (base_duty * yellow_percent) / 100;
    uint32_t white_duty = base_duty - yellow_duty;

    if (!fade_inited) {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, yellow_duty);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_1, white_duty);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_1);
        current_yellow_duty = yellow_duty;
        current_white_duty = white_duty;
        SendLampStatus(false);
        return;
    }

    uint32_t delta_yellow = AbsDiffU32(current_yellow_duty, yellow_duty);
    uint32_t delta_white = AbsDiffU32(current_white_duty, white_duty);
    uint32_t max_delta = std::max(delta_yellow, delta_white);
    uint32_t fade_ms = ComputeFadeTimeMs(max_delta, max_duty);

    ESP_ERROR_CHECK(ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL_0, yellow_duty, fade_ms));
    ESP_ERROR_CHECK(ledc_set_fade_with_time(LEDC_MODE, LEDC_CHANNEL_1, white_duty, fade_ms));
    ESP_ERROR_CHECK(ledc_fade_start(LEDC_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT));
    ESP_ERROR_CHECK(ledc_fade_start(LEDC_MODE, LEDC_CHANNEL_1, LEDC_FADE_NO_WAIT));
    current_yellow_duty = yellow_duty;
    current_white_duty = white_duty;

    SendLampStatus(false);
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

    ESP_ERROR_CHECK(ledc_fade_func_install(0));
    fade_inited = true;

    audio_uploader_set_connected_cb(OnWsConnected);
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
