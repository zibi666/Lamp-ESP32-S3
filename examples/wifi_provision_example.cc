/**
 * @file wifi_provision_example.cc
 * @brief WiFi SoftAP 配网示例
 * 
 * 展示如何使用 WiFiProvisionManager 实现 WiFi 配网功能
 */

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include "network/wifi_provision_manager.h"

static const char* TAG = "WiFiProvisionExample";

extern "C" void app_main(void)
{
    // 1. 初始化 NVS（必需）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 创建默认事件循环（必需）
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. 获取 WiFi 配网管理器实例
    auto& wifi_mgr = WiFiProvisionManager::GetInstance();

    // 4. 初始化配网管理器
    // 参数：设备名称（用于热点名称），界面语言
    wifi_mgr.Initialize("Lamp", "zh-CN");

    // 5. 设置 WiFi 连接成功回调
    wifi_mgr.SetOnConnectedCallback([](const std::string& ssid) {
        ESP_LOGI(TAG, "✓ WiFi 连接成功！");
        ESP_LOGI(TAG, "  SSID: %s", ssid.c_str());
        
        auto& mgr = WiFiProvisionManager::GetInstance();
        ESP_LOGI(TAG, "  IP 地址: %s", mgr.GetIpAddress().c_str());
        ESP_LOGI(TAG, "  信号强度: %d dBm", mgr.GetRssi());
        
        // TODO: 在这里添加 WiFi 连接成功后的业务逻辑
        // 例如：连接到服务器、启动 MQTT 等
    });

    // 6. 设置进入配网模式回调
    wifi_mgr.SetOnProvisionStartCallback([]() {
        ESP_LOGI(TAG, "✓ 配网模式已启动");
        
        auto& mgr = WiFiProvisionManager::GetInstance();
        ESP_LOGI(TAG, "  热点名称: %s", mgr.GetProvisionSsid().c_str());
        
        // TODO: 在这里添加进入配网模式后的业务逻辑
        // 例如：点亮 LED 指示灯、显示二维码等
    });

    // 7. 启动 WiFi 连接或配网
    // 自动判断：有配置则连接，无配置则进入配网模式
    wifi_mgr.Start();

    // 8. 主循环
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        if (wifi_mgr.IsInProvisionMode()) {
            // 配网模式下的处理
            ESP_LOGI(TAG, "等待用户配置 WiFi...");
        } else if (wifi_mgr.IsConnected()) {
            // 已连接状态下的处理
            ESP_LOGI(TAG, "WiFi 已连接，RSSI: %d dBm", wifi_mgr.GetRssi());
        } else {
            // 连接中或断开状态
            ESP_LOGI(TAG, "WiFi 连接中...");
        }
    }
}

/**
 * @brief 高级示例：手动进入配网模式
 * 
 * 例如：通过按钮长按触发配网模式
 */
void enter_provision_mode_manually()
{
    auto& wifi_mgr = WiFiProvisionManager::GetInstance();
    
    ESP_LOGI(TAG, "手动进入配网模式");
    
    // 强制进入配网模式（即使有保存的配置）
    wifi_mgr.StartProvisionMode();
}

/**
 * @brief 高级示例：清除所有 WiFi 配置
 * 
 * 例如：通过按钮触发恢复出厂设置
 */
void clear_wifi_configs()
{
    auto& wifi_mgr = WiFiProvisionManager::GetInstance();
    
    ESP_LOGI(TAG, "清除所有 WiFi 配置");
    wifi_mgr.ClearAllConfigs();
    
    // 清除后可以重启设备，或者直接进入配网模式
    ESP_LOGI(TAG, "重启设备...");
    esp_restart();
}
