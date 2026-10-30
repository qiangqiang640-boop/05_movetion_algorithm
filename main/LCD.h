#ifndef LCD_H
#define LCD_H

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#define LCD_WIDTH  240
#define LCD_HEIGHT 320

extern esp_lcd_panel_handle_t panel_handle;

void lcd_init(void);
esp_err_t lcd_display_jpeg(const uint8_t *jpg_data, size_t jpg_len);

#endif