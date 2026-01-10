/*
 * 函数说明
 * radar_protocol_build_frame 函数会自动处理以下细节：
 *
 * 填充帧头 0x53 0x59
 * 填充控制字 (参数传入)
 * 填入你传入的 命令字
 * 自动计算并填入 数据长度 (大端序)
 * 复制 数据 内容
 * 自动计算并填入 校验码 (前面所有字节之和的低8位)
 * 填充帧尾 0x54 0x43
 */
#include "radar_protocol.h"

int radar_protocol_build_frame(uint8_t ctrl, uint8_t cmd, const uint8_t *data, uint16_t data_len, uint8_t *out_buf, uint16_t *out_len)
{
    uint16_t total_len = RADAR_MIN_FRAME_LEN + data_len;
    
    if (*out_len < total_len) {
        return -1; // Buffer too small
    }

    uint16_t idx = 0;
    uint32_t checksum = 0;

    // 1. 帧头
    out_buf[idx++] = RADAR_FRAME_HEADER_1;
    out_buf[idx++] = RADAR_FRAME_HEADER_2;

    // 2. 控制字
    out_buf[idx++] = ctrl;

    // 3. 命令字
    out_buf[idx++] = cmd;

    // 4. 长度标识 (2 Byte), 使用大端序 (High Byte First)
    out_buf[idx++] = (data_len >> 8) & 0xFF;
    out_buf[idx++] = data_len & 0xFF;

    // 5. 数据
    if (data != NULL && data_len > 0) {
        for (uint16_t i = 0; i < data_len; i++) {
            out_buf[idx++] = data[i];
        }
    }

    // 计算校验码 (前面所有字节之和的低8位)
    // 前面所有字节包括: Head(2) + Ctrl(1) + Cmd(1) + Len(2) + Data(n)
    // 当前 idx 指向校验码应该存放的位置，也就是数据的下一个字节
    // 所以我们需要计算从 0 到 idx-1 的和
    for (uint16_t i = 0; i < idx; i++) {
        checksum += out_buf[i];
    }

    // 6. 校验码
    out_buf[idx++] = checksum & 0xFF;

    // 7. 帧尾
    out_buf[idx++] = RADAR_FRAME_TAIL_1;
    out_buf[idx++] = RADAR_FRAME_TAIL_2;

    // 更新实际长度
    *out_len = idx;

    return 0;
}

int radar_protocol_pack_heart_rate_switch(uint8_t enable, uint8_t *out_buf, uint16_t *out_len)
{
    uint8_t data = enable ? HEART_RATE_ON : HEART_RATE_OFF;
    return radar_protocol_build_frame(CTRL_HEART_RATE, CMD_HEART_RATE_SWITCH, &data, 1, out_buf, out_len);
}

int radar_protocol_pack_motion_query(uint8_t *out_buf, uint16_t *out_len)
{
    /* 
     * 体动参数查询帧: 53 59 80 83 00 01 0F sum 54 43
     * 控制字: 0x80 (人体存在)
     * 命令字: 0x83 (体动参数)
     * 数据: 0x0F (查询标识)
     */
    uint8_t data = DATA_QUERY;
    return radar_protocol_build_frame(CTRL_HUMAN_PRESENCE, CMD_BODY_MOVEMENT, &data, 1, out_buf, out_len);
}

int radar_protocol_parse_frame(const uint8_t *buffer, uint16_t len, uint8_t *out_ctrl, uint8_t *out_cmd, uint8_t **out_data, uint16_t *out_data_len)
{
    if (len < RADAR_MIN_FRAME_LEN) {
        return -2; // Length too short
    }

    // 1. Check Header
    if (buffer[0] != RADAR_FRAME_HEADER_1 || buffer[1] != RADAR_FRAME_HEADER_2) {
        return -1; // Invalid Header
    }

    // 2. Get Length
    uint16_t payload_len = (buffer[4] << 8) | buffer[5];
    uint16_t total_len = RADAR_MIN_FRAME_LEN + payload_len;

    if (len < total_len) {
        return -2; // Incomplete frame
    }

    // 3. Check Tail
    if (buffer[total_len - 2] != RADAR_FRAME_TAIL_1 || buffer[total_len - 1] != RADAR_FRAME_TAIL_2) {
        return -1; // Invalid Tail
    }

    // 4. Check Checksum
    uint32_t checksum = 0;
    // Sum from Header to Data (inclusive), excluding Checksum and Tail
    // Checksum is at index: total_len - 3
    for (uint16_t i = 0; i < total_len - 3; i++) {
        checksum += buffer[i];
    }
    
    if ((checksum & 0xFF) != buffer[total_len - 3]) {
        return -1; // Checksum failed
    }

    // 5. Output results
    *out_ctrl = buffer[2];
    *out_cmd = buffer[3];
    *out_data_len = payload_len;
    if (payload_len > 0) {
        *out_data = (uint8_t *)&buffer[6];
    } else {
        *out_data = NULL;
    }

    return 0;
}
