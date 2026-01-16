#include "smart_light_backlight_bridge.h"
#include "board.h"
#include "backlight.h"
#include "esp_log.h"

static const char *TAG = "SmartLight_Bridge";

extern "C" void smart_light_set_backlight(uint8_t brightness) {
    Board& board = Board::GetInstance();
    Backlight* backlight = board.GetBacklight();
    
    if (backlight != nullptr) {
        backlight->SetBrightness(brightness, false);
        ESP_LOGI(TAG, "设置灯光亮度: %d", brightness);
    } else {
        ESP_LOGW(TAG, "无法获取背光控制器");
    }
}
