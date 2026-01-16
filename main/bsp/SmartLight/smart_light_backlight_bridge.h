#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 设置背光亮度（C接口包装）
 * 
 * @param brightness 亮度值 (0-100)
 */
void smart_light_set_backlight(uint8_t brightness);

#ifdef __cplusplus
}
#endif
