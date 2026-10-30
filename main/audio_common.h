#ifndef AUDIO_COMMON_H
#define AUDIO_COMMON_H

#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

// ====== 引脚定义（避开 Octal PSRAM 保留引脚） ======
// INMP441 麦克风 (I2S_NUM_0)
#define MIC_I2S_NUM         I2S_NUM_0
#define MIC_BCLK_PIN        GPIO_NUM_2
#define MIC_WS_PIN          GPIO_NUM_41
#define MIC_DIN_PIN         GPIO_NUM_42

// MAX98357 扬声器 (I2S_NUM_1)
#define SPK_I2S_NUM         I2S_NUM_1
#define SPK_BCLK_PIN        GPIO_NUM_48
#define SPK_LRC_PIN         GPIO_NUM_39   // 待确认
#define SPK_DOUT_PIN        GPIO_NUM_40   // 待确认

// 音频参数
#define SAMPLE_RATE         16000
#define DMA_BUF_SAMPLES     512                          // 每次读取 512 个样本
#define DMA_BUF_LEN         (DMA_BUF_SAMPLES * sizeof(int32_t))  // 2048 字节
#define RINGBUF_SIZE        (32 * 1024)   // 32KB 环形缓冲

// I2S 配置：统一使用 32 位数据位宽和槽宽
#define I2S_DATA_BIT_WIDTH  I2S_DATA_BIT_WIDTH_32BIT
#define I2S_SLOT_BIT_WIDTH  I2S_SLOT_BIT_WIDTH_32BIT
#define SLOT_MODE           I2S_SLOT_MODE_MONO

#endif