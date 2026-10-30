#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_wifi.h"
#include "camera.h"
#include "LCD.h"
#include "audio_common.h"
#include "uart_comm.h"
#include "motion_detect.h"

extern void audio_mic_init(void);
extern bool audio_mic_read(int32_t *buffer, size_t *bytes_read);
extern void audio_mic_to_pcm16(const int32_t *raw, int16_t *pcm, int count);
extern void audio_playback_init(void);
extern void audio_playback_task(void *arg);

static const char *TAG = "MAIN";

static void audio_capture_task(void *arg)
{
    int32_t *raw_buffer = heap_caps_malloc(DMA_BUF_LEN, MALLOC_CAP_SPIRAM);
    int16_t *pcm_buffer = heap_caps_malloc(DMA_BUF_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!raw_buffer || !pcm_buffer) {
        ESP_LOGE(TAG, "音频缓冲区分配失败");
        vTaskDelete(NULL);
        return;
    }

    audio_mic_init();
    ESP_LOGI(TAG, "🎤 音频采集任务启动");

    while (1) {
        size_t bytes_read = 0;
        if (audio_mic_read(raw_buffer, &bytes_read)) {
            int samples = bytes_read / sizeof(int32_t);
            audio_mic_to_pcm16(raw_buffer, pcm_buffer, samples);
            audio_ws_send_pcm(pcm_buffer, samples);
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "启动无线监控系统");

    app_wifi_init();
    camera_init();
    lcd_init();
    app_http_start();

    ESP_ERROR_CHECK(uart_comm_init());
    uart_comm_test();
    // 3. 初始化运动检测
    motion_detect_init();

    // 4. 复位云台到中位
    vTaskDelay(pdMS_TO_TICKS(500));
    motion_center();
    // 初始化扬声器（会创建 Ringbuffer 和 I2S TX）
    audio_playback_init();


    // 启动播放任务（接收浏览器发来的 PCM → I2S）
    xTaskCreate(audio_playback_task, "audio_play", 4096, NULL, 2, NULL);

    // 启动麦克风采集任务（采集 → WebSocket → 浏览器）
    xTaskCreate(audio_capture_task, "audio_capture", 8192, NULL, 2, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Free internal: %lu, PSRAM: %lu",
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}