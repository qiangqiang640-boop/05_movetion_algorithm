#include "audio_common.h"
#include "freertos/ringbuf.h"

static const char *TAG = "SPK";
static i2s_chan_handle_t tx_handle = NULL;
static RingbufHandle_t s_rx_ringbuf = NULL;

void audio_playback_init(void)
{
    ESP_LOGI(TAG, "初始化扬声器...");

    // 创建环形缓冲（用于接收来自 WebSocket 的 PCM 数据）
    s_rx_ringbuf = xRingbufferCreate(RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (!s_rx_ringbuf) {
        ESP_LOGE(TAG, "环形缓冲创建失败");
        return;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(SPK_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 240;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH,   // 32bit slot
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
            .bclk = SPK_BCLK_PIN,
            .ws   = SPK_LRC_PIN,
            .dout = SPK_DOUT_PIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_LOGI(TAG, "扬声器初始化完成");
}

/**
 * @brief 由 WebSocket 回调调用：把浏览器发来的 16-bit PCM 写入环形缓冲
 * @param pcm16 16-bit 单声道 PCM 数据
 * @param count 样本数
 */
void audio_playback_push_pcm(const int16_t *pcm16, size_t count)
{
    if (!s_rx_ringbuf || count == 0) return;

    // 需要把 16-bit 左移 16 位扩展成 32-bit（对应 I2S 32-bit slot）
    static int32_t expand_buf[1024];
    size_t sent = 0;

    while (sent < count) {
        size_t chunk = count - sent;
        if (chunk > 1024) chunk = 1024;

        for (size_t i = 0; i < chunk; i++) {
            expand_buf[i] = ((int32_t)pcm16[sent + i]) << 16;
        }

        // 写入环形缓冲（不阻塞）
        BaseType_t ok = xRingbufferSend(s_rx_ringbuf, expand_buf,
                                        chunk * sizeof(int32_t), 0);
        if (ok != pdTRUE) {
            ESP_LOGW(TAG, "环形缓冲已满，丢弃 %u 字节", (unsigned)(chunk * 4));
            break;
        }
        sent += chunk;
    }
}

/**
 * @brief 扬声器播放任务：从环形缓冲取数据写入 I2S
 */
void audio_playback_task(void *arg)
{
    const size_t BUF_SAMPLES = 256;
    int32_t *local_buf = heap_caps_malloc(BUF_SAMPLES * sizeof(int32_t), MALLOC_CAP_INTERNAL);
    if (!local_buf) {
        ESP_LOGE(TAG, "播放缓冲分配失败");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "🔊 播放任务启动");

    while (1) {
        size_t sz = 0;
        void *data = xRingbufferReceiveUpTo(s_rx_ringbuf, &sz,
                                            pdMS_TO_TICKS(50),
                                            BUF_SAMPLES * sizeof(int32_t));
        if (data && sz > 0) {
            size_t written = 0;
            i2s_channel_write(tx_handle, data, sz, &written, pdMS_TO_TICKS(100));
            vRingbufferReturnItem(s_rx_ringbuf, data);
        } else {
            // 没数据时输出静音，避免 I2S 卡顿
            memset(local_buf, 0, BUF_SAMPLES * sizeof(int32_t));
            size_t written = 0;
            i2s_channel_write(tx_handle, local_buf,
                              BUF_SAMPLES * sizeof(int32_t), &written, pdMS_TO_TICKS(50));
        }
    }
}