
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* 音乐播放模式 */
typedef enum {
    AUDIO_MODE_WAKE,  /* 唤醒音乐 - 随机播放 */
    AUDIO_MODE_SLEEP, /* 睡眠助眠音乐 - 随机播放 */
} audio_play_mode_t;

esp_err_t audio_player_init(void);
esp_err_t audio_player_start(void);
void audio_player_stop(void);
bool audio_player_is_running(void);

/* 设置播放模式 */
esp_err_t audio_player_set_mode(audio_play_mode_t mode);

/* 获取当前播放模式 */
audio_play_mode_t audio_player_get_mode(void);

/* 播放控制 - 上一首/下一首 */
void audio_player_next(void);
void audio_player_prev(void);

/* 获取当前播放信息 */
size_t audio_player_get_current_track(void);  // 当前曲目索引 (1-based)
size_t audio_player_get_track_count(void);    // 总曲目数

/* 音量控制 */
void audio_player_set_volume(uint8_t volume);  // 设置音量 (0-100)
uint8_t audio_player_get_volume(void);         // 获取当前音量
