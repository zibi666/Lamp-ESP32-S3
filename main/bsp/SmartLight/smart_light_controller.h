#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "sleep_analysis.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 智能灯光控制器状态
 */
typedef enum {
    SMART_LIGHT_IDLE = 0,              // 空闲状态，灯光关闭
    SMART_LIGHT_ON_INITIAL,            // 灯光初始开启，亮度20
    SMART_LIGHT_INCREASING,            // 亮度递增中，每30秒+5
    SMART_LIGHT_MONITORING_RETURN,     // 监测用户返回（第二次体动）
    SMART_LIGHT_WAITING_SETTLE         // 等待用户重新躺下（体动降低）
} smart_light_state_t;

/**
 * @brief 智能灯光控制器上下文
 */
typedef struct {
    smart_light_state_t state;          // 当前状态
    uint8_t current_brightness;         // 当前亮度
    uint32_t last_increase_time;        // 上次增加亮度的时间戳
    
    // 状态追踪
    sleep_stage_t prev_sleep_stage;     // 上一次的睡眠阶段
    float prev_motion_index;            // 上一次的体动值
    uint32_t motion_event_count;        // 体动事件计数（0->大->0为一个周期）
    uint32_t low_motion_epochs;         // 连续低体动epoch计数
    bool was_high_motion;               // 上次是否为高体动
    
} smart_light_context_t;

/**
 * @brief 初始化智能灯光控制器
 * 
 * @param ctx 控制器上下文指针
 */
void smart_light_init(smart_light_context_t *ctx);

/**
 * @brief 更新智能灯光控制器（每个epoch调用一次）
 * 
 * @param ctx 控制器上下文指针
 * @param current_stage 当前睡眠阶段
 * @param sleep_state 睡眠状态（SLEEP_MONITORING/SLEEP_SETTLING/SLEEP_SLEEPING）
 * @param motion_index 当前体动值 (0-100)
 * @param timestamp 当前时间戳（秒）
 */
void smart_light_update(smart_light_context_t *ctx,
                       sleep_stage_t current_stage,
                       int sleep_state,
                       float motion_index,
                       uint32_t timestamp);

/**
 * @brief 获取当前应该设置的灯光亮度
 * 
 * @param ctx 控制器上下文指针
 * @return uint8_t 亮度值 (0-100)，0表示关闭
 */
uint8_t smart_light_get_brightness(const smart_light_context_t *ctx);

/**
 * @brief 获取当前状态的字符串表示
 * 
 * @param ctx 控制器上下文指针
 * @return const char* 状态字符串
 */
const char* smart_light_get_state_str(const smart_light_context_t *ctx);

#ifdef __cplusplus
}
#endif
