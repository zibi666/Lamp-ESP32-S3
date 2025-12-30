#include "pwm_test.h"
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#define PWM_TAG "PWM_TEST"

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO_20       (20) // Define the output GPIO
#define LEDC_OUTPUT_IO_19       (19) // Define the output GPIO
#define LEDC_CHANNEL_0          LEDC_CHANNEL_0
#define LEDC_CHANNEL_1          LEDC_CHANNEL_1
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_FREQUENCY          (5000) // Frequency in Hertz. Set frequency at 5 kHz

static void pwm_test_task(void *arg) {
    ESP_LOGI(PWM_TAG, "PWM Task Started");

    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel[2] = {
        {
            .gpio_num       = LEDC_OUTPUT_IO_20,
            .speed_mode     = LEDC_MODE,
            .channel        = LEDC_CHANNEL_0,
            .intr_type      = LEDC_INTR_DISABLE,
            .timer_sel      = LEDC_TIMER,
            .duty           = 0, // Set duty to 0%
            .hpoint         = 0
        },
        {
            .gpio_num       = LEDC_OUTPUT_IO_19,
            .speed_mode     = LEDC_MODE,
            .channel        = LEDC_CHANNEL_1,
            .intr_type      = LEDC_INTR_DISABLE,
            .timer_sel      = LEDC_TIMER,
            .duty           = 0, // Set duty to 0%
            .hpoint         = 0
        },
    };

    for (int ch = 0; ch < 2; ch++) {
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel[ch]));
    }

    // 13-bit resolution: 2^13 - 1 = 8191
    // 20% = 1638
    // 40% = 3276
    // 60% = 4914
    // 80% = 6552
    uint32_t duties[] = {1638, 3276, 4914, 6552};
    int duty_idx = 0;

    while (1) {
        uint32_t current_duty = duties[duty_idx];
        ESP_LOGI(PWM_TAG, "Setting duty cycle to %d%% (Value: %lu)", (duty_idx + 1) * 20, current_duty);

        for (int ch = 0; ch < 2; ch++) {
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, ledc_channel[ch].channel, current_duty));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, ledc_channel[ch].channel));
        }

        duty_idx++;
        if (duty_idx >= 4) {
            duty_idx = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // Change every 2 seconds
    }
}

void StartPwmTest() {
    xTaskCreate(pwm_test_task, "pwm_test_task", 4096, NULL, 5, NULL);
}
