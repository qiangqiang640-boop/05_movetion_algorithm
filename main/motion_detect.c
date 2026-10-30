#include "motion_detect.h"
#include "uart_comm.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "MOTION";

/* ============ 静态变量 ============ */
static gimbal_state_t s_gimbal = { 90, 90 };
static uint8_t *s_prev_gray = NULL;   // 上一帧灰度图（降采样后）
static uint8_t *s_curr_gray = NULL;   // 当前帧灰度图（降采样后）
static bool s_first_frame = true;

/* ============ 初始化 ============ */
void motion_detect_init(void)
{
    if (!s_prev_gray) {
        s_prev_gray = (uint8_t *)heap_caps_malloc(MOTION_W * MOTION_H, MALLOC_CAP_SPIRAM);
        s_curr_gray = (uint8_t *)heap_caps_malloc(MOTION_W * MOTION_H, MALLOC_CAP_SPIRAM);
        if (!s_prev_gray || !s_curr_gray) {
            ESP_LOGE(TAG, "灰度缓冲分配失败");
        }
    }
    s_gimbal.pan_angle = 90;
    s_gimbal.tilt_angle = 90;
    s_first_frame = true;
    ESP_LOGI(TAG, "运动检测初始化完成 (%dx%d)", MOTION_W, MOTION_H);
}

/* ============ 核心处理 ============ */
bool motion_detect_process(const uint8_t *rgb565_data, int src_w, int src_h)
{
    // 1. 检查是否初始化
    if (!s_prev_gray || !s_curr_gray) {
        static int err_cnt = 0;
        if (err_cnt++ % 50 == 0) {
            ESP_LOGE(TAG, "❌ 错误：motion_detect 未初始化！");
        }
        return false;
    }

    // 2. 降采样并转灰度
    const uint16_t *src = (const uint16_t *)rgb565_data;
    int x_step = src_w / MOTION_W;
    int y_step = src_h / MOTION_H;

    for (int y = 0; y < MOTION_H; y++) {
        for (int x = 0; x < MOTION_W; x++) {
            int src_idx = (y * y_step) * src_w + (x * x_step);
            uint16_t color = src[src_idx];
            uint8_t g = (color >> 5) & 0x3F;
            s_curr_gray[y * MOTION_W + x] = g;
        }
    }

    // 3. 第一帧直接保存
    if (s_first_frame) {
        ESP_LOGI(TAG, "⏳ 第 1 帧：保存背景模型...");
        memcpy(s_prev_gray, s_curr_gray, MOTION_W * MOTION_H);
        s_first_frame = false;
        return false;
    }

    // 4. 帧差法计算质心
    int sum_x = 0, sum_y = 0, count = 0;
    for (int y = 0; y < MOTION_H; y++) {
        for (int x = 0; x < MOTION_W; x++) {
            int idx = y * MOTION_W + x;
            int diff = abs((int)s_curr_gray[idx] - (int)s_prev_gray[idx]);
            
            if (diff > MOTION_THRESHOLD) {
                sum_x += x;
                sum_y += y;
                count++;
            }
        }
    }

    // 更新上一帧
    memcpy(s_prev_gray, s_curr_gray, MOTION_W * MOTION_H);

    // =========================================================
    // 【核心日志】把这里的注释打开，现在每帧都会打印
    // =========================================================
    ESP_LOGI(TAG, "帧差计算完成 -> count = %d (阈值=%d)", count, MOTION_THRESHOLD);

    if (count < MIN_MOTION_AREA) {
        return false;
    }

    // ... 后续的质心计算和 P 控制保持不变 ...

    // 5. 计算质心和偏差（使用降采样后的坐标系）
    int cx = sum_x / count;
    int cy = sum_y / count;
    int dx = cx - MOTION_W / 2;
    int dy = cy - MOTION_H / 2;
    // 【关键调试日志】打印质心和偏差
    ESP_LOGI(TAG, "质心(%d,%d) 偏差(%d,%d)", cx, cy, dx, dy);
    // 6. 死区判断
    if (abs(dx) < DEAD_ZONE_PIX && abs(dy) < DEAD_ZONE_PIX) {
        return false;
    }

    // 7. P 控制计算角度增量
    //    画面偏右(dx>0) → 云台右转 → pan角度增大
    //    画面偏下(dy>0) → 云台下俯 → tilt角度减小
    int pan_delta  = (int)( KP_PAN  * dx);
    int tilt_delta = (int)(KP_TILT * dy);

    int new_pan  = s_gimbal.pan_angle  + pan_delta;
    int new_tilt = s_gimbal.tilt_angle + tilt_delta;

    // 8. 限幅 60~120
    if (new_pan  < SERVO_ANGLE_MIN) new_pan  = SERVO_ANGLE_MIN;
    if (new_pan  > SERVO_ANGLE_MAX) new_pan  = SERVO_ANGLE_MAX;
    if (new_tilt < SERVO_ANGLE_MIN) new_tilt = SERVO_ANGLE_MIN;
    if (new_tilt > SERVO_ANGLE_MAX) new_tilt = SERVO_ANGLE_MAX;

    // 9. 如果角度有变化才发送（避免频繁发相同数据）
    if (new_pan != s_gimbal.pan_angle || new_tilt != s_gimbal.tilt_angle) {
        s_gimbal.pan_angle  = new_pan;
        s_gimbal.tilt_angle = new_tilt;
        
        uart_send_pan_tilt(new_pan, new_tilt);

        ESP_LOGI(TAG, "目标(%d,%d) 偏差(%d,%d) → 舵机 P:%d T:%d 面积:%d", 
                 cx, cy, dx, dy, new_pan, new_tilt, count);
        return true;
    }

    return false;
}

/* ============ 辅助函数 ============ */
void motion_get_gimbal(gimbal_state_t *state)
{
    if (state) *state = s_gimbal;
}

void motion_center(void)
{
    s_gimbal.pan_angle = 90;
    s_gimbal.tilt_angle = 90;
    uart_send_center();
    ESP_LOGI(TAG, "云台复位到中心");
}