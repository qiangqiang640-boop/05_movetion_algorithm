#ifndef MOTION_DETECT_H
#define MOTION_DETECT_H

#include <stdint.h>
#include <stdbool.h>

/* ============ 算法参数 ============ */
#define MOTION_W            80      // 降采样后的图像宽度（太小测不到，太大算得慢）
#define MOTION_H            60      // 降采样后的图像高度
#define MOTION_THRESHOLD    25      // 像素灰度差阈值
#define MIN_MOTION_AREA     30      // 最小运动区域（降采样后的像素个数）
#define DEAD_ZONE_PIX       5       // 死区（降采样后的像素）
#define KP_PAN              0.8f    // 水平比例系数
#define KP_TILT             0.8f    // 垂直比例系数

/* ============ 云台状态 ============ */
typedef struct {
    int pan_angle;      // 当前水平角度
    int tilt_angle;     // 当前垂直角度
} gimbal_state_t;

/**
 * @brief 初始化运动检测，分配 PSRAM 缓冲
 */
void motion_detect_init(void);

/**
 * @brief 处理一帧 RGB565 图像，检测运动并更新云台
 * @param rgb565_data  已解码的 RGB565 图像数据
 * @param src_w        原图宽度（如 320）
 * @param src_h        原图高度（如 240）
 * @return true 表示检测到目标并发送了指令
 */
bool motion_detect_process(const uint8_t *rgb565_data, int src_w, int src_h);

/**
 * @brief 获取当前云台状态
 */
void motion_get_gimbal(gimbal_state_t *state);

/**
 * @brief 复位云台到中心
 */
void motion_center(void);

#endif