#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"

// 使用 esp-wifi-connect 组件进行 WiFi 配网
#include <wifi_configuration_ap.h>
#include <wifi_station.h>
#include <ssid_manager.h>

static const char *TAG = "wifi_connect";

static bool s_wifi_connected = false;
static esp_ip4_addr_t s_ip_addr;
static char s_ssid[33] = {0};
static WifiStation* s_wifi_station = nullptr;
static WifiConfigurationAp* s_wifi_ap = nullptr;

// C++ 内部实现
namespace {

bool wifi_connect_impl(void)
{
    // 1. 初始化网络接口（必须在WiFi之前）
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
        return false;
    }
    
    // 2. 初始化WiFi驱动
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(ret));
        return false;
    }
    
    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();

    if (ssid_list.empty()) {
        // 没有保存的 WiFi 配置，启动配网模式
        ESP_LOGI(TAG, "No saved WiFi config, starting provision mode");
        
        // 创建配网AP实例（全局静态，避免被销毁）
        if (!s_wifi_ap) {
            s_wifi_ap = new WifiConfigurationAp();
        }
        
        s_wifi_ap->SetSsidPrefix("Lamp");  // 设置热点名称前缀
        s_wifi_ap->SetLanguage("zh-CN");   // 设置中文界面
        s_wifi_ap->Start();
        
        std::string ssid = s_wifi_ap->GetSsid();
        std::string url = s_wifi_ap->GetWebServerUrl();
        
        ESP_LOGI(TAG, "==============================================");
        ESP_LOGI(TAG, "WiFi 配网模式已启动");
        ESP_LOGI(TAG, "==============================================");
        ESP_LOGI(TAG, "1. 请使用手机连接到热点: %s", ssid.c_str());
        ESP_LOGI(TAG, "2. 浏览器会自动打开配置页面");
        ESP_LOGI(TAG, "3. 如果没有自动打开，请访问: %s", url.c_str());
        ESP_LOGI(TAG, "==============================================");
        
        // 配网模式不返回，等待用户配置后设备会自动重启
        return false;
    }

    // 有保存的配置，尝试连接
    ESP_LOGI(TAG, "Found %d saved WiFi config(s), connecting...", ssid_list.size());
    
    // 创建 WiFi Station 实例（全局静态）
    if (!s_wifi_station) {
        s_wifi_station = new WifiStation();
    }
    
    // 添加所有保存的WiFi配置
    for (const auto& item : ssid_list) {
        s_wifi_station->AddAuth(std::string(item.ssid), std::string(item.password));
        ESP_LOGI(TAG, "Added WiFi: %s", item.ssid.c_str());
    }
    
    // 设置连接成功回调
    s_wifi_station->OnConnected([](const std::string& connected_ssid) {
        ESP_LOGI(TAG, "✓ WiFi 连接成功: %s", connected_ssid.c_str());
        s_wifi_connected = true;
        strncpy(s_ssid, connected_ssid.c_str(), sizeof(s_ssid) - 1);
        
        if (s_wifi_station) {
            std::string ip = s_wifi_station->GetIpAddress();
            if (!ip.empty()) {
                // 解析 IP 地址
                unsigned int ip1, ip2, ip3, ip4;
                if (sscanf(ip.c_str(), "%u.%u.%u.%u", &ip1, &ip2, &ip3, &ip4) == 4) {
                    s_ip_addr.addr = (ip4 << 24) | (ip3 << 16) | (ip2 << 8) | ip1;
                }
            }
        }
    });
    
    s_wifi_station->Start();
    
    // 等待连接（最多等待30秒）
    if (s_wifi_station->WaitForConnected(30000)) {
        ESP_LOGI(TAG, "WiFi 连接成功");
        return true;
    } else {
        ESP_LOGW(TAG, "WiFi 连接超时，可能需要重新配网");
        return false;
    }
}

} // namespace

// C 接口导出
extern "C" {

bool wifi_connect(void)
{
    return wifi_connect_impl();
}

bool wifi_is_connected(void) {
    if (!s_wifi_station) {
        return false;
    }
    return s_wifi_connected && s_wifi_station->IsConnected();
}

esp_ip4_addr_t wifi_get_ip_addr(void) {
    return s_ip_addr;
}

const char* wifi_get_ssid(void) {
    return s_ssid;
}

} // extern "C"
