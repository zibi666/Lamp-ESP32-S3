#ifndef AUDIO_UPLOADER_H
#define AUDIO_UPLOADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// 初始化 WebSocket 和发送任务
void audio_uploader_init(void);

// 发送二进制数据 (Opus包或PCM)
// 内部会自动处理内存拷贝和队列管理，网络断开时会自动丢弃
void audio_uploader_send_bytes(const uint8_t *data, size_t len);

// 发送 PCM 数据 (兼容旧接口)
void audio_uploader_send(const int16_t *data, int samples);

// 回调函数定义
typedef void (*audio_uploader_binary_cb_t)(const uint8_t *data, size_t len);
typedef void (*audio_uploader_text_cb_t)(const char *data, size_t len);

void audio_uploader_set_binary_cb(audio_uploader_binary_cb_t cb);
void audio_uploader_set_text_cb(audio_uploader_text_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif