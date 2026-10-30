#include "LCD.h"
#include "esp_log.h"
#include "esp_lcd_ili9341.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "jpeg_decoder.h"
#include "esp_heap_caps.h"
#include <string.h>
#include "motion_detect.h"

static const char *TAG = "LCD";

// ---------- 引脚定义 ----------
#define LCD_HOST      SPI2_HOST
#define PIN_SCLK      3
#define PIN_MOSI      45
#define PIN_MISO      46
#define PIN_DC        47
#define PIN_RST       21
#define PIN_CS        14
#define PIN_BK        0
#define PCLK_HZ       (40 * 1000 * 1000)
#define BK_ON_LEVEL   1

esp_lcd_panel_handle_t panel_handle = NULL;

// ---------- 显示 JPEG 图像（分段绘制） ----------
esp_err_t lcd_display_jpeg(const uint8_t *jpg_data, size_t jpg_len)
{
    esp_jpeg_image_cfg_t cfg_info = {
        .indata = (uint8_t *)jpg_data,
        .indata_size = jpg_len,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,   // 不缩放
    };
    esp_jpeg_image_output_t img_info;
    esp_err_t ret = esp_jpeg_get_image_info(&cfg_info, &img_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "获取 JPEG 信息失败: %d", ret);
        return ret;
    }

    // 分配解码输出缓冲区（尺寸 = 320x240 = 153600 字节）
    size_t out_len = img_info.width * img_info.height * 2;
    uint8_t *outbuf = (uint8_t *)heap_caps_malloc(out_len, MALLOC_CAP_SPIRAM);
    if (!outbuf) {
        ESP_LOGE(TAG, "分配输出缓冲区失败");
        return ESP_ERR_NO_MEM;
    }

    esp_jpeg_image_cfg_t cfg_decode = {
        .indata = (uint8_t *)jpg_data,
        .indata_size = jpg_len,
        .outbuf = outbuf,
        .outbuf_size = out_len,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = { .swap_color_bytes = true },
        .advanced = { .working_buffer = NULL, .working_buffer_size = 0 },
    };
    esp_jpeg_image_output_t out;
    ret = esp_jpeg_decode(&cfg_decode, &out);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "解码失败: %d", ret);
        heap_caps_free(outbuf);
        return ret;
    }

    // ========================================================
    // 1. 【核心调用】在内存释放之前，将 RGB565 数据喂给运动检测！
    // ========================================================
    motion_detect_process((const uint8_t *)outbuf, out.width, out.height);

    // ========================================================
    // 2. 绘制到 LCD
    // ========================================================
    ret = esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, out.width, out.height, outbuf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "绘制失败: %d", ret);
    }

    // ========================================================
    // 3. 最后释放内存，然后返回。
    // 绝对不能把 return 写在调用检测函数之前！
    // ========================================================
    heap_caps_free(outbuf);
    return ret;
}

// ---------- LCD 初始化 ----------
void lcd_init(void)
{
    // 背光
    gpio_config_t bk = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_BK,
    };
    gpio_config(&bk);
    gpio_set_level(PIN_BK, BK_ON_LEVEL);

    // SPI 总线（最大传输大小设小，DMA 更易分配）
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCLK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,   // 每次传输不超过 4KB，避免分配失败
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    // Panel IO
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = PCLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &io));

    // 面板驱动
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_cfg, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // 方向调整（根据实际显示效果尝试修改）
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));   // 横屏
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));  // 先不镜像
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "LCD 初始化完成");
}