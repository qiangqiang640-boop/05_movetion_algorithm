#ifndef APP_WIFI_H
#define APP_WIFI_H

#include "esp_http_server.h"
#include <stdbool.h>
#include <stdint.h>

void app_wifi_init(void);
httpd_handle_t app_http_start(void);
bool audio_ws_send_pcm(const int16_t *pcm, size_t count);

#endif