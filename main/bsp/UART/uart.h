/**
 ****************************************************************************************************
 * @file        uart.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-12-29
 * @brief       UART0驱动代码
 ****************************************************************************************************/

#ifndef __UART_H
#define __UART_H

#include "driver/uart.h"
#include "driver/uart_select.h"
#include "driver/gpio.h"

/* 引脚和串口定义 */
/* 注意：ESP32-S3 使用 Octal PSRAM 时，GPIO35/36/37 被 PSRAM 占用，不能用作 UART！
 * 请根据实际硬件连接修改以下引脚定义 */
#define USART_UX            UART_NUM_1
#define USART_TX_GPIO_PIN   GPIO_NUM_4   /* TODO: 根据实际雷达模块连接修改 */
#define USART_RX_GPIO_PIN   GPIO_NUM_5   /* TODO: 根据实际雷达模块连接修改 */

/* 串口接收相关定义 */
#define RX_BUF_SIZE         1024        /* 环形缓冲区大小(单位字节) */

/* 函数声明 */
void uart0_init(uint32_t baudrate);

#endif