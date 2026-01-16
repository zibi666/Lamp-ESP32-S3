#pragma once

#include "esp_err.h"
#include <stdbool.h>

#define AUDIO_SD_MOUNT_POINT "/sdcard"
#define AUDIO_MUSIC_DIR AUDIO_SD_MOUNT_POINT "/MUSIC"
#define AUDIO_MUSIC_DIR_FAT "0:/MUSIC"

/* 唤醒音乐和睡眠音乐路径 */
#define AUDIO_WAKE_MUSIC_DIR AUDIO_SD_MOUNT_POINT "/MUSIC/wake"
#define AUDIO_WAKE_MUSIC_DIR_FAT "0:/MUSIC/wake"
#define AUDIO_SLEEP_MUSIC_DIR AUDIO_SD_MOUNT_POINT "/MUSIC/sleep"
#define AUDIO_SLEEP_MUSIC_DIR_FAT "0:/MUSIC/sleep"

/* 唤醒音乐总数 */
#define AUDIO_WAKE_MUSIC_COUNT 177

/* 唤醒音乐和睡眠音乐路径 */
#define AUDIO_WAKE_MUSIC_DIR     AUDIO_SD_MOUNT_POINT "/MUSIC/wake"
#define AUDIO_WAKE_MUSIC_DIR_FAT "0:/MUSIC/wake"
#define AUDIO_SLEEP_MUSIC_DIR     AUDIO_SD_MOUNT_POINT "/MUSIC/sleep"
#define AUDIO_SLEEP_MUSIC_DIR_FAT "0:/MUSIC/sleep"

/* 唤醒音乐总数 */
#define AUDIO_WAKE_MUSIC_COUNT 177

esp_err_t audio_sdcard_mount(void);
void audio_sdcard_unmount(void);
bool audio_sdcard_is_mounted(void);
