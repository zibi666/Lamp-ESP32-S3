#ifndef RADAR_PROTOCOL_H
#define RADAR_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 帧头
#define RADAR_FRAME_HEADER_1  0x53
#define RADAR_FRAME_HEADER_2  0x59

// 控制字 (心率监测功能)
#define CTRL_HEART_RATE 0x85
// 控制字 (人体存在/运动信息)
#define CTRL_HUMAN_PRESENCE 0x80
// 控制字 (呼吸监测)
#define CTRL_BREATH     0x81
// 控制字 (睡眠监测)
#define CTRL_SLEEP      0x84

// 帧尾
#define RADAR_FRAME_TAIL_1    0x54
#define RADAR_FRAME_TAIL_2    0x43

// 最小帧长度 (Header(2) + Ctrl(1) + Cmd(1) + Len(2) + Checksum(1) + Tail(2))
#define RADAR_MIN_FRAME_LEN   9

// 命令字 - 心率
#define CMD_HEART_RATE_SWITCH 0x00
#define CMD_HEART_RATE_REPORT 0x02

// 命令字 - 人体存在/运动 (CTRL_HUMAN_PRESENCE 0x80)
#define CMD_MOTION_INFO       0x02 // 运动信息 (静止/活跃)
#define CMD_BODY_MOVEMENT     0x83 // 体动参数 (查询命令字)
#define CMD_BODY_MOVEMENT_RPT 0x83 // 体动参数回复 (数据包含1B标识)
#define CMD_HUMAN_DISTANCE    0x04 // 人体距离
#define CMD_HUMAN_ORIENTATION 0x05 // 人体方位

// 查询命令数据标识
#define DATA_QUERY            0x0F // 查询指令数据
#define DATA_REPORT           0x1B // 上报数据标识

// 命令字 - 呼吸 (CTRL_BREATH 0x81)
#define CMD_BREATH_VALUE      0x02 // 呼吸数值

// 命令字 - 睡眠 (CTRL_SLEEP 0x84)
#define CMD_SLEEP_COMPREHENSIVE 0x0C // 睡眠综合状态上报
#define CMD_SLEEP_QUALITY       0x0D // 睡眠质量分析上报

// 开关状态
#define HEART_RATE_ON  0x01
#define HEART_RATE_OFF 0x00

/**
 * @brief 构建协议帧
 */
int radar_protocol_build_frame(uint8_t ctrl, uint8_t cmd, const uint8_t *data, uint16_t data_len, uint8_t *out_buf, uint16_t *out_len);

/**
 * @brief 构建心率监测开关指令帧
 */
int radar_protocol_pack_heart_rate_switch(uint8_t enable, uint8_t *out_buf, uint16_t *out_len);

/**
 * @brief 解析协议帧
 */
int radar_protocol_parse_frame(const uint8_t *buffer, uint16_t len, uint8_t *out_ctrl, uint8_t *out_cmd, uint8_t **out_data, uint16_t *out_data_len);

/**
 * @brief 构建体动参数查询指令帧
 */
int radar_protocol_pack_motion_query(uint8_t *out_buf, uint16_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // RADAR_PROTOCOL_H
