// audio_mic.c
#include "audio_common.h"

static const char *TAG = "MIC";
static i2s_chan_handle_t rx_handle = NULL;

void audio_mic_init(void)
{
    ESP_LOGI(TAG, "初始化麦克风...");
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIC_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 240;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH,
            .slot_mode = SLOT_MODE,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH,
            .ws_pol = false,
            .bit_shift = false,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_BCLK_PIN,
            .ws   = MIC_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = MIC_DIN_PIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    ESP_LOGI(TAG, "麦克风初始化完成");
}

bool audio_mic_read(int32_t *buffer, size_t *bytes_read)
{
    esp_err_t err = i2s_channel_read(rx_handle, buffer, DMA_BUF_LEN,
                                     bytes_read, pdMS_TO_TICKS(100));
    return (err == ESP_OK && *bytes_read > 0);
}

// 将 32-bit I2S 原始数据转为 16-bit PCM
void audio_mic_to_pcm16(const int32_t *raw, int16_t *pcm, int count)
{
    for (int i = 0; i < count; i++) {
        int32_t sample = raw[i] >> 14;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        pcm[i] = (int16_t)sample;
    }
}