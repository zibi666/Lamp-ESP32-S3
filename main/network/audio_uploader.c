#include "audio_uploader.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_websocket_client.h"

// ---------------- 配置 ----------------
#define WEBSOCKET_URI           "ws://118.195.133.25:6060/esp32"
#define TAG                     "WS_UPLOADER"

#define SEND_QUEUE_LEN          150        // Opus 60ms帧，约9秒缓冲
#define WS_SEND_TIMEOUT_MS      1000

// ---------------- 状态管理 ----------------
static esp_websocket_client_handle_t ws_client = NULL;
static QueueHandle_t send_queue = NULL;
static TaskHandle_t send_task_handle = NULL;
static SemaphoreHandle_t ws_mutex = NULL;

static volatile bool is_connected = false;

static audio_uploader_binary_cb_t binary_cb = NULL;
static audio_uploader_text_cb_t text_cb = NULL;
static audio_uploader_connected_cb_t connected_cb = NULL;
static audio_uploader_disconnected_cb_t disconnected_cb = NULL;

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
            if (disconnected_cb) {
                disconnected_cb();
            }
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
    TickType_t last_send_time = 0;
    const TickType_t MIN_SEND_INTERVAL_MS = 5; // 最小发送间隔，避免过于频繁导致帧问题
    int consecutive_failures = 0;  // 连续发送失败计数
    const int MAX_SEND_FAILURES = 5;  // 最大连续失败次数
    
    while (true) {
        if (xQueueReceive(send_queue, &item, portMAX_DELAY) != pdTRUE) continue;
        
        // 检查连接状态
        if (is_connected && ws_client != NULL && esp_websocket_client_is_connected(ws_client)) {
            // 确保发送间隔，避免帧合并或异常
            TickType_t now = xTaskGetTickCount();
            TickType_t elapsed = now - last_send_time;
            if (elapsed < pdMS_TO_TICKS(MIN_SEND_INTERVAL_MS)) {
                vTaskDelay(pdMS_TO_TICKS(MIN_SEND_INTERVAL_MS) - elapsed);
            }
            
            if (ws_mutex && xSemaphoreTake(ws_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                int ret = esp_websocket_client_send_bin(ws_client, (const char*)item.buf, item.len, pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
                xSemaphoreGive(ws_mutex);
                last_send_time = xTaskGetTickCount();
                
                if (ret < 0) {
                    consecutive_failures++;
                    if (consecutive_failures == 1) {
                        ESP_LOGW(TAG, "发送失败 (ret=%d)，将重试", ret);
                    }
                    
                    // 只有连续多次失败才认为断开
                    if (consecutive_failures >= MAX_SEND_FAILURES) {
                        ESP_LOGE(TAG, "连续%d次发送失败，标记为断开", consecutive_failures);
                        is_connected = false;
                        consecutive_failures = 0;
                        clear_queue();
                        vTaskDelay(pdMS_TO_TICKS(1000));
                    } else {
                        // 短暂延迟后重试
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                } else {
                    // 发送成功，重置计数
                    consecutive_failures = 0;
                }
            }
        }
        
        if (item.buf) {
            free(item.buf);
            item.buf = NULL;
        }
    }
}

// ---------------- 公共接口 ----------------

void audio_uploader_init(void) {
    if (send_queue == NULL) {
        send_queue = xQueueCreate(SEND_QUEUE_LEN, sizeof(queue_item_t));
    }
    if (ws_mutex == NULL) {
        ws_mutex = xSemaphoreCreateMutex();
    }

    esp_websocket_client_config_t config = {
        .uri = WEBSOCKET_URI,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 15000,
        .buffer_size = 16384,            // 增大缓冲区到16KB，确保单帧完整发送
        .disable_auto_reconnect = false,
        .keep_alive_enable = true,
        .keep_alive_idle = 15,
        .keep_alive_interval = 10,
        .keep_alive_count = 3,
        .ping_interval_sec = 15,         // WebSocket层面每15秒Ping
        .pingpong_timeout_sec = 45,      // Ping-Pong超时时间
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

    // 2. 限制单包大小，避免触发WebSocket分片 (opCode 0)
    // Opus 60ms 帧通常在 100-300 字节，设置 1024 字节上限足够
    if (len > 1024) {
        ESP_LOGW(TAG, "数据包过大 (%d bytes)，丢弃以避免分片", (int)len);
        return;
    }

    // 3. 队列满时丢弃最新的（保最新）
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
    if (ws_mutex && xSemaphoreTake(ws_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int ret = esp_websocket_client_send_text(ws_client, data, strlen(data), pdMS_TO_TICKS(WS_SEND_TIMEOUT_MS));
        xSemaphoreGive(ws_mutex);
        return ret >= 0;
    }
    return false;
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

void audio_uploader_set_disconnected_cb(audio_uploader_disconnected_cb_t cb) {
    disconnected_cb = cb;
}

bool audio_uploader_is_connected(void) {
    return is_connected && ws_client != NULL && esp_websocket_client_is_connected(ws_client);
}
