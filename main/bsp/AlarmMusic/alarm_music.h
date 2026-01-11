#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "http_request.h"

/**
 * @brief 初始化闹钟音乐模块
 * @return ESP_OK 成功，否则失败
 */
esp_err_t alarm_music_init(void);

/**
 * @brief 启动闹钟音乐任务
 * @return ESP_OK 成功，否则失败
 */
esp_err_t alarm_music_start(void);

/**
 * @brief 停止闹钟音乐任务
 */
void alarm_music_stop(void);

/**
 * @brief 闹钟触发时的回调函数
 * @note 该函数由闹钟服务调用，触发音乐播放
 */
void alarm_music_ring_callback(const alarm_info_t *alarm, void *ctx);
