#include "smart_light_controller.h"
#include "smart_light_backlight_bridge.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "SmartLight";

// 常量定义
#define MOTION_THRESHOLD_HIGH 20.0f     // 高体动阈值（起床/返回）
#define MOTION_THRESHOLD_LOW 5.0f       // 低体动阈值（离开/躺下）
#define BRIGHTNESS_INITIAL 20           // 初始亮度
#define BRIGHTNESS_INCREMENT 5          // 亮度递增值
#define BRIGHTNESS_MAX 80               // 最大亮度
#define BRIGHTNESS_INCREASE_INTERVAL 30 // 亮度递增间隔（秒）
#define LOW_MOTION_SETTLE_EPOCHS 2      // 需要连续2个epoch低体动才确认躺下（1分钟）

void smart_light_init(smart_light_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    
    memset(ctx, 0, sizeof(smart_light_context_t));
    ctx->state = SMART_LIGHT_IDLE;
    ctx->current_brightness = 0;
    ctx->prev_sleep_stage = SLEEP_STAGE_UNKNOWN;
    ctx->prev_motion_index = 0.0f;
    ctx->motion_event_count = 0;
    ctx->low_motion_epochs = 0;
    ctx->was_high_motion = false;
    
    ESP_LOGI(TAG, "智能灯光控制器已初始化");
}

void smart_light_update(smart_light_context_t *ctx,
                       sleep_stage_t current_stage,
                       int sleep_state,
                       float motion_index,
                       uint32_t timestamp) {
    if (ctx == NULL) {
        return;
    }
    
    // 检测体动变化模式
    bool is_high_motion = (motion_index > MOTION_THRESHOLD_HIGH);
    bool is_low_motion = (motion_index < MOTION_THRESHOLD_LOW);
    
    // 检测体动从低到高的转变（体动事件）
    bool motion_rise_event = false;
    if (!ctx->was_high_motion && is_high_motion) {
        motion_rise_event = true;
        ctx->motion_event_count++;
        ESP_LOGI(TAG, "检测到体动上升事件 #%lu (体动值: %.1f)", 
                (unsigned long)ctx->motion_event_count, motion_index);
    }
    
    // 更新低体动epoch计数
    if (is_low_motion) {
        ctx->low_motion_epochs++;
    } else {
        ctx->low_motion_epochs = 0;
    }
    
    // 状态机
    switch (ctx->state) {
        case SMART_LIGHT_IDLE: {
            // 检测从睡眠到清醒的转换 + 第一次体动增大
            bool wake_from_sleep = (ctx->prev_sleep_stage == SLEEP_STAGE_NREM || 
                                   ctx->prev_sleep_stage == SLEEP_STAGE_REM) &&
                                   (current_stage == SLEEP_STAGE_WAKE);
            
            // 或者从SLEEP_SLEEPING状态转为SLEEP_MONITORING
            bool state_wake = (sleep_state == 0); // SLEEP_MONITORING = 0
            
            if ((wake_from_sleep || state_wake) && motion_rise_event) {
                // 触发开灯
                ctx->state = SMART_LIGHT_ON_INITIAL;
                ctx->current_brightness = BRIGHTNESS_INITIAL;
                ctx->last_increase_time = timestamp;
                ctx->motion_event_count = 1; // 重置为第一次事件
                
                // 控制硬件
                smart_light_set_backlight(ctx->current_brightness);
                
                ESP_LOGI(TAG, "🌟 触发开灯！用户起床 (亮度: %d)", ctx->current_brightness);
            }
            break;
        }
        
        case SMART_LIGHT_ON_INITIAL: {
            // 立即转入递增状态
            ctx->state = SMART_LIGHT_INCREASING;
            ESP_LOGI(TAG, "进入亮度递增模式");
            break;
        }
        
        case SMART_LIGHT_INCREASING: {
            // 检查是否需要增加亮度
            uint32_t elapsed = timestamp - ctx->last_increase_time;
            if (elapsed >= BRIGHTNESS_INCREASE_INTERVAL && 
                ctx->current_brightness < BRIGHTNESS_MAX) {
                ctx->current_brightness += BRIGHTNESS_INCREMENT;
                if (ctx->current_brightness > BRIGHTNESS_MAX) {
                    ctx->current_brightness = BRIGHTNESS_MAX;
                }
                ctx->last_increase_time = timestamp;
                
                // 控制硬件
                smart_light_set_backlight(ctx->current_brightness);
                
                ESP_LOGI(TAG, "⬆️ 亮度递增至 %d", ctx->current_brightness);
            }
            
            // 检测用户离开（体动变低）
            if (is_low_motion) {
                ESP_LOGI(TAG, "检测到体动降低，用户可能离开测量范围 (体动: %.1f)", motion_index);
                ctx->state = SMART_LIGHT_MONITORING_RETURN;
            }
            break;
        }
        
        case SMART_LIGHT_MONITORING_RETURN: {
            // 继续递增亮度（如果还没到最大值）
            uint32_t elapsed = timestamp - ctx->last_increase_time;
            if (elapsed >= BRIGHTNESS_INCREASE_INTERVAL && 
                ctx->current_brightness < BRIGHTNESS_MAX) {
                ctx->current_brightness += BRIGHTNESS_INCREMENT;
                if (ctx->current_brightness > BRIGHTNESS_MAX) {
                    ctx->current_brightness = BRIGHTNESS_MAX;
                }
                ctx->last_increase_time = timestamp;
                
                // 控制硬件
                smart_light_set_backlight(ctx->current_brightness);
                
                ESP_LOGI(TAG, "⬆️ 亮度递增至 %d (等待返回)", ctx->current_brightness);
            }
            
            // 检测第二次体动上升（用户返回）
            if (motion_rise_event && ctx->motion_event_count >= 2) {
                ESP_LOGI(TAG, "🔙 检测到用户返回 (第%lu次体动事件)", 
                        (unsigned long)ctx->motion_event_count);
                ctx->state = SMART_LIGHT_WAITING_SETTLE;
                ctx->low_motion_epochs = 0;
            }
            break;
        }
        
        case SMART_LIGHT_WAITING_SETTLE: {
            // 等待用户重新躺下（体动持续降低）
            if (ctx->low_motion_epochs >= LOW_MOTION_SETTLE_EPOCHS) {
                // 用户已经躺下，关灯
                ESP_LOGI(TAG, "💤 用户重新躺下，关闭灯光 (连续%lu个epoch低体动)", 
                        (unsigned long)ctx->low_motion_epochs);
                ctx->state = SMART_LIGHT_IDLE;
                ctx->current_brightness = 0;
                ctx->motion_event_count = 0;
                ctx->low_motion_epochs = 0;
                
                // 控制硬件关灯
                smart_light_set_backlight(0);
            } else if (is_low_motion) {
                ESP_LOGI(TAG, "等待躺下确认... (%lu/%d epochs)", 
                        (unsigned long)ctx->low_motion_epochs, LOW_MOTION_SETTLE_EPOCHS);
            } else {
                // 体动仍然较高，用户还在活动
                ESP_LOGI(TAG, "用户仍在活动 (体动: %.1f)", motion_index);
            }
            break;
        }
    }
    
    // 更新历史状态
    ctx->prev_sleep_stage = current_stage;
    ctx->prev_motion_index = motion_index;
    ctx->was_high_motion = is_high_motion;
}

uint8_t smart_light_get_brightness(const smart_light_context_t *ctx) {
    if (ctx == NULL) {
        return 0;
    }
    return ctx->current_brightness;
}

const char* smart_light_get_state_str(const smart_light_context_t *ctx) {
    if (ctx == NULL) {
        return "未知";
    }
    
    switch (ctx->state) {
        case SMART_LIGHT_IDLE:
            return "空闲";
        case SMART_LIGHT_ON_INITIAL:
            return "初始开启";
        case SMART_LIGHT_INCREASING:
            return "亮度递增";
        case SMART_LIGHT_MONITORING_RETURN:
            return "等待返回";
        case SMART_LIGHT_WAITING_SETTLE:
            return "等待躺下";
        default:
            return "未知";
    }
}
