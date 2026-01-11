#pragma once

#include <stdint.h>
#include "esp_err.h"

/* Logical key codes mapped to XL9555 pins:
 * KEY0 -> IO1_7
 * KEY1 -> IO1_6
 * KEY2 -> IO1_5
 * KEY3 -> IO1_4
 */
enum xl9555_key_code
{
    XL9555_KEY_NONE = 0,
    XL9555_KEY0 = 1,
    XL9555_KEY1 = 2,
    XL9555_KEY2 = 3,
    XL9555_KEY3 = 4,
};

#define XL9555_BEEP_BIT   3

esp_err_t xl9555_beep_on(void);
esp_err_t xl9555_beep_off(void);
esp_err_t xl9555_beep_init(void);

esp_err_t xl9555_keys_init(void);
/* mode: 0 no-repeat, 1 repeat */
uint8_t xl9555_keys_scan(uint8_t mode);
