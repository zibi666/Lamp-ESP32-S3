/**
 * @file audio_player.c
 * @brief SD 卡音频播放器实现
 * 
 * 本模块从 SD 卡 MUSIC 目录读取 WAV 文件并随机播放。
 * 支持唤醒音乐（MUSIC/wake）和睡眠音乐（MUSIC/sleep）两种模式。
 * 音频输出通过 main 项目的音频系统（ES8388）。
 */

#include "audio_player.h"
#include "audio_sdcard.h"
#include "audio_hw.h"
#include "xl9555_keys.h"
#include "app_controller.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AUDIO_TASK_STACK   (8 * 1024)
#define AUDIO_TASK_PRIO    5
#define AUDIO_IO_BUF_SIZE  4096
#define MAX_TRACKS         200
#define MAX_NAME_LEN       128

/* 睡眠检测自动停止相关配置 */
#define SLEEP_DETECT_INTERVAL_MS   500    // 睡眠检测间隔
#define SLEEP_VOL_DECREASE_MS      20000  // 每20秒降低一次音量
#define SLEEP_VOL_DECREASE_STEP    3      // 每次降低的音量步进
#define SLEEP_DEFAULT_VOLUME       20     // 睡眠音乐默认音量

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
static bool s_skip_next = false;  // 跳到下一首标志
static bool s_skip_prev = false;  // 跳到上一首标志
static size_t s_track_index = 0;
static size_t s_track_count = 0;  // 当前播放列表的歌曲总数
static audio_play_mode_t s_play_mode = AUDIO_MODE_WAKE;

/**
 * @brief 随机数生成函数 - 使用线性同余生成器
 * @param seed 随机数种子（会被修改）
 * @return 随机数
 */
static uint32_t linear_congruential_random(uint32_t *seed)
{
    // 使用标准的线性同余生成器参数
    // 参数来自 POSIX.1-2001 标准
    *seed = (*seed * 1103515245 + 12345) & 0x7fffffff;
    return *seed;
}

/**
 * @brief Fisher-Yates 洗牌算法实现
 * 用于随机排列音乐列表，确保每次播放都是随机顺序
 * 
 * @param tracks 音轨数组
 * @param track_count 音轨总数
 */
static void shuffle_tracks(char (*tracks)[MAX_NAME_LEN], size_t track_count)
{
    if (track_count < 2) {
        return;
    }

    // 使用 esp_random() 生成真随机数
    uint32_t seed = esp_random();
    
    for (size_t i = track_count - 1; i > 0; i--) {
        // 生成 0 到 i 之间的随机数
        size_t j = linear_congruential_random(&seed) % (i + 1);
        
        // 交换 tracks[i] 和 tracks[j]
        char temp[MAX_NAME_LEN];
        strlcpy(temp, tracks[i], MAX_NAME_LEN);
        strlcpy(tracks[i], tracks[j], MAX_NAME_LEN);
        strlcpy(tracks[j], temp, MAX_NAME_LEN);
    }
}

/**
 * @brief 获取播放目录路径
 * @param mode 播放模式
 * @return FAT 格式的目录路径
 */
static const char* get_music_dir(audio_play_mode_t mode)
{
    if (mode == AUDIO_MODE_SLEEP) {
        return AUDIO_SLEEP_MUSIC_DIR_FAT;
    }
    return AUDIO_WAKE_MUSIC_DIR_FAT;
}

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

    while (!s_stop && !s_skip_next && !s_skip_prev) {
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
    
    /* 睡眠检测相关变量 */
    TickType_t sleep_detect_tick = 0;      // 开始检测到非WAKE的时间
    TickType_t last_vol_decrease_tick = 0; // 上次降低音量的时间
    uint8_t current_volume = SLEEP_DEFAULT_VOLUME;
    bool is_fading_out = false;            // 是否正在渐弱

    while (!s_stop) {
        if (!audio_sdcard_is_mounted()) {
            if (audio_sdcard_mount() != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
        }

        const char *music_dir = get_music_dir(s_play_mode);
        FF_DIR dir;
        FRESULT fr = f_opendir(&dir, music_dir);
        if (fr != FR_OK) {
            ESP_LOGW(TAG, "dir %s missing, waiting for files", music_dir);
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
            ESP_LOGI(TAG, "no wav files in %s", music_dir);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(TAG, "Found %u music files in %s, shuffling...", (unsigned int)track_count, music_dir);
        
        // 对音乐列表进行洗牌（随机排列）
        shuffle_tracks(tracks, track_count);
        
        ESP_LOGI(TAG, "Starting random playback (mode=%d)", s_play_mode);

        // 睡眠音乐模式：初始化音量和检测状态
        if (s_play_mode == AUDIO_MODE_SLEEP) {
            current_volume = SLEEP_DEFAULT_VOLUME;
            audio_hw_set_volume(current_volume);
            sleep_detect_tick = 0;
            last_vol_decrease_tick = 0;
            is_fading_out = false;
            ESP_LOGI(TAG, "Sleep mode: volume set to %u, sleep detection enabled", current_volume);
        }

        // 按随机顺序播放所有音乐
        s_track_index = 0;
        s_track_count = track_count;
        while (!s_stop && s_track_index < track_count) {
            char full_path[260];
            snprintf(full_path, sizeof(full_path), "%s/%s", music_dir, tracks[s_track_index]);
            play_single(full_path);

            if (s_stop) {
                break;
            }

            // 睡眠音乐模式：检测睡眠状态并渐进式降低音量
            if (s_play_mode == AUDIO_MODE_SLEEP) {
                TickType_t now = xTaskGetTickCount();
                sleep_stage_t stage = app_controller_get_current_sleep_stage();
                
                if (stage != SLEEP_STAGE_WAKE) {
                    // 非清醒状态，开始或继续渐弱
                    if (!is_fading_out) {
                        is_fading_out = true;
                        sleep_detect_tick = now;
                        last_vol_decrease_tick = now;
                        ESP_LOGI(TAG, "Sleep detected (stage=%d), starting volume fade out", stage);
                    }
                    
                    // 每20秒降低一次音量
                    if ((now - last_vol_decrease_tick) * portTICK_PERIOD_MS >= SLEEP_VOL_DECREASE_MS) {
                        if (current_volume > SLEEP_VOL_DECREASE_STEP) {
                            current_volume -= SLEEP_VOL_DECREASE_STEP;
                        } else {
                            current_volume = 0;
                        }
                        audio_hw_set_volume(current_volume);
                        last_vol_decrease_tick = now;
                        ESP_LOGI(TAG, "Sleep fade: volume decreased to %u", current_volume);
                        
                        // 音量降至0，停止播放
                        if (current_volume == 0) {
                            ESP_LOGI(TAG, "Sleep fade complete, stopping music");
                            s_stop = true;
                            break;
                        }
                    }
                } else {
                    // 清醒状态，重置渐弱状态并恢复音量
                    if (is_fading_out) {
                        is_fading_out = false;
                        current_volume = SLEEP_DEFAULT_VOLUME;
                        audio_hw_set_volume(current_volume);
                        ESP_LOGI(TAG, "Wake detected, volume restored to %u", current_volume);
                    }
                }
            }

            // 处理上一首/下一首跳转
            if (s_skip_prev) {
                s_skip_prev = false;
                if (s_track_index > 0) {
                    s_track_index--;  // 回到上一首
                }
                ESP_LOGI(TAG, "Skip to previous: Track %u/%u", (unsigned int)(s_track_index + 1), (unsigned int)track_count);
            } else if (s_skip_next) {
                s_skip_next = false;
                s_track_index++;  // 继续到下一首
                ESP_LOGI(TAG, "Skip to next: Track %u/%u", (unsigned int)(s_track_index + 1), (unsigned int)track_count);
            } else {
                s_track_index++;  // 正常播放完毕，下一首
                ESP_LOGI(TAG, "Track %u/%u", (unsigned int)s_track_index, (unsigned int)track_count);
            }
        }
        
        // 如果播放完所有音乐，重新开始随机播放
        if (!s_stop) {
            ESP_LOGI(TAG, "Finished all tracks, reshuffling...");
            vTaskDelay(pdMS_TO_TICKS(500));
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

esp_err_t audio_player_set_mode(audio_play_mode_t mode)
{
    if (mode != AUDIO_MODE_WAKE && mode != AUDIO_MODE_SLEEP) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_play_mode = mode;
    ESP_LOGI(TAG, "Audio mode set to %d", mode);
    return ESP_OK;
}

audio_play_mode_t audio_player_get_mode(void)
{
    return s_play_mode;
}

void audio_player_next(void)
{
    if (s_audio_task) {
        s_skip_next = true;
        ESP_LOGI(TAG, "Next track requested");
    }
}

void audio_player_prev(void)
{
    if (s_audio_task) {
        s_skip_prev = true;
        ESP_LOGI(TAG, "Previous track requested");
    }
}

size_t audio_player_get_current_track(void)
{
    return s_track_index + 1;  // 返回1-based索引
}

size_t audio_player_get_track_count(void)
{
    return s_track_count;
}

void audio_player_set_volume(uint8_t volume)
{
    if (volume > 100) {
        volume = 100;
    }
    audio_hw_set_volume(volume);
    ESP_LOGI(TAG, "Volume set to %u", volume);
}

uint8_t audio_player_get_volume(void)
{
    return audio_hw_get_volume();
}
