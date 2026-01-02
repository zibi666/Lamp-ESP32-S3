#include "audio_uploader.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_websocket_client.h"

// ---------------- 配置 ----------------
#define WEBSOCKET_URI           "ws://118.195.133.25:6060/esp32"
#define TAG                     "WS_UPLOADER"

// 队列深度：Opus 60ms帧，150帧约9秒。
#define SEND_QUEUE_LEN          150
#define WS_SEND_TIMEOUT_MS      1000

// ---------------- 状态管理 ----------------
static esp_websocket_client_handle_t ws_client = NULL;
static QueueHandle_t send_queue = NULL;
static TaskHandle_t send_task_handle = NULL;
static TickType_t last_reconnect_tick = 0;

// 使用 volatile bool 避免多线程锁竞争
static volatile bool is_connected = false;

static audio_uploader_binary_cb_t binary_cb = NULL;
static audio_uploader_text_cb_t text_cb = NULL;
static audio_uploader_connected_cb_t connected_cb = NULL;

typedef struct {
    size_t len;
    uint8_t* buf; 
} queue_item_t;

static void clear_queue(void);

// ---------------- WebSocket 事件处理 ----------------
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket Connected!");
            is_connected = true;
            if (connected_cb) {
                connected_cb();
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket Disconnected!");
            is_connected = false;
            clear_queue();
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == WS_TRANSPORT_OPCODES_BINARY) {
                if (binary_cb) binary_cb((const uint8_t*)data->data_ptr, data->data_len);
            } else if (data->op_code == WS_TRANSPORT_OPCODES_TEXT) {
                if (text_cb) text_cb((const char*)data->data_ptr, data->data_len);
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket Error!");
            break;
    }
}

// 新增：清空队列
// 当网络断开时，必须清空积压的旧数据，否则重连后你会听到几秒前的录音，产生巨大延迟
static void clear_queue() {
    queue_item_t item;
    int dropped_count = 0;
    while (xQueueReceive(send_queue, &item, 0) == pdTRUE) {
        if (item.buf) free(item.buf);
        dropped_count++;
    }
    if (dropped_count > 0) {
        ESP_LOGW(TAG, "网络中断，丢弃积压音频包: %d 个", dropped_count);
    }
}

// ---------------- 发送任务 (消费者) ----------------
static void audio_send_task(void* arg) {
    queue_item_t item;
    const TickType_t kReconnectInterval = pdMS_TO_TICKS(5000);
    
    while (true) {
        // 1. 等待数据
        if (xQueueReceive(send_queue, &item, portMAX_DELAY) == pdTRUE) {
            
            // 2. 检查连接状态
            // 增加 esp_websocket_client_is_connected 检查，确保底层连接完全就绪
            if (is_connected && ws_client != NULL && esp_websocket_client_is_connected(ws_client)) {
                
                int ret = esp_websocket_client_send_bin(ws_client, (const char*)item.buf, item.len, pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
                
                // 核心修复：发送失败时的熔断机制
                if (ret < 0) {
                    ESP_LOGE(TAG, "发送失败 (ret=%d)，暂停发送等待重连...", ret);
                    
                    // A. 强制标记断开，阻止新数据入队
                    is_connected = false; 
                    
                    // B. 释放当前包内存
                    if (item.buf) {
                        free(item.buf); 
                        item.buf = NULL; // 避免悬空指针
                    }

                    // C. 清空所有积压队列 (避免延迟和内存泄漏)
                    clear_queue();

                    // D. 强制休眠 2 秒！
                    // 这是解决刷屏的关键。给底层 Wi-Fi 协议栈时间去扫描和重连，
                    // 避免 CPU 被死循环占满导致 Wi-Fi 无法恢复。
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    continue; // 跳过本次循环剩余部分，进入下一轮等待
                }
            } else {
                // 触发周期性重连，避免 WebSocket 进入卡死状态
                TickType_t now = xTaskGetTickCount();
                if (ws_client != NULL && (now - last_reconnect_tick) > kReconnectInterval) {
                    last_reconnect_tick = now;
                    esp_websocket_client_stop(ws_client);
                    esp_websocket_client_start(ws_client);
                }

                // 如果未连接或连接未就绪，直接丢弃当前包
                // ESP_LOGW(TAG, "WebSocket not connected or not ready, dropping audio packet.");
                if (item.buf) {
                    free(item.buf);
                    item.buf = NULL;
                }
            }
            
            // 正常发送成功，释放内存
            if (item.buf) {
                free(item.buf);
                item.buf = NULL;
            }
        }
    }
}

// ---------------- 公共接口 ----------------

void audio_uploader_init(void) {
    if (send_queue == NULL) {
        send_queue = xQueueCreate(SEND_QUEUE_LEN, sizeof(queue_item_t));
    }

    esp_websocket_client_config_t config = {
        .uri = WEBSOCKET_URI,
        .reconnect_timeout_ms = 3000,   // 3秒重连
        .network_timeout_ms = 5000,     // 5秒超时
        .buffer_size = 4096,
        .disable_auto_reconnect = false,
        .keep_alive_enable = true,
        .keep_alive_idle = 4,           // 激进的保活检测：4秒无数据就检测
        .keep_alive_interval = 4,
        .keep_alive_count = 2
    };

    ws_client = esp_websocket_client_init(&config);
    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
    esp_websocket_client_start(ws_client);

    if (send_task_handle == NULL) {
        xTaskCreate(audio_send_task, "ws_send_task", 4096, NULL, 5, &send_task_handle);
    }
}

void audio_uploader_send_bytes(const uint8_t *data, size_t len) {
    // 1. 快速检查：断连时直接丢弃，不进队列
    if (!is_connected || data == NULL || len == 0) {
        return;
    }

    // 2. 队列满时丢弃最新的（保最新）
    if (uxQueueSpacesAvailable(send_queue) < 5) {
        // ESP_LOGW(TAG, "队列满，丢包"); // 注释掉减少日志干扰
        return;
    }

    uint8_t* buf_copy = (uint8_t*)malloc(len);
    if (!buf_copy) return;
    memcpy(buf_copy, data, len);

    queue_item_t item = { .len = len, .buf = buf_copy };

    if (xQueueSend(send_queue, &item, 0) != pdTRUE) {
        free(buf_copy);
    }
}

// 兼容接口：如果还想发 PCM，封装一下即可
void audio_uploader_send(const int16_t *data, int samples) {
    audio_uploader_send_bytes((const uint8_t*)data, samples * sizeof(int16_t));
}

bool audio_uploader_send_text(const char *data) {
    if (!is_connected || ws_client == NULL || data == NULL || data[0] == '\0') {
        return false;
    }
    if (!esp_websocket_client_is_connected(ws_client)) {
        return false;
    }
    int ret = esp_websocket_client_send_text(ws_client, data, strlen(data), pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
    return ret >= 0;
}

void audio_uploader_set_binary_cb(audio_uploader_binary_cb_t cb) {
    binary_cb = cb;
}

void audio_uploader_set_text_cb(audio_uploader_text_cb_t cb) {
    text_cb = cb;
}

void audio_uploader_set_connected_cb(audio_uploader_connected_cb_t cb) {
    connected_cb = cb;
}
