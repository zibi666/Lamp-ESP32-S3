#include "audio_uploader.h"
#include "audio_service.h"
#include "boards/common/wifi_connect.h"
#include "boards/common/board.h"
#include <esp_log.h>
#include <memory>
#include <string>

#define TAG "AFE_WS_SENDER"

static bool ws_ready = false;
static AudioService* g_service = nullptr;

// 延迟到 WiFi 已连接后再去初始化 WebSocket，避免在 netif 未就绪时崩溃
extern "C" void audio_afe_ws_sender_init(void) {
    if (ws_ready) {
        return;
    }
    if (!wifi_is_connected()) {
        return;
    }
    audio_uploader_init();
    ws_ready = true;
    ESP_LOGI(TAG, "AFE WebSocket sender initialized after WiFi up");
}

// 发送降噪/回声消除后的语音流
void audio_afe_ws_send(const int16_t *data, int samples) {
    audio_afe_ws_sender_init();
    if (!ws_ready) {
        return; // WiFi 尚未连上，丢弃
    }
    audio_uploader_send(data, samples);
}

// 挂载到 AudioService 的 AFE输出回调
void audio_afe_ws_hook(AudioService* service) {
    g_service = service;
    service->SetAfeOutputCallback([](std::vector<int16_t>&& pcm) {
        audio_afe_ws_send(pcm.data(), pcm.size());
    });
}

// 将 Opus 编码后的数据通过发送队列上传
void audio_afe_ws_attach_send_callbacks(AudioService* service, AudioServiceCallbacks& callbacks) {
    g_service = service;
    callbacks.on_send_queue_available = []() {
        if (!g_service) return;
        while (true) {
            auto pkt = g_service->PopPacketFromSendQueue();
            if (!pkt) break;
            audio_uploader_send_bytes(pkt->payload.data(), pkt->payload.size());
        }
    };
}

// 将服务端推送的 Opus 二进制数据放入解码队列
void audio_afe_ws_attach_downlink(AudioService* service) {
    g_service = service;
    audio_uploader_set_binary_cb([](const uint8_t* data, size_t len) {
        if (!g_service || !data || len == 0) {
            return;
        }

        auto packet = std::make_unique<AudioStreamPacket>();
        packet->sample_rate = 24000;                 // 匹配硬件输出采样率，避免重采样
        packet->frame_duration = OPUS_FRAME_DURATION_MS;
        packet->payload.assign(data, data + len);

        if (!g_service->PushPacketToDecodeQueue(std::move(packet), false)) {
            ESP_LOGW(TAG, "decode queue full, drop downstream audio len=%d", (int)len);
        }
    });

    audio_uploader_set_text_cb([](const char* data, size_t len) {
        ESP_LOGI(TAG, "WS text: %.*s", (int)len, data);
        
        // Handle volume control from server (numeric string "0"-"100")
        if (len > 0 && len < 4) { // Volume string shouldn't be long
            std::string text(data, len);
            char* end;
            long val = strtol(text.c_str(), &end, 10);
            if (end != text.c_str() && *end == '\0') {
                if (val >= 0 && val <= 100) {
                    auto codec = Board::GetInstance().GetAudioCodec();
                    if (codec) {
                        codec->SetOutputVolume((int)val);
                        ESP_LOGI(TAG, "Server set volume to %ld", val);
                    }
                }
            }
        }
    });
}

// 用法：
// 1. 在 app 初始化时调用 audio_afe_ws_sender_init();
// 2. 在 AudioService 初始化后调用 audio_afe_ws_hook(&audio_service);
