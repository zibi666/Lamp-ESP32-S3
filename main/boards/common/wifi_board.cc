#include "wifi_board.h"

#include "board.h"
#include "application.h"
#include "system_info.h"
#include "wifi_connect.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_network.h>
#include <esp_log.h>
#include <esp_netif_ip_addr.h>
#include <font_awesome.h>

static const char *TAG = "WifiBoard";

namespace {
std::string IpAddressToString(const esp_ip4_addr_t& addr) {
    char buffer[16] = {0};
    esp_ip4addr_ntoa(&addr, buffer, sizeof(buffer));
    return std::string(buffer);
}
}

WifiBoard::WifiBoard() {
}

std::string WifiBoard::GetBoardType() {
    return "wifi";
}

void WifiBoard::StartNetwork() {
    ESP_LOGI(TAG, "Connecting to fixed Wi-Fi network");
    if (!wifi_connect()) {
        ESP_LOGE(TAG, "Failed to connect to predefined Wi-Fi credentials");
        return;
    }
}

NetworkInterface* WifiBoard::GetNetwork() {
    static EspNetwork network;
    return &network;
}

const char* WifiBoard::GetNetworkStateIcon() {
    return wifi_is_connected() ? FONT_AWESOME_WIFI : FONT_AWESOME_WIFI_SLASH;
}

std::string WifiBoard::GetBoardJson() {
    // Set the board type for OTA
    std::string board_json = R"({)";
    board_json += R"("type":")" + std::string(BOARD_TYPE) + R"(",)";
    board_json += R"("name":")" + std::string(BOARD_NAME) + R"(",)";
    if (wifi_is_connected()) {
        auto ip = IpAddressToString(wifi_get_ip_addr());
        board_json += R"("ssid":")" + std::string(wifi_get_ssid()) + R"(",)";
        board_json += R"("ip":")" + ip + R"(",)";
    }
    board_json += R"("mac":")" + SystemInfo::GetMacAddress() + R"(")";
    board_json += R"(})";
    return board_json;
}

void WifiBoard::SetPowerSaveMode(bool enabled) {
    // WifiStation::GetInstance().SetPowerSaveMode(enabled);
}

void WifiBoard::ResetWifiConfiguration() {
    ESP_LOGW(TAG, "ResetWifiConfiguration not supported in fixed Wi-Fi mode");
}

std::string WifiBoard::GetDeviceStatusJson() {
    /*
     * 返回设备状态JSON
     * 
     * 返回的JSON结构如下：
     * {
     *     "audio_speaker": {
     *         "volume": 70
     *     },
     *     "screen": {
     *         "brightness": 100,
     *         "theme": "light"
     *     },
     *     "network": {
     *         "type": "wifi",
     *         "ssid": "Xiaozhi",
     *         "rssi": -60
     *     },
     *     "chip": {
     *         "temperature": 25
     *     }
     * }
     */
    auto& board = Board::GetInstance();
    auto root = cJSON_CreateObject();

    // Audio speaker
    auto audio_speaker = cJSON_CreateObject();
    auto audio_codec = board.GetAudioCodec();
    if (audio_codec) {
        cJSON_AddNumberToObject(audio_speaker, "volume", audio_codec->output_volume());
    }
    cJSON_AddItemToObject(root, "audio_speaker", audio_speaker);

    // Screen brightness
    auto backlight = board.GetBacklight();
    auto screen = cJSON_CreateObject();
    if (backlight) {
        cJSON_AddNumberToObject(screen, "brightness", backlight->brightness());
    }
    cJSON_AddItemToObject(root, "screen", screen);

    // Network
    auto network = cJSON_CreateObject();
    cJSON_AddStringToObject(network, "type", "wifi");
    if (wifi_is_connected()) {
        cJSON_AddStringToObject(network, "ssid", wifi_get_ssid());
        cJSON_AddStringToObject(network, "signal", "strong"); // Placeholder
    } else {
        cJSON_AddStringToObject(network, "ssid", "");
        cJSON_AddStringToObject(network, "signal", "none");
    }
    cJSON_AddItemToObject(root, "network", network);

    // Chip
    float esp32temp = 0.0f;
    if (board.GetTemperature(esp32temp)) {
        auto chip = cJSON_CreateObject();
        cJSON_AddNumberToObject(chip, "temperature", esp32temp);
        cJSON_AddItemToObject(root, "chip", chip);
    }

    auto json_str = cJSON_PrintUnformatted(root);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return json;
}
