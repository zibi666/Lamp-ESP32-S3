#include "app_controller.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "http_request.h"
#include "radar_protocol.h"
#include "sleep_analysis.h"
#include "uart.h"
#include "smart_light_controller.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "app_ctrl";

static int g_heart_rate = 0;
static int g_breathing_rate = 0;
static float g_motion_index = 0.0f;

/* 睡眠状态 */
typedef enum {
  SLEEP_MONITORING = 0, /* 监测中（未入睡或已醒来） */
  SLEEP_SETTLING,       /* 入睡观察期 */
  SLEEP_SLEEPING        /* 睡眠中 */
} sleep_state_t;

static sleep_state_t g_sleep_state = SLEEP_MONITORING;

#define EPOCH_MS 30000U          /* 每30秒分析一次 */
#define ONSET_WINDOW_EPOCHS 10U  /* 入睡观察期: 10个epoch = 5分钟 */
#define MOTION_SLEEP_MAX 15.0f   /* 入睡体动阈值(0-100)，更严格 */
#define RESP_SLEEP_MIN 8.0f      /* 入睡呼吸最小值 */
#define RESP_SLEEP_MAX 22.0f     /* 入睡呼吸最大值，更严格 */
#define MOTION_WAKE_THRESH 30.0f /* 清醒体动阈值，更敏感 */
#define HR_WAKE_THRESH 80.0f     /* 心率高于此值认为清醒 */
#define HR_DROP_REQUIRED 5.0f    /* 心率需下降至少5bpm */
#define SENSOR_WARMUP_EPOCHS 2U
#define RADAR_SAMPLES_PER_EPOCH 10U
#define THRESH_WINDOW_EPOCHS 40U

/* 入睡观察计数器 */
static uint32_t g_settling_count = 0;
static float g_baseline_hr = 0.0f; /* 基线心率（开始监测时的心率） */

#define MAX_SLEEP_EPOCHS 512
static sleep_epoch_t g_epochs[MAX_SLEEP_EPOCHS];
static sleep_stage_result_t g_stage_results[MAX_SLEEP_EPOCHS];
static size_t g_epoch_count = 0;
static sleep_thresholds_t g_thresholds = {0};
static sleep_quality_report_t g_report = {0};
static volatile sleep_stage_t g_current_stage = SLEEP_STAGE_UNKNOWN;

static bool s_started = false;

/* 智能灯光控制器 */
static smart_light_context_t g_smart_light_ctx = {0};

static QueueHandle_t s_health_queue = NULL;
#define HEALTH_QUEUE_LEN 16

static portMUX_TYPE s_radar_sample_mux = portMUX_INITIALIZER_UNLOCKED;
static radar_sample_t s_radar_sample_ring[RADAR_SAMPLES_PER_EPOCH];
static size_t s_radar_sample_count = 0;
static size_t s_radar_sample_head = 0;

static void radar_sample_push(uint8_t heart_rate_bpm,
                              uint8_t respiratory_rate_bpm,
                              uint8_t motion_level) {
  radar_sample_t sample = {
      .heart_rate_bpm = heart_rate_bpm,
      .respiratory_rate_bpm = respiratory_rate_bpm,
      .motion_level = motion_level,
      .timestamp = (uint32_t)time(NULL),
  };

  portENTER_CRITICAL(&s_radar_sample_mux);
  s_radar_sample_ring[s_radar_sample_head] = sample;
  s_radar_sample_head = (s_radar_sample_head + 1) % RADAR_SAMPLES_PER_EPOCH;
  if (s_radar_sample_count < RADAR_SAMPLES_PER_EPOCH) {
    s_radar_sample_count++;
  }
  portEXIT_CRITICAL(&s_radar_sample_mux);
}

/* 睡眠阶段转字符串 */
static const char *stage_to_str(sleep_stage_t s) {
  switch (s) {
  case SLEEP_STAGE_WAKE:
    return "清醒";
  case SLEEP_STAGE_REM:
    return "REM睡眠";
  case SLEEP_STAGE_NREM:
    return "深度睡眠";
  default:
    return "未知";
  }
}

static const char *stage_to_cloud_str(sleep_stage_t s) {
  switch (s) {
  case SLEEP_STAGE_WAKE:
    return "WAKE";
  case SLEEP_STAGE_REM:
    return "REM";
  case SLEEP_STAGE_NREM:
    return "NREM";
  default:
    return "UNKNOWN";
  }
}

/* 睡眠质量等级 */
static const char *quality_to_str(float score) {
  if (score >= 85.0f)
    return "优秀";
  if (score >= 70.0f)
    return "良好";
  if (score >= 50.0f)
    return "一般";
  return "较差";
}

static void upload_data_task(void *pvParameters) {
  while (1) {
    if (!s_health_queue) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    health_data_t data = {0};
    if (xQueueReceive(s_health_queue, &data, pdMS_TO_TICKS(1000)) == pdTRUE) {
      if (data.heart_rate <= 0 && data.breathing_rate <= 0) {
        continue;
      }
      printf("正在上传数据 - 心率:%d 呼吸:%d 体动:%.1f 阶段:%s\n",
             data.heart_rate, data.breathing_rate, data.motion_index,
             data.sleep_status);
      esp_err_t err = http_send_health_data(&data);
      if (err != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        (void)xQueueSend(s_health_queue, &data, 0);
      }
    }
  }
}

static void sleep_stage_task(void *pvParameters) {
  const TickType_t period = pdMS_TO_TICKS(EPOCH_MS);
  uint32_t warmup_left = SENSOR_WARMUP_EPOCHS;

  printf("\n========== 睡眠监测已启动 ==========\n");
  printf("入睡判定条件: 连续%u分钟低体动(<%.0f) + 心率下降\n",
         ONSET_WINDOW_EPOCHS / 2, MOTION_SLEEP_MAX);

  while (1) {
    radar_sample_t samples[RADAR_SAMPLES_PER_EPOCH] = {0};
    size_t copied = 0;
    portENTER_CRITICAL(&s_radar_sample_mux);
    if (s_radar_sample_count >= RADAR_SAMPLES_PER_EPOCH) {
      for (size_t i = 0; i < RADAR_SAMPLES_PER_EPOCH; ++i) {
        const size_t idx = (s_radar_sample_head + i) % RADAR_SAMPLES_PER_EPOCH;
        samples[i] = s_radar_sample_ring[idx];
      }
      copied = RADAR_SAMPLES_PER_EPOCH;
    }
    portEXIT_CRITICAL(&s_radar_sample_mux);

    if (copied < RADAR_SAMPLES_PER_EPOCH) {
      vTaskDelay(period);
      continue;
    }

    size_t valid_rr_count = 0;
    size_t valid_hr_count = 0;
    float motion_sum = 0.0f;
    float motion_max = 0.0f;
    for (size_t i = 0; i < RADAR_SAMPLES_PER_EPOCH; ++i) {
      const uint8_t rr = samples[i].respiratory_rate_bpm;
      const uint8_t hr = samples[i].heart_rate_bpm;
      const float mv = (float)samples[i].motion_level;
      if (rr > 0 && rr <= 35)
        valid_rr_count++;
      if (hr >= 60 && hr <= 120)
        valid_hr_count++;
      motion_sum += mv;
      if (mv > motion_max)
        motion_max = mv;
    }
    const float motion_avg = motion_sum / (float)RADAR_SAMPLES_PER_EPOCH;

    sleep_epoch_t epoch = {0};
    const size_t epoch_n =
        sleep_analysis_aggregate_samples(samples, copied, &epoch, 1);
    if (epoch_n == 0) {
      vTaskDelay(period);
      continue;
    }
    if (valid_rr_count == 0) {
      epoch.respiratory_rate_bpm = 0.0f;
    }
    if (valid_hr_count == 0) {
      epoch.heart_rate_mean = 0.0f;
      epoch.heart_rate_std = 0.0f;
    }

    epoch.motion_index = motion_max;

    const float hr_avg = epoch.heart_rate_mean;
    const float rr_avg = epoch.respiratory_rate_bpm;

    const bool has_valid_epoch = (valid_hr_count > 0) || (valid_rr_count > 0);
    if (warmup_left > 0) {
      if (has_valid_epoch) {
        warmup_left--;
      }
      vTaskDelay(period);
      continue;
    }

    if (!has_valid_epoch) {
      vTaskDelay(period);
      continue;
    }

    /* 2. 存储epoch数据 */
    if (g_epoch_count >= MAX_SLEEP_EPOCHS) {
      memmove(&g_epochs[0], &g_epochs[1],
              (MAX_SLEEP_EPOCHS - 1) * sizeof(sleep_epoch_t));
      memmove(&g_stage_results[0], &g_stage_results[1],
              (MAX_SLEEP_EPOCHS - 1) * sizeof(sleep_stage_result_t));
      g_epoch_count = MAX_SLEEP_EPOCHS - 1;
    }
    g_epochs[g_epoch_count] = epoch;
    g_epoch_count++;

    /* 3. 入睡状态机 */
    sleep_stage_t current_stage = SLEEP_STAGE_WAKE;
    bool is_quiet = (motion_avg < MOTION_SLEEP_MAX) &&
                    (rr_avg >= RESP_SLEEP_MIN && rr_avg <= RESP_SLEEP_MAX) &&
                    (rr_avg > 0); /* 呼吸数据必须有效 */
    bool is_active =
        (motion_avg > MOTION_WAKE_THRESH) || (hr_avg > HR_WAKE_THRESH);

    switch (g_sleep_state) {
    case SLEEP_MONITORING:
      /* 记录基线心率 */
      if (g_baseline_hr < 1.0f && hr_avg > 50.0f) {
        g_baseline_hr = hr_avg;
        printf("[睡眠] 基线心率: %.0f bpm\n", g_baseline_hr);
      }

      if (is_quiet && !is_active) {
        /* 开始入睡观察 */
        g_sleep_state = SLEEP_SETTLING;
        g_settling_count = 1;
        printf("[睡眠] 进入观察期 (%lu/%u)\n", (unsigned long)g_settling_count,
               ONSET_WINDOW_EPOCHS);
      }
      break;

    case SLEEP_SETTLING:
      if (is_active) {
        /* 活动太大，重置 */
        g_sleep_state = SLEEP_MONITORING;
        g_settling_count = 0;
        printf("[睡眠] 观察期中断(体动%.1f/心率%.0f)，重新监测\n", motion_avg,
               hr_avg);
      } else if (is_quiet) {
        g_settling_count++;
        printf("[睡眠] 观察期进行中 (%lu/%u)\n",
               (unsigned long)g_settling_count, ONSET_WINDOW_EPOCHS);

        /* 检查是否满足入睡条件 */
        if (g_settling_count >= ONSET_WINDOW_EPOCHS) {
          /* 检查心率是否有下降趋势 */
          float hr_drop = g_baseline_hr - hr_avg;
          if (hr_drop >= HR_DROP_REQUIRED || hr_avg < 75.0f) {
            g_sleep_state = SLEEP_SLEEPING;
            printf("[睡眠] ★ 确认入睡! 心率从%.0f降至%.0f (降%.0f)\n",
                   g_baseline_hr, hr_avg, hr_drop);
          } else {
            printf("[睡眠] 体动低但心率未下降(%.0f→%.0f)，继续观察\n",
                   g_baseline_hr, hr_avg);
            /* 保持在观察期，不重置计数 */
          }
        }
      } else {
        /* 不够安静，减少计数 */
        if (g_settling_count > 0)
          g_settling_count--;
        if (g_settling_count == 0) {
          g_sleep_state = SLEEP_MONITORING;
          printf("[睡眠] 观察期结束，未入睡\n");
        }
      }
      break;

    case SLEEP_SLEEPING:
      if (is_active) {
        /* 醒来了 */
        g_sleep_state = SLEEP_MONITORING;
        g_settling_count = 0;
        g_baseline_hr = hr_avg; /* 重新设置基线 */
        printf("[睡眠] ★ 检测到觉醒 (体动%.1f/心率%.0f)\n", motion_avg, hr_avg);
      }
      break;
    }

    /* 4. 睡眠阶段分析（仅在确认睡眠后） */
    static uint32_t s_wake_count = 0; /* 连续WAKE计数器 */

    if (g_sleep_state == SLEEP_SLEEPING &&
        g_epoch_count >= ONSET_WINDOW_EPOCHS) {
      size_t thr_count = g_epoch_count;
      size_t thr_start = 0;
      if (thr_count > THRESH_WINDOW_EPOCHS) {
        thr_start = thr_count - THRESH_WINDOW_EPOCHS;
        thr_count = THRESH_WINDOW_EPOCHS;
      }
      sleep_analysis_compute_thresholds(&g_epochs[thr_start], thr_count,
                                        &g_thresholds);
      sleep_analysis_detect_stages(g_epochs, g_epoch_count, &g_thresholds,
                                   g_stage_results);
      current_stage = g_stage_results[g_epoch_count - 1].stage;

      /* 如果论文算法判定为WAKE，检查是否真的觉醒 */
      if (current_stage == SLEEP_STAGE_WAKE) {
        s_wake_count++;

        if (s_wake_count >= 3) {
          /* 连续3次WAKE（1.5分钟），真的觉醒了 */
          g_sleep_state = SLEEP_MONITORING;
          g_settling_count = 0;
          g_baseline_hr = hr_avg;
          s_wake_count = 0;
          printf("[睡眠] ★ 算法检测到觉醒\n");
        } else {
          /* 可能是短暂微觉醒，保持睡眠状态，标记为浅睡 */
          current_stage = SLEEP_STAGE_NREM;
          printf("[睡眠] 微觉醒信号 (%lu/3)，继续监测\n",
                 (unsigned long)s_wake_count);
        }
      } else {
        /* 非WAKE，重置觉醒计数 */
        s_wake_count = 0;
      }
    } else {
      s_wake_count = 0; /* 未在睡眠状态，重置计数 */
      /* 未入睡，全部标记为清醒 */
      for (size_t i = 0; i < g_epoch_count; ++i) {
        g_stage_results[i].stage = SLEEP_STAGE_WAKE;
        g_stage_results[i].respiratory_rate_bpm =
            g_epochs[i].respiratory_rate_bpm;
        g_stage_results[i].motion_index = g_epochs[i].motion_index;
        g_stage_results[i].heart_rate_mean = g_epochs[i].heart_rate_mean;
        g_stage_results[i].heart_rate_std = g_epochs[i].heart_rate_std;
      }
    }

    /* 5. 计算睡眠质量报告 */
    sleep_analysis_build_quality(g_epochs, g_stage_results, g_epoch_count,
                                 &g_report);

    if (s_health_queue && g_epoch_count > 0) {
      const sleep_stage_result_t *last = &g_stage_results[g_epoch_count - 1];
      health_data_t data = {0};
      data.heart_rate = (int)(last->heart_rate_mean + 0.5f);
      data.breathing_rate = (int)(last->respiratory_rate_bpm + 0.5f);
      snprintf(data.sleep_status, sizeof(data.sleep_status), "%s",
               stage_to_cloud_str(last->stage));
      data.motion_index = last->motion_index;

      if (data.heart_rate <= 0 && data.breathing_rate <= 0) {
        vTaskDelay(period);
        continue;
      }

      if (xQueueSend(s_health_queue, &data, 0) != pdTRUE) {
        health_data_t dropped = {0};
        (void)xQueueReceive(s_health_queue, &dropped, 0);
        (void)xQueueSend(s_health_queue, &data, 0);
      }
    }

    /* 6. 更新智能灯光控制器 */
    smart_light_update(&g_smart_light_ctx, current_stage, g_sleep_state, 
                      motion_avg, (uint32_t)time(NULL));
    
    /* 7. 输出睡眠状态 */
    g_current_stage = current_stage;
    const char *state_str = (g_sleep_state == SLEEP_MONITORING) ? "监测中"
                            : (g_sleep_state == SLEEP_SETTLING) ? "观察期"
                                                                : "睡眠中";

    printf("\n╔════════════════════════════════════════╗\n");
    printf("║           睡眠监测报告                  ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║ 监测状态: %-28s ║\n", state_str);
    printf("║ 睡眠阶段: %-28s ║\n", stage_to_str(current_stage));
    printf("║ 呼吸频率: %-3d 次/分                     ║\n",
           (int)(rr_avg + 0.5f));
    printf("║ 心率:     %-3d bpm                       ║\n",
           (int)(hr_avg + 0.5f));
    printf("║ 体动指数: %-5.1f                         ║\n", motion_avg);
    printf("╠════════════════════════════════════════╣\n");

    if (g_sleep_state == SLEEP_SLEEPING) {
      printf("║ 睡眠评分: %-5.1f (%s)                 ║\n",
             g_report.sleep_score, quality_to_str(g_report.sleep_score));
      printf("║ 睡眠效率: %-5.1f%%                       ║\n",
             g_report.sleep_efficiency * 100.0f);
      printf("║ REM占比:  %-5.1f%%                       ║\n",
             g_report.rem_ratio * 100.0f);
      printf("║ 深睡时长: %-4lu 秒                      ║\n",
             (unsigned long)g_report.nrem_seconds);
      printf("║ 平均心率: %-5.1f bpm                    ║\n",
             g_report.average_heart_rate);
    } else if (g_sleep_state == SLEEP_SETTLING) {
      printf("║ 入睡观察: %lu/%u (%.1f分钟)             ║\n",
             (unsigned long)g_settling_count, ONSET_WINDOW_EPOCHS,
             g_settling_count * 0.5f);
    } else {
      printf("║ [等待入睡信号...]                       ║\n");
    }
    
    /* 输出智能灯光状态 */
    uint8_t light_brightness = smart_light_get_brightness(&g_smart_light_ctx);
    if (light_brightness > 0) {
      printf("╠════════════════════════════════════════╣\n");
      printf("║ 💡 智能灯光: %s (亮度: %d)         ║\n",
             smart_light_get_state_str(&g_smart_light_ctx), light_brightness);
    }
    
    printf("╚════════════════════════════════════════╝\n");

    vTaskDelay(period);
  }
}

sleep_stage_t app_controller_get_current_sleep_stage(void) {
  return g_current_stage;
}

static void uart_rx_task(void *pvParameters) {
  uint8_t rx_buf[128] = {0};
  uint16_t len = 0;

  /* 发送开启心率监测指令 */
  uint8_t tx_buf[32];
  uint16_t tx_len = sizeof(tx_buf);
  if (radar_protocol_pack_heart_rate_switch(1, tx_buf, &tx_len) == 0) {
    uart_write_bytes(USART_UX, (const char *)tx_buf, tx_len);
    printf("已发送心率使能命令\n");
  }

  /* 体动查询定时器 (每3秒查询一次) */
  TickType_t last_motion_query = xTaskGetTickCount();
  const TickType_t motion_query_period = pdMS_TO_TICKS(3000);

  while (1) {
    /* 定时发送体动参数查询 */
    if ((xTaskGetTickCount() - last_motion_query) >= motion_query_period) {
      tx_len = sizeof(tx_buf);
      if (radar_protocol_pack_motion_query(tx_buf, &tx_len) == 0) {
        uart_write_bytes(USART_UX, (const char *)tx_buf, tx_len);
      }
      last_motion_query = xTaskGetTickCount();
    }

    uart_get_buffered_data_len(USART_UX, (size_t *)&len);

    if (len > 0) {
      int rx_len = uart_read_bytes(
          USART_UX, rx_buf, (len > sizeof(rx_buf) ? sizeof(rx_buf) : len), 100);
      if (rx_len > 0) {
        uint8_t ctrl, cmd;
        uint8_t *data_ptr;
        uint16_t data_len;

        int parse_res = radar_protocol_parse_frame(rx_buf, rx_len, &ctrl, &cmd,
                                                   &data_ptr, &data_len);

        if (parse_res == 0) {
          /*
           * 只处理三种数据：心率、呼吸、体动
           * 其他帧静默忽略
           */

          /* 心率上报: 5359 85 02 0001 1B [心率] sum 5443 */
          if (ctrl == CTRL_HEART_RATE && cmd == CMD_HEART_RATE_REPORT) {
            /* 数据格式: 1B + 心率值 */
            if (data_len >= 1) {
              /* 检查是否有0x1B前缀 */
              uint8_t heart_rate = (data_len == 2 && data_ptr[0] == DATA_REPORT)
                                       ? data_ptr[1]
                                       : data_ptr[0];
              if (heart_rate >= 60 && heart_rate <= 120) {
                g_heart_rate = heart_rate;
                printf("心率: %d bpm\n", heart_rate);
              }
            }
          }
          /* 呼吸上报: 5359 81 02 0001 1B [呼吸] sum 5443 */
          else if (ctrl == CTRL_BREATH && cmd == CMD_BREATH_VALUE) {
            if (data_len >= 1) {
              uint8_t breath = (data_len == 2 && data_ptr[0] == DATA_REPORT)
                                   ? data_ptr[1]
                                   : data_ptr[0];
              if (breath <= 35) {
                g_breathing_rate = breath;
                if (breath > 0) {
                  printf("呼吸频率: %d 次/分\n", breath);
                }
              }
            }
          }
          /* 体动回复: 5359 80 83 0001 1B [体动] sum 5443 */
          else if (ctrl == CTRL_HUMAN_PRESENCE && cmd == CMD_BODY_MOVEMENT) {
            if (data_len >= 1) {
              uint8_t movement = (data_len == 2 && data_ptr[0] == DATA_REPORT)
                                     ? data_ptr[1]
                                     : data_ptr[0];
              if (movement <= 100) {
                g_motion_index = (float)movement;
                printf("体动参数: %d\n", movement);
                const uint8_t hr = (g_heart_rate >= 60 && g_heart_rate <= 120)
                                       ? (uint8_t)g_heart_rate
                                       : 0;
                const uint8_t rr =
                    (g_breathing_rate > 0 && g_breathing_rate <= 35)
                        ? (uint8_t)g_breathing_rate
                        : 0;
                radar_sample_push(hr, rr, movement);
              }
            }
          }
          /* 其他帧静默忽略，不打印 */
        }
        /* 解析失败也静默忽略 */
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

esp_err_t app_controller_start(void) {
  if (s_started) {
    return ESP_OK;
  }

  s_health_queue = xQueueCreate(HEALTH_QUEUE_LEN, sizeof(health_data_t));
  if (!s_health_queue) {
    ESP_LOGE(TAG, "create health queue failed");
    return ESP_FAIL;
  }

  /* 初始化智能灯光控制器 */
  smart_light_init(&g_smart_light_ctx);

  BaseType_t r1 =
      xTaskCreate(upload_data_task, "upload_data_task", 4096, NULL, 5, NULL);
  BaseType_t r2 =
      xTaskCreate(sleep_stage_task, "sleep_stage_task", 4096, NULL, 5, NULL);
  BaseType_t r3 =
      xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 5, NULL);

  if (r1 != pdPASS || r2 != pdPASS || r3 != pdPASS) {
    ESP_LOGE(TAG, "create task failed: %ld %ld %ld", (long)r1, (long)r2,
             (long)r3);
    return ESP_FAIL;
  }

  s_started = true;
  return ESP_OK;
}
