#include "audio_uploader.h"
#include "audio_service.h"
#include "wifi_connect.h"
#include "board.h"
#include "pwm_test.h"
#include <esp_log.h>
#include <memory>
#include <string>
#include <algorithm>
#include <cctype>

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

        // 处理后端命令：(command, amplitude)
        std::string text(data, len);
        auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        text.erase(text.begin(), std::find_if(text.begin(), text.end(), [&](char c) { return !is_space(c); }));
        text.erase(std::find_if(text.rbegin(), text.rend(), [&](char c) { return !is_space(c); }).base(), text.end());
        if (!text.empty() && text.front() == '(' && text.back() == ')') {
            text = text.substr(1, text.size() - 2);
        }
        size_t comma = text.find(',');
        std::string cmd = (comma == std::string::npos) ? text : text.substr(0, comma);
        std::string amp_str = (comma == std::string::npos) ? "" : text.substr(comma + 1);
        cmd.erase(cmd.begin(), std::find_if(cmd.begin(), cmd.end(), [&](char c) { return !is_space(c); }));
        cmd.erase(std::find_if(cmd.rbegin(), cmd.rend(), [&](char c) { return !is_space(c); }).base(), cmd.end());
        amp_str.erase(amp_str.begin(), std::find_if(amp_str.begin(), amp_str.end(), [&](char c) { return !is_space(c); }));
        amp_str.erase(std::find_if(amp_str.rbegin(), amp_str.rend(), [&](char c) { return !is_space(c); }).base(), amp_str.end());

        const int kDefaultAmplitude = 10;
        int amplitude = kDefaultAmplitude;
        if (!amp_str.empty()) {
            char* end = nullptr;
            long val = strtol(amp_str.c_str(), &end, 10);
            if (end != amp_str.c_str() && *end == '\0') {
                amplitude = (int)val;
            }
        }
        if (amplitude < 0) {
            amplitude = -amplitude;
        }
        if (amplitude > 100) {
            amplitude = 100;
        }

        if (cmd == "brightness_down" ||
            cmd == "brightness_up" ||
            cmd == "tem_down" ||
            cmd == "tem_up" ||
            cmd == "volume_down" ||
            cmd == "volume_up" ||
            cmd == "volume_set") {
            ESP_LOGI(TAG, "Backend command: %s, amplitude=%d", cmd.c_str(), amplitude);
            if (cmd == "brightness_down") {
                LampAdjustBrightness(-amplitude);
            } else if (cmd == "brightness_up") {
                LampAdjustBrightness(amplitude);
            } else if (cmd == "tem_down") {
                LampAdjustTemperature(-amplitude);
            } else if (cmd == "tem_up") {
                LampAdjustTemperature(amplitude);
            } else if (cmd == "volume_down" || cmd == "volume_up") {
                auto codec = Board::GetInstance().GetAudioCodec();
                if (codec) {
                    int delta = (cmd == "volume_up") ? amplitude : -amplitude;
                    int next_volume = std::max(0, std::min(100, codec->output_volume() + delta));
                    if (next_volume != codec->output_volume()) {
                        codec->SetOutputVolume(next_volume);
                        ESP_LOGI(TAG, "Volume adjusted: %d -> %d", codec->output_volume() - delta, next_volume);
                    }
                }
            } else if (cmd == "volume_set") {
                auto codec = Board::GetInstance().GetAudioCodec();
                if (codec) {
                    int target_volume = std::max(0, std::min(100, amplitude));
                    codec->SetOutputVolume(target_volume);
                    ESP_LOGI(TAG, "Volume set to: %d", target_volume);
                }
            }
        }
    });
}

// 用法：
// 1. 在 app 初始化时调用 audio_afe_ws_sender_init();
// 2. 在 AudioService 初始化后调用 audio_afe_ws_hook(&audio_service);