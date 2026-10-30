#ifndef UART_COMM_H
#define UART_COMM_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ============ 硬件配置 ============ */
#define UART_COMM_PORT      UART_NUM_1
#define UART_COMM_TX_PIN    GPIO_NUM_38
#define UART_COMM_BAUD      115200

/* ============ 舵机角度范围 ============ */
#define SERVO_ANGLE_MIN     60
#define SERVO_ANGLE_MAX     120

/* ============ 舵机编号 ============ */
#define SERVO_PAN           1   // 水平舵机
#define SERVO_TILT          2   // 垂直舵机

/**
 * @brief 初始化 UART 通信
 */
esp_err_t uart_comm_init(void);

/**
 * @brief 发送单条舵机角度指令
 * @param servo_id 舵机编号 (1=水平, 2=垂直)
 * @param angle    角度值 (自动限幅到 60~120)
 */
void uart_send_servo(uint8_t servo_id, int angle);

/**
 * @brief 同时发送水平和垂直角度
 */
void uart_send_pan_tilt(int pan_angle, int tilt_angle);

/**
 * @brief 复位到中位 (90, 90)
 */
void uart_send_center(void);

/**
 * @brief 通信测试：依次发送一系列角度，验证 STM32 和舵机响应
 *        会阻塞约 5 秒
 */
void uart_comm_test(void);

#endif