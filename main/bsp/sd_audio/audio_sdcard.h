#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define AUDIO_SD_MOUNT_POINT   "/sdcard"
#define AUDIO_MUSIC_DIR        AUDIO_SD_MOUNT_POINT "/MUSIC"
#define AUDIO_MUSIC_DIR_FAT    "0:/MUSIC"

esp_err_t audio_sdcard_mount(void);
void audio_sdcard_unmount(void);
bool audio_sdcard_is_mounted(void);
