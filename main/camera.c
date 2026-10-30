#include "camera.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "sensor.h"

static const char *TAG = "CAMERA_DRV";

// ---------- 引脚定义（ESP32-S3-CAM 通用） ----------
// 请根据你的具体开发板原理图调整，此处为最常见配置
#define CAM_PWDN_GPIO   -1
#define CAM_RESET_GPIO  -1
#define CAM_XCLK_GPIO   15
#define CAM_SIOD_GPIO   4
#define CAM_SIOC_GPIO   5
#define CAM_D7_GPIO     16
#define CAM_D6_GPIO     17
#define CAM_D5_GPIO     18
#define CAM_D4_GPIO     12
#define CAM_D3_GPIO     10
#define CAM_D2_GPIO     8
#define CAM_D1_GPIO     9
#define CAM_D0_GPIO     11
#define CAM_VSYNC_GPIO  6
#define CAM_HREF_GPIO   7
#define CAM_PCLK_GPIO   13

esp_err_t camera_init(void)
{
    camera_config_t config = {
        .pin_pwdn  = CAM_PWDN_GPIO,
        .pin_reset = CAM_RESET_GPIO,
        .pin_xclk = CAM_XCLK_GPIO,
        .pin_sscb_sda = CAM_SIOD_GPIO,
        .pin_sscb_scl = CAM_SIOC_GPIO,
        .pin_d7 = CAM_D7_GPIO,
        .pin_d6 = CAM_D6_GPIO,
        .pin_d5 = CAM_D5_GPIO,
        .pin_d4 = CAM_D4_GPIO,
        .pin_d3 = CAM_D3_GPIO,
        .pin_d2 = CAM_D2_GPIO,
        .pin_d1 = CAM_D1_GPIO,
        .pin_d0 = CAM_D0_GPIO,
        .pin_vsync = CAM_VSYNC_GPIO,
        .pin_href = CAM_HREF_GPIO,
        .pin_pclk = CAM_PCLK_GPIO,

        .xclk_freq_hz = 20000000,           // XCLK 20MHz
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,      // JPEG 格式，适合网络传输
        .frame_size = FRAMESIZE_QVGA,        // 800x600，可根据需要调整
        .jpeg_quality = 20,                  // 0~63，数值越小画质越好（文件越大）
        .fb_count = 2,                       // 双缓冲，提高流畅度
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    };

    esp_err_t err = esp_camera_init(&config);
    if (err == ESP_OK) ESP_LOGI(TAG, "摄像头初始化成功");
    else ESP_LOGE(TAG, "摄像头初始化失败: 0x%x", err);

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
    s->set_exposure_ctrl(s, 0); // 关闭自动曝光（AEC）
    s->set_gain_ctrl(s, 0);     // 关闭自动增益（AGC）
    s->set_whitebal(s, 0);      // 关闭自动白平衡（AWB）
    // 如果你发现画面太暗或太亮，可以手动设置曝光值（范围 0-1200）
    s->set_aec_value(s, 600); 
    }
    return err;
}