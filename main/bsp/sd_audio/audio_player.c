/**
 * @file audio_player.c
 * @brief SD 卡音频播放器实现
 * 
 * 本模块从 SD 卡 MUSIC 目录读取 WAV 文件并循环播放。
 * 音频输出通过 main 项目的音频系统（ES8388）。
 */

#include "audio_player.h"
#include "audio_sdcard.h"
#include "audio_hw.h"
#include "xl9555_keys.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdbool.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AUDIO_TASK_STACK   (8 * 1024)
#define AUDIO_TASK_PRIO    5
#define AUDIO_IO_BUF_SIZE  4096
#define MAX_TRACKS         64
#define MAX_NAME_LEN       128

static const char *TAG = "audio_player";

typedef struct {
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t channels;
    uint32_t data_offset;
    uint32_t data_size;
} audio_wav_info_t;

static TaskHandle_t s_audio_task = NULL;
static bool s_inited = false;
static bool s_stop = false;
static size_t s_track_index = 0;

static bool is_wav_file(const char *name)
{
    size_t len = strlen(name);
    if (len < 4) {
        return false;
    }
    const char *ext = name + len - 4;
    return (strcasecmp(ext, ".wav") == 0);
}

static esp_err_t wav_parse(FIL *file, audio_wav_info_t *info)
{
    UINT br = 0;
    uint8_t header[12];

    FRESULT fr = f_read(file, header, sizeof(header), &br);
    if (fr != FR_OK || br != sizeof(header)) {
        return ESP_FAIL;
    }

    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        return ESP_FAIL;
    }

    bool fmt_found = false;
    bool data_found = false;
    uint16_t audio_format = 0;

    while (!data_found) {
        struct __attribute__((packed)) {
            char id[4];
            uint32_t size;
        } chunk;

        fr = f_read(file, &chunk, sizeof(chunk), &br);
        if (fr != FR_OK || br != sizeof(chunk)) {
            return ESP_FAIL;
        }

        uint32_t next_pos = f_tell(file) + chunk.size;

        if (memcmp(chunk.id, "fmt ", 4) == 0) {
            struct __attribute__((packed)) {
                uint16_t format;
                uint16_t channels;
                uint32_t sample_rate;
                uint32_t byte_rate;
                uint16_t block_align;
                uint16_t bits_per_sample;
            } fmt_hdr;

            fr = f_read(file, &fmt_hdr, sizeof(fmt_hdr), &br);
            if (fr != FR_OK || br != sizeof(fmt_hdr)) {
                return ESP_FAIL;
            }

            audio_format = fmt_hdr.format;
            info->channels = fmt_hdr.channels;
            info->sample_rate = fmt_hdr.sample_rate;
            info->bits_per_sample = fmt_hdr.bits_per_sample;
            fmt_found = true;
        } else if (memcmp(chunk.id, "data", 4) == 0) {
            info->data_offset = f_tell(file);
            info->data_size = chunk.size;
            data_found = true;
        }

        if (!data_found) {
            if (chunk.size % 2) {
                next_pos += 1;
            }
            f_lseek(file, next_pos);
        }
    }

    if (!fmt_found || audio_format != 1) {
        return ESP_FAIL;
    }

    f_lseek(file, info->data_offset);
    return ESP_OK;
}

static esp_err_t play_single(const char *path)
{
    FIL file;
    FRESULT fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        ESP_LOGW(TAG, "open %s failed: %d", path, fr);
        return ESP_FAIL;
    }

    audio_wav_info_t info = {0};
    if (wav_parse(&file, &info) != ESP_OK) {
        ESP_LOGW(TAG, "skip non-wav: %s", path);
        f_close(&file);
        return ESP_FAIL;
    }

    /* 配置音频参数（兼容性接口） */
    audio_hw_configure(info.sample_rate, info.bits_per_sample, info.channels);
    audio_hw_start();

    uint8_t *buf = (uint8_t *)heap_caps_malloc(AUDIO_IO_BUF_SIZE, MALLOC_CAP_DEFAULT);
    if (!buf) {
        ESP_LOGE(TAG, "malloc audio buffer failed");
        f_close(&file);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "play %s (%lu Hz, %u bit, %u ch)", path, 
             (unsigned long)info.sample_rate, info.bits_per_sample, info.channels);

    while (!s_stop) {
        UINT br = 0;
        fr = f_read(&file, buf, AUDIO_IO_BUF_SIZE, &br);
        if (fr != FR_OK || br == 0) {
            break;
        }

        audio_hw_write(buf, br, pdMS_TO_TICKS(500));
    }

    free(buf);
    audio_hw_stop();
    f_close(&file);
    return ESP_OK;
}

static void audio_task(void *args)
{
    static char tracks[MAX_TRACKS][MAX_NAME_LEN];
    size_t track_count = 0;

    while (!s_stop) {
        if (!audio_sdcard_is_mounted()) {
            if (audio_sdcard_mount() != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
        }

        FF_DIR dir;
        FRESULT fr = f_opendir(&dir, AUDIO_MUSIC_DIR_FAT);
        if (fr != FR_OK) {
            ESP_LOGW(TAG, "dir %s missing, waiting for files", AUDIO_MUSIC_DIR_FAT);
            vTaskDelay(pdMS_TO_TICKS(1500));
            continue;
        }

        track_count = 0;
        FILINFO finfo;
        while (!s_stop && track_count < MAX_TRACKS) {
            fr = f_readdir(&dir, &finfo);
            if (fr != FR_OK || finfo.fname[0] == 0) {
                break;
            }

            if (!is_wav_file(finfo.fname)) {
                continue;
            }

            strlcpy(tracks[track_count], finfo.fname, MAX_NAME_LEN);
            track_count++;
        }

        f_closedir(&dir);

        if (track_count == 0) {
            ESP_LOGI(TAG, "no wav files in %s", AUDIO_MUSIC_DIR);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (s_track_index >= track_count) {
            s_track_index = 0;
        }

        while (!s_stop) {
            char full_path[260];
            snprintf(full_path, sizeof(full_path), "%s/%s", AUDIO_MUSIC_DIR_FAT, tracks[s_track_index]);
            play_single(full_path);

            if (s_stop) {
                break;
            }

            s_track_index = (s_track_index + 1) % track_count;
        }
    }

    s_audio_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t audio_player_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(audio_hw_init(), TAG, "hw init fail");
    ESP_RETURN_ON_ERROR(xl9555_keys_init(), TAG, "keys init fail");
    ESP_RETURN_ON_ERROR(audio_sdcard_mount(), TAG, "sd mount fail");
    s_inited = true;
    return ESP_OK;
}

esp_err_t audio_player_start(void)
{
    if (!s_inited) {
        ESP_RETURN_ON_ERROR(audio_player_init(), TAG, "init before start");
    }

    if (s_audio_task) {
        return ESP_OK;
    }

    s_stop = false;
    BaseType_t res = xTaskCreate(audio_task, "audio_player", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIO, &s_audio_task);
    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}

void audio_player_stop(void)
{
    if (!s_audio_task) {
        return;
    }

    s_stop = true;
    while (s_audio_task) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool audio_player_is_running(void)
{
    return s_audio_task != NULL;
}
