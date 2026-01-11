#include "sleep_analysis.h"

#include <algorithm>
#include <cmath>
#include <cstring>

/**
 * 基于论文 "Unsupervised Detection of Multiple Sleep Stages Using a Single FMCW Radar"
 * (Applied Sciences 2023, 13, 4468) 实现的睡眠阶段检测算法
 * 
 * 论文准确率：平均 68.91%，单个受试者最高 88.8%
 * 
 * 核心公式：
 * - RRthres = mean(RR) + std(RR)        呼吸率阈值
 * - Movthres = mean(Mov) + std(Mov)     运动阈值
 * - Wake: Mov(t) > mean(Mov)            运动超过平均值即为清醒
 * - REM: RR(t) > RRthres && Mov(t) <= Movthres   高呼吸率且低运动
 * - NREM: 其他情况
 * 
 * 适配说明（针对每3秒采样的雷达芯片）：
 * - 体动参数范围: 0-100
 * - 采样间隔: 3秒
 * - 聚合方式: 每10个采样（30秒）聚合为1个epoch
 */

/* 每个epoch包含的采样数（30秒 / 3秒 = 10） */
#define SAMPLES_PER_EPOCH 10
#define EPOCH_DURATION_SECONDS 30

namespace {
float safe_duration(const sleep_epoch_t &epoch) {
    return epoch.duration_seconds > 0 ? static_cast<float>(epoch.duration_seconds) : 60.0f;
}

float clamp(float v, float lo, float hi) {
    return std::max(lo, std::min(v, hi));
}

struct Statistics {
    float mean = 0.0f;
    float stddev = 0.0f;
    float min_val = 0.0f;
    float max_val = 0.0f;
};

/**
 * @brief 计算统计量（均值、标准差、最小值、最大值）
 * 论文公式 (8) 和 (11)
 */
Statistics compute_statistics(const sleep_epoch_t *epochs, size_t count, bool use_motion) {
    if (count == 0 || epochs == nullptr) {
        return {};
    }

    float sum = 0.0f;
    float min_val = 1e9f;
    float max_val = -1e9f;
    
    for (size_t i = 0; i < count; ++i) {
        float v = use_motion ? epochs[i].motion_index : epochs[i].respiratory_rate_bpm;
        sum += v;
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
    }
    const float mean = sum / static_cast<float>(count);

    float var = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const float v = use_motion ? epochs[i].motion_index : epochs[i].respiratory_rate_bpm;
        const float d = v - mean;
        var += d * d;
    }
    /* 使用 N-1 计算样本标准差 */
    const float stddev = count > 1 ? std::sqrt(var / static_cast<float>(count - 1)) : 0.0f;

    return {mean, stddev, min_val, max_val};
}

/**
 * @brief 中值滤波（3点），用于平滑运动数据
 * 论文中提到使用中值滤波减少瞬时运动的影响
 */
float median3(float a, float b, float c) {
    return a + b + c - std::max({a, b, c}) - std::min({a, b, c});
}

/**
 * @brief 5点中值滤波，更好的平滑效果
 */
float median5(float a, float b, float c, float d, float e) {
    float arr[5] = {a, b, c, d, e};
    std::sort(arr, arr + 5);
    return arr[2];
}
}

/**
 * @brief 将原始3秒采样数据聚合为30秒epoch
 * 
 * 聚合策略：
 * - 呼吸率：取有效值的平均（跳过0值，0表示未检测到）
 * - 体动参数：取最大值（反映该时段内的最大活动）
 * - 心率：计算均值和标准差（反映HRV心率变异性）
 * 
 * 芯片数值范围：
 * - 心率: 60-120 bpm
 * - 呼吸: 0-35 次/分 (0=无效)
 * - 体动: 0-100
 * 
 * @param samples       原始采样数组（每3秒一个）
 * @param sample_count  采样数量
 * @param out_epochs    输出的epoch数组（需预分配）
 * @param max_epochs    out_epochs数组的最大容量
 * @return size_t       实际生成的epoch数量
 */
extern "C" size_t sleep_analysis_aggregate_samples(const radar_sample_t *samples,
                                                    size_t sample_count,
                                                    sleep_epoch_t *out_epochs,
                                                    size_t max_epochs) {
    if (samples == nullptr || out_epochs == nullptr || sample_count == 0 || max_epochs == 0) {
        return 0;
    }

    size_t epoch_count = 0;
    size_t sample_idx = 0;

    while (sample_idx < sample_count && epoch_count < max_epochs) {
        float resp_sum = 0.0f;
        float motion_max = 0.0f;
        float hr_sum = 0.0f;
        float hr_values[SAMPLES_PER_EPOCH] = {0};
        size_t valid_resp_count = 0;
        size_t valid_hr_count = 0;
        size_t sample_in_epoch = 0;

        /* 聚合 SAMPLES_PER_EPOCH 个采样 */
        for (size_t j = 0; j < SAMPLES_PER_EPOCH && sample_idx < sample_count; ++j, ++sample_idx) {
            /* 呼吸率：跳过0值（无效），范围1-35有效 */
            uint8_t resp = samples[sample_idx].respiratory_rate_bpm;
            if (resp > 0 && resp <= 35) {
                resp_sum += static_cast<float>(resp);
                valid_resp_count++;
            }
            
            /* 体动取最大值，范围0-100 */
            float motion = static_cast<float>(samples[sample_idx].motion_level);
            if (motion > motion_max) {
                motion_max = motion;
            }
            
            /* 心率：收集有效值，范围60-120有效 */
            uint8_t hr = samples[sample_idx].heart_rate_bpm;
            if (hr >= 60 && hr <= 120) {
                hr_values[valid_hr_count] = static_cast<float>(hr);
                hr_sum += hr_values[valid_hr_count];
                valid_hr_count++;
            }
            
            sample_in_epoch++;
        }

        if (sample_in_epoch > 0) {
            /* 呼吸率：有效值平均，若全无效则使用默认值15 */
            out_epochs[epoch_count].respiratory_rate_bpm = 
                (valid_resp_count > 0) ? (resp_sum / static_cast<float>(valid_resp_count)) : 15.0f;
            out_epochs[epoch_count].motion_index = motion_max;
            
            /* 心率均值和标准差（HRV指标） */
            if (valid_hr_count > 0) {
                float hr_mean = hr_sum / static_cast<float>(valid_hr_count);
                out_epochs[epoch_count].heart_rate_mean = hr_mean;
                
                /* 计算心率标准差（反映短期HRV） */
                float hr_var = 0.0f;
                for (size_t k = 0; k < valid_hr_count; ++k) {
                    float diff = hr_values[k] - hr_mean;
                    hr_var += diff * diff;
                }
                out_epochs[epoch_count].heart_rate_std = 
                    (valid_hr_count > 1) ? std::sqrt(hr_var / static_cast<float>(valid_hr_count - 1)) : 0.0f;
            } else {
                out_epochs[epoch_count].heart_rate_mean = 70.0f;  /* 默认心率 */
                out_epochs[epoch_count].heart_rate_std = 2.0f;    /* 默认HRV */
            }
            
            out_epochs[epoch_count].duration_seconds = EPOCH_DURATION_SECONDS;
            epoch_count++;
        }
    }

    return epoch_count;
}

extern "C" void sleep_analysis_compute_thresholds(const sleep_epoch_t *epochs,
                                                   size_t count,
                                                   sleep_thresholds_t *out_thresholds) {
    if (out_thresholds == nullptr) {
        return;
    }
    
    /* 默认阈值（当数据不足时使用）- 已适配体动参数0-100范围 */
    sleep_thresholds_t defaults{
        .resp_rate_threshold = 16.0f,      /* 成人正常呼吸率 12-20 次/分 */
        .motion_threshold = 30.0f,         /* 运动阈值（0-100范围） */
        .wake_motion_threshold = 15.0f,    /* 清醒运动阈值（0-100范围） */
        .heart_rate_mean = 70.0f,          /* 默认平均心率 */
        .heart_rate_wake_threshold = 75.0f,/* 清醒心率阈值 */
        .hrv_rem_threshold = 4.0f          /* REM期HRV阈值 */
    };
    *out_thresholds = defaults;

    if (epochs == nullptr || count < 10) {
        /* 数据量太少，使用默认值 */
        return;
    }

    /* 论文公式 (8): RRthres = mean(RR) + std(RR) */
    const Statistics rr_stats = compute_statistics(epochs, count, /*use_motion=*/false);
    
    /* 论文公式 (11): Movthres = mean(Mov) + std(Mov) */
    const Statistics mv_stats = compute_statistics(epochs, count, /*use_motion=*/true);

    /* 
     * 论文核心阈值计算：
     * - resp_rate_threshold: 用于判断 REM（呼吸率 > 阈值）
     * - motion_threshold: 用于修正 REM 误判（高运动排除 REM）
     * - wake_motion_threshold: 用于判断 Wake（运动 > 平均值）
     */
    out_thresholds->resp_rate_threshold = rr_stats.mean + rr_stats.stddev;
    out_thresholds->motion_threshold = mv_stats.mean + mv_stats.stddev;
    out_thresholds->wake_motion_threshold = mv_stats.mean;  /* 论文公式 (12) */
    
    /*
     * 心率相关阈值计算（扩展算法）：
     * 
     * 睡眠生理学基础：
     * - Wake: 心率较高（交感神经活跃）
     * - REM: 心率变异性高（类似清醒状态）
     * - NREM: 心率低且稳定（副交感神经主导）
     */
    float hr_sum = 0.0f;
    float hrv_sum = 0.0f;
    float hr_var_sum = 0.0f;
    float hrv_var_sum = 0.0f;
    
    for (size_t i = 0; i < count; ++i) {
        hr_sum += epochs[i].heart_rate_mean;
        hrv_sum += epochs[i].heart_rate_std;
    }
    
    float hr_mean = hr_sum / static_cast<float>(count);
    float hrv_mean = hrv_sum / static_cast<float>(count);
    
    for (size_t i = 0; i < count; ++i) {
        float hr_diff = epochs[i].heart_rate_mean - hr_mean;
        float hrv_diff = epochs[i].heart_rate_std - hrv_mean;
        hr_var_sum += hr_diff * hr_diff;
        hrv_var_sum += hrv_diff * hrv_diff;
    }
    
    float hr_std = (count > 1) ? std::sqrt(hr_var_sum / static_cast<float>(count - 1)) : 0.0f;
    float hrv_std = (count > 1) ? std::sqrt(hrv_var_sum / static_cast<float>(count - 1)) : 0.0f;
    
    out_thresholds->heart_rate_mean = hr_mean;
    /* 清醒时心率通常高于平均值 */
    out_thresholds->heart_rate_wake_threshold = hr_mean + 0.5f * hr_std;
    /* REM期HRV通常高于平均值 */
    out_thresholds->hrv_rem_threshold = hrv_mean + hrv_std;
}

/**
 * @brief 睡眠阶段检测 - 基于论文算法 + 心率扩展
 * 
 * 原论文判断逻辑（呼吸+运动）：
 * 1. Wake检测 (公式12): Mov(t) > mean(Mov) → Wake
 * 2. REM检测 (公式7): RR(t) > RRthres → REM（初步）
 * 3. REM修正 (公式10): 如果 REM && Mov(t) > Movthres → 不是REM，是Wake
 * 4. NREM: 其余情况
 * 
 * 心率扩展逻辑（提高准确率）：
 * - Wake辅助判断: HR(t) > HR_wake_threshold → 增加Wake置信度
 * - REM辅助判断: HRV(t) > HRV_rem_threshold → 增加REM置信度
 * - NREM特征: HR低且稳定（HRV低）
 * 
 * 优先级：Wake > REM > NREM
 */
extern "C" void sleep_analysis_detect_stages(const sleep_epoch_t *epochs,
                                              size_t count,
                                              const sleep_thresholds_t *thresholds,
                                              sleep_stage_result_t *out_results) {
    if (epochs == nullptr || thresholds == nullptr || out_results == nullptr || count == 0) {
        return;
    }

    /* 第一遍：计算平滑后的运动指数并初步判断 */
    for (size_t i = 0; i < count; ++i) {
        /* 使用中值滤波平滑运动数据，减少瞬时运动噪声 */
        float motion_smoothed;
        if (count >= 5 && i >= 2 && i + 2 < count) {
            /* 5点中值滤波 */
            motion_smoothed = median5(
                epochs[i - 2].motion_index,
                epochs[i - 1].motion_index,
                epochs[i].motion_index,
                epochs[i + 1].motion_index,
                epochs[i + 2].motion_index
            );
        } else {
            /* 边界使用3点中值滤波 */
            float prev = (i == 0) ? epochs[i].motion_index : epochs[i - 1].motion_index;
            float curr = epochs[i].motion_index;
            float next = (i + 1 < count) ? epochs[i + 1].motion_index : epochs[i].motion_index;
            motion_smoothed = median3(prev, curr, next);
        }

        /* 获取当前epoch的心率特征 */
        const float hr_mean = epochs[i].heart_rate_mean;
        const float hr_std = epochs[i].heart_rate_std;  /* HRV指标 */
        
        /* 
         * ========== 论文原始判断 ==========
         * 
         * 1. Wake判断 (公式12): 
         *    如果运动 > 运动平均值，判定为清醒
         */
        const bool motion_wake = motion_smoothed > thresholds->wake_motion_threshold;
        
        /* 
         * 2. REM初步判断 (公式7):
         *    如果呼吸率 > 呼吸率阈值，初步判定为REM
         */
        const bool resp_rem = epochs[i].respiratory_rate_bpm > thresholds->resp_rate_threshold;
        
        /* 
         * 3. REM修正 (公式10):
         *    如果初步判定为REM，但运动 > 运动阈值，则排除REM判定
         */
        const bool high_motion = motion_smoothed > thresholds->motion_threshold;
        
        /* 
         * ========== 心率扩展判断 ==========
         * 
         * 4. 心率辅助Wake判断:
         *    清醒时交感神经活跃，心率升高
         */
        const bool hr_wake = hr_mean > thresholds->heart_rate_wake_threshold;
        
        /* 
         * 5. 心率变异性辅助REM判断:
         *    REM期类似清醒，HRV较高
         *    NREM期副交感神经主导，HRV较低
         */
        const bool hrv_rem = hr_std > thresholds->hrv_rem_threshold;
        
        /* 
         * 6. 心率辅助NREM判断:
         *    深睡眠时心率较低且稳定
         */
        const bool hr_nrem = (hr_mean < thresholds->heart_rate_mean) && 
                              (hr_std < thresholds->hrv_rem_threshold);

        /* 
         * ========== 综合判断逻辑 ==========
         * 
         * 使用加权投票机制，结合多个特征：
         * - 运动和心率都指向Wake → 高置信度Wake
         * - 呼吸和HRV都指向REM → 高置信度REM
         * - 心率低且稳定 → 高置信度NREM
         */
        
        /* Wake判定：运动高 OR (运动中等 AND 心率高) */
        const bool is_wake = motion_wake || (high_motion && hr_wake);
        
        /* REM判定：呼吸率高 AND 运动不高 AND (HRV高 OR 呼吸特征明显) */
        const bool is_rem = !is_wake && resp_rem && !high_motion && 
                           (hrv_rem || (resp_rem && !hr_nrem));

        /* 阶段判定（按优先级） */
        sleep_stage_t stage;
        if (is_wake) {
            stage = SLEEP_STAGE_WAKE;
        } else if (is_rem) {
            stage = SLEEP_STAGE_REM;
        } else {
            stage = SLEEP_STAGE_NREM;
        }

        out_results[i].stage = stage;
        out_results[i].respiratory_rate_bpm = epochs[i].respiratory_rate_bpm;
        out_results[i].motion_index = motion_smoothed;
        out_results[i].heart_rate_mean = hr_mean;
        out_results[i].heart_rate_std = hr_std;
    }

    /* 第二遍：平滑处理，避免孤立的阶段判断 */
    /* 论文中没有明确提到，但实际应用中常用于提高一致性 */
    for (size_t i = 1; i + 1 < count; ++i) {
        /* 如果前后都是同一阶段，当前不同，则修正为前后的阶段 */
        if (out_results[i - 1].stage == out_results[i + 1].stage &&
            out_results[i].stage != out_results[i - 1].stage) {
            out_results[i].stage = out_results[i - 1].stage;
        }
    }
}

/**
 * @brief 睡眠质量评估
 * 
 * 基于检测结果计算：
 * - 各阶段时长
 * - 睡眠效率 = (REM + NREM) / 总时长
 * - REM占比（正常成人约 20-25%）
 * - 综合评分（0-100）
 */
extern "C" void sleep_analysis_build_quality(const sleep_epoch_t *epochs,
                                              const sleep_stage_result_t *stages,
                                              size_t count,
                                              sleep_quality_report_t *out_report) {
    if (out_report == nullptr) {
        return;
    }
    *out_report = {};

    if (epochs == nullptr || stages == nullptr || count == 0) {
        return;
    }

    float total_seconds = 0.0f;
    float sleep_seconds = 0.0f;
    float rem_seconds = 0.0f;
    float nrem_seconds = 0.0f;
    float wake_seconds = 0.0f;
    float resp_sum = 0.0f;
    float motion_sum = 0.0f;
    float hr_sum = 0.0f;
    float hrv_sum = 0.0f;
    
    /* 计算阶段转换次数（用于评估睡眠稳定性） */
    size_t stage_transitions = 0;
    sleep_stage_t prev_stage = SLEEP_STAGE_UNKNOWN;

    for (size_t i = 0; i < count; ++i) {
        const float dur = safe_duration(epochs[i]);
        total_seconds += dur;
        resp_sum += epochs[i].respiratory_rate_bpm;
        motion_sum += stages[i].motion_index;
        hr_sum += stages[i].heart_rate_mean;
        hrv_sum += stages[i].heart_rate_std;

        if (prev_stage != SLEEP_STAGE_UNKNOWN && stages[i].stage != prev_stage) {
            stage_transitions++;
        }
        prev_stage = stages[i].stage;

        switch (stages[i].stage) {
            case SLEEP_STAGE_WAKE:
                wake_seconds += dur;
                break;
            case SLEEP_STAGE_REM:
                rem_seconds += dur;
                sleep_seconds += dur;
                break;
            case SLEEP_STAGE_NREM:
                nrem_seconds += dur;
                sleep_seconds += dur;
                break;
            default:
                break;
        }
    }

    out_report->wake_seconds = static_cast<uint32_t>(wake_seconds);
    out_report->rem_seconds = static_cast<uint32_t>(rem_seconds);
    out_report->nrem_seconds = static_cast<uint32_t>(nrem_seconds);
    out_report->sleep_efficiency = (total_seconds > 0.0f) ? (sleep_seconds / total_seconds) : 0.0f;
    out_report->rem_ratio = (sleep_seconds > 0.0f) ? (rem_seconds / sleep_seconds) : 0.0f;
    out_report->average_resp_rate = resp_sum / static_cast<float>(count);
    out_report->average_motion = motion_sum / static_cast<float>(count);
    out_report->average_heart_rate = hr_sum / static_cast<float>(count);
    out_report->average_hrv = hrv_sum / static_cast<float>(count);

    /*
     * 睡眠评分计算（综合多个因素）:
     * 
     * 1. 睡眠效率得分 (40%权重)
     *    - 85%以上为优秀（满分）
     *    - 低于85%按比例扣分
     * 
     * 2. REM占比得分 (30%权重)
     *    - 正常成人REM应占睡眠的20-25%
     *    - 以22%为最佳，偏离越多扣分越多
     * 
     * 3. 睡眠稳定性得分 (20%权重)
     *    - 基于运动指数，运动越少越好
     * 
     * 4. 睡眠连续性得分 (10%权重)
     *    - 阶段转换次数越少越好（睡眠更稳定）
     */
    
    /* 效率得分：85%以上满分，低于50%得0分 */
    const float efficiency_pct = out_report->sleep_efficiency * 100.0f;
    float efficiency_score;
    if (efficiency_pct >= 85.0f) {
        efficiency_score = 100.0f;
    } else if (efficiency_pct >= 50.0f) {
        efficiency_score = (efficiency_pct - 50.0f) / 35.0f * 100.0f;
    } else {
        efficiency_score = 0.0f;
    }
    
    /* REM占比得分：22%最佳，偏离过大扣分 */
    const float rem_pct = out_report->rem_ratio * 100.0f;
    const float rem_deviation = std::fabs(rem_pct - 22.0f);
    float rem_score;
    if (rem_deviation <= 5.0f) {
        rem_score = 100.0f;  /* 17-27%范围内满分 */
    } else if (rem_deviation <= 15.0f) {
        rem_score = 100.0f - (rem_deviation - 5.0f) * 5.0f;  /* 每偏离1%扣5分 */
    } else {
        rem_score = 50.0f - (rem_deviation - 15.0f) * 2.5f;
    }
    rem_score = clamp(rem_score, 0.0f, 100.0f);
    
    /* 稳定性得分：运动指数越低越好 */
    /* 体动参数范围 0-100，< 10 为非常稳定，> 50 为不稳定 */
    float stability_score;
    if (out_report->average_motion <= 10.0f) {
        stability_score = 100.0f;
    } else if (out_report->average_motion <= 50.0f) {
        stability_score = 100.0f - (out_report->average_motion - 10.0f) * 1.875f;  /* (100-25)/(50-10) */
    } else {
        stability_score = 25.0f - (out_report->average_motion - 50.0f) * 0.5f;
    }
    stability_score = clamp(stability_score, 0.0f, 100.0f);
    
    /* 连续性得分：阶段转换越少越好 */
    /* 每小时4-6次转换是正常的（约60个epoch，每10-15个转换一次） */
    const float transitions_per_hour = (total_seconds > 0.0f) 
        ? (static_cast<float>(stage_transitions) / (total_seconds / 3600.0f)) 
        : 0.0f;
    float continuity_score;
    if (transitions_per_hour <= 6.0f) {
        continuity_score = 100.0f;
    } else if (transitions_per_hour <= 15.0f) {
        continuity_score = 100.0f - (transitions_per_hour - 6.0f) * 6.0f;
    } else {
        continuity_score = 40.0f;
    }
    continuity_score = clamp(continuity_score, 0.0f, 100.0f);

    /* 综合评分（加权平均） */
    const float weighted = 0.40f * efficiency_score + 
                           0.30f * rem_score + 
                           0.20f * stability_score +
                           0.10f * continuity_score;
    out_report->sleep_score = clamp(weighted, 0.0f, 100.0f);
}
