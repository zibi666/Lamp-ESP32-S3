#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SLEEP_STAGE_UNKNOWN = 0,
    SLEEP_STAGE_WAKE = 1,
    SLEEP_STAGE_REM = 2,
    SLEEP_STAGE_NREM = 3
} sleep_stage_t;

typedef struct {
    float respiratory_rate_bpm;  // 当前窗口的呼吸频率 (次/分)
    float motion_index;          // 当前窗口的体动强度指标 (0-100)
    float heart_rate_mean;       // 当前窗口的平均心率 (bpm)
    float heart_rate_std;        // 当前窗口的心率标准差 (反映HRV)
    uint32_t duration_seconds;   // 该窗口的持续时间，默认为 30s
} sleep_epoch_t;

typedef struct {
    sleep_stage_t stage;
    float respiratory_rate_bpm;
    float motion_index;
    float heart_rate_mean;       // 该epoch的平均心率
    float heart_rate_std;        // 该epoch的心率变异性
} sleep_stage_result_t;

typedef struct {
    float resp_rate_threshold;   // RRthres = mean(RR) + std(RR)
    float motion_threshold;      // Movthres = mean(Mov) + std(Mov)
    float wake_motion_threshold; // 用于判定醒来的体动平均阈值
    float heart_rate_mean;       // 心率均值
    float heart_rate_wake_threshold;  // 清醒心率阈值 = mean(HR) + 0.5*std(HR)
    float hrv_rem_threshold;     // REM心率变异阈值 = mean(HRV) + std(HRV)
} sleep_thresholds_t;

typedef struct {
    uint32_t wake_seconds;
    uint32_t rem_seconds;
    uint32_t nrem_seconds;
    float rem_ratio;             // REM 占比（相对于总睡眠时长）
    float sleep_efficiency;      // 睡眠效率 = (REM + NREM) / 总时长
    float average_resp_rate;
    float average_motion;
    float average_heart_rate;    // 平均心率
    float average_hrv;           // 平均心率变异性
    float sleep_score;           // 简易 0-100 评分
} sleep_quality_report_t;

/**
 * @brief 原始采样数据（来自雷达芯片，每3秒一次）
 * 
 * 芯片通信协议：
 * - 心率上报: 5359 85 02 0001 1B [心率] sum 5443 (被动上报，3秒/次)
 * - 呼吸上报: 5359 81 02 0001 1B [呼吸] sum 5443 (被动上报，3秒/次)
 * - 体动查询: 下发 5359 80 83 0001 0F sum 5443
 *             回复 5359 80 83 0001 1B [体动] sum 5443
 */
typedef struct {
    uint8_t heart_rate_bpm;      // 心率 (60-120 bpm)
    uint8_t respiratory_rate_bpm;// 呼吸率 (0-35 次/分，0表示无效)
    uint8_t motion_level;        // 体动参数 (0-100)
    uint32_t timestamp;          // 时间戳 (秒)
} radar_sample_t;

/**
 * @brief 将原始3秒采样数据聚合为30秒epoch
 * 
 * @param samples       原始采样数组
 * @param sample_count  采样数量
 * @param out_epochs    输出的epoch数组（需预分配）
 * @param max_epochs    out_epochs数组的最大容量
 * @return size_t       实际生成的epoch数量
 */
size_t sleep_analysis_aggregate_samples(const radar_sample_t *samples,
                                        size_t sample_count,
                                        sleep_epoch_t *out_epochs,
                                        size_t max_epochs);

/**
 * @brief 按论文公式计算阈值。
 *        RRthres = mean(RR) + std(RR)
 *        Movthres = mean(Mov) + std(Mov)
 *        wake_motion_threshold = mean(Mov)
 */
void sleep_analysis_compute_thresholds(const sleep_epoch_t *epochs,
                                       size_t count,
                                       sleep_thresholds_t *out_thresholds);

/**
 * @brief 依据阈值进行睡眠阶段判定，包含 REM 纠错逻辑。
 */
void sleep_analysis_detect_stages(const sleep_epoch_t *epochs,
                                  size_t count,
                                  const sleep_thresholds_t *thresholds,
                                  sleep_stage_result_t *out_results);

/**
 * @brief 基于阶段结果做睡眠质量评估（效率、占比与简单评分）。
 */
void sleep_analysis_build_quality(const sleep_epoch_t *epochs,
                                  const sleep_stage_result_t *stages,
                                  size_t count,
                                  sleep_quality_report_t *out_report);

#ifdef __cplusplus
}
#endif
