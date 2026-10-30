#include "uart_comm.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UART_COMM";

/* ============ 发送队列 ============ */
#define UART_TX_QUEUE_LEN   16

typedef struct {
    uint8_t servo_id;
    int     angle;
} uart_cmd_t;

static QueueHandle_t s_uart_queue = NULL;
static TaskHandle_t  s_uart_task  = NULL;

/* ---------------- 角度限幅 ---------------- */
static inline int clamp_angle(int angle)
{
    if (angle < SERVO_ANGLE_MIN) return SERVO_ANGLE_MIN;
    if (angle > SERVO_ANGLE_MAX) return SERVO_ANGLE_MAX;
    return angle;
}

/* ---------------- 组帧并发送 ---------------- */
/* 帧格式: S<id>:<angle>\n   例如 "S1:90\n" */
static void uart_send_frame(uint8_t servo_id, int angle)
{
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "S%u:%d\n", servo_id, angle);

    uart_write_bytes(UART_COMM_PORT, buf, len);
    ESP_LOGD(TAG, "TX: %s", buf);
}

/* ---------------- UART 发送任务 ---------------- */
static void uart_tx_task(void *arg)
{
    uart_cmd_t cmd;
    ESP_LOGI(TAG, "UART 发送任务启动");

    while (1) {
        if (xQueueReceive(s_uart_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            uart_send_frame(cmd.servo_id, cmd.angle);
        }
    }
}

/* ---------------- 初始化 ---------------- */
esp_err_t uart_comm_init(void)
{
    uart_config_t uart_cfg = {
        .baud_rate  = UART_COMM_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_COMM_PORT, 256, 2048, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_COMM_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_COMM_PORT,
                                 UART_COMM_TX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));

    s_uart_queue = xQueueCreate(UART_TX_QUEUE_LEN, sizeof(uart_cmd_t));
    if (!s_uart_queue) {
        ESP_LOGE(TAG, "队列创建失败");
        return ESP_FAIL;
    }

    xTaskCreate(uart_tx_task, "uart_tx", 3072, NULL, 4, &s_uart_task);

    ESP_LOGI(TAG, "UART 初始化完成: TX=GPIO%d, %d bps",
             UART_COMM_TX_PIN, UART_COMM_BAUD);
    return ESP_OK;
}

/* ---------------- 对外接口 ---------------- */
void uart_send_servo(uint8_t servo_id, int angle)
{
    if (!s_uart_queue) return;
    if (servo_id != SERVO_PAN && servo_id != SERVO_TILT) {
        ESP_LOGW(TAG, "无效舵机编号: %u", servo_id);
        return;
    }

    uart_cmd_t cmd = {
        .servo_id = servo_id,
        .angle    = clamp_angle(angle),
    };

    if (xQueueSend(s_uart_queue, &cmd, 0) != pdTRUE) {
        uart_cmd_t dummy;
        xQueueReceive(s_uart_queue, &dummy, 0);
        xQueueSend(s_uart_queue, &cmd, 0);
    }
}

void uart_send_pan_tilt(int pan_angle, int tilt_angle)
{
    uart_send_servo(SERVO_PAN,  pan_angle);
    uart_send_servo(SERVO_TILT, tilt_angle);
}

void uart_send_center(void)
{
    uart_send_pan_tilt(90, 90);
}

/* ============================================================
 *  通信测试函数
 *  依次发送以下序列：
 *    1) 水平中位 90
 *    2) 水平最小 60
 *    3) 水平最大 120
 *    4) 水平回到中位 90
 *    5) 垂直中位 90
 *    6) 垂直最小 60
 *    7) 垂直最大 120
 *    8) 垂直回到中位 90
 *  每步间隔 500ms，整个测试约 4 秒
 * ============================================================ */
void uart_comm_test(void)
{
    ESP_LOGI(TAG, "===== UART 通信测试开始 =====");

    /* 定义测试序列：{舵机ID, 角度} */
    const struct {
        uint8_t servo;
        int     angle;
        const char *desc;
    } test_seq[] = {
        { SERVO_PAN,  90, "水平 -> 中位 90"   },
        { SERVO_PAN,  60, "水平 -> 最小 60"   },
        { SERVO_PAN, 120, "水平 -> 最大 120"  },
        { SERVO_PAN,  90, "水平 -> 回到 90"   },
        { SERVO_TILT, 90, "垂直 -> 中位 90"   },
        { SERVO_TILT, 60, "垂直 -> 最小 60"   },
        { SERVO_TILT,120, "垂直 -> 最大 120"  },
        { SERVO_TILT, 90, "垂直 -> 回到 90"   },
    };

    int seq_len = sizeof(test_seq) / sizeof(test_seq[0]);

    for (int i = 0; i < seq_len; i++) {
        ESP_LOGI(TAG, "[%d/%d] %s", i + 1, seq_len, test_seq[i].desc);
        uart_send_servo(test_seq[i].servo, test_seq[i].angle);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "===== UART 通信测试结束 =====");
}