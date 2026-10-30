#include "app_wifi.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "LCD.h"
#include "motion_detect.h"

#define WIFI_SSID      "OnePlus"
#define WIFI_PASSWORD  "12345678"

/* ==================== 外部依赖声明 ==================== */
extern void audio_playback_push_pcm(const int16_t *pcm16, size_t count);


static const char *TAG = "APP_WIFI";
#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t s_wifi_event_group;

/* ==================== 全局状态 ==================== */
static int s_audio_client_fd = -1;   // 音频推送（ESP32 → 浏览器）
static httpd_handle_t s_http_server = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"

typedef struct {
    httpd_req_t *req;
} stream_ctx_t;

static QueueHandle_t s_stream_queue = NULL;
static void video_stream_task(void *arg);

/* ==================== Wi-Fi 初始化 ==================== */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGE(TAG, "Wi-Fi 断开 reason=%d，1 秒后重连", d->reason);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi 连接成功，IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void app_wifi_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    s_wifi_event_group = xEventGroupCreate();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .capable = true, .required = false },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    esp_wifi_set_max_tx_power(44);

    ESP_LOGI(TAG, "Wi-Fi 启动，等待连接...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
}

/* ==================== 根路径：HTML 页面 ==================== */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>ESP32-S3 音视频监控</title>"
        "<style>"
        "body{text-align:center;background:#111;color:#0fc;font-family:Arial;padding:20px}"
        "img{border:3px solid #0fc;border-radius:8px;max-width:90%}"
        "button{padding:12px 24px;font-size:16px;margin:8px;background:#0fc;color:#111;"
        "border:none;border-radius:8px;cursor:pointer;font-weight:bold}"
        "button:hover{background:#0af}"
        "</style></head><body>"
        "<h1>📹 ESP32-S3 实时监控</h1>"
        "<img src='/stream' alt='视频流'>"
        "<div>"
        "<button id='btn'>🔊 开启音箱</button>"
        "<button id='mic-btn'>🎤 开启麦克风</button>"
        "<div id='status' style='margin-top:10px;color:#ff0'>状态：等待操作</div>"
        "<div id='log' style='margin-top:10px;color:#888;font-size:12px;"
        "max-width:600px;margin-left:auto;margin-right:auto;text-align:left'></div>"
        "</div>"
        "<script>"
        "function log(msg, color){"
        "  var el=document.getElementById('log');"
        "  el.innerHTML += '<div style=\"color:' + (color||'#888') + '\">' + msg + '</div>';"
        "  console.log(msg);"
        "}"

        /* -------- 音箱：ESP32 → 浏览器 -------- */
        "var audioCtx=null, ws=null;"
        "document.getElementById('btn').addEventListener('click', async function(){"
        "  log('音箱按钮被点击', '#0ff');"
        "  try {"
        "    if(!audioCtx){ audioCtx=new AudioContext({sampleRate:16000}); }"
        "    if(audioCtx.state==='suspended'){ await audioCtx.resume(); }"
        "  } catch(e) { log('AudioContext 失败: ' + e.message, '#f00'); return; }"
        "  if(ws){ ws.close(); ws=null;"
        "    document.getElementById('status').innerText='状态：已断开';"
        "    document.getElementById('btn').innerText='🔊 开启音箱'; return; }"
        "  var url = 'ws://' + location.host + '/audio';"
        "  log('连接音箱: ' + url, '#0ff');"
        "  try { ws=new WebSocket(url); } catch(e) { log('创建失败: '+e.message, '#f00'); return; }"
        "  ws.binaryType='arraybuffer';"
        "  ws.onopen=function(){ log('音箱已连接', '#0f0');"
        "    document.getElementById('status').innerText='状态：已连接 ✅';"
        "    document.getElementById('status').style.color='#0f0';"
        "    document.getElementById('btn').innerText='🔇 关闭音箱'; };"
        "  ws.onclose=function(ev){ log('音箱已关闭, code='+ev.code, '#ff0');"
        "    document.getElementById('btn').innerText='🔊 开启音箱'; ws=null; };"
        "  ws.onerror=function(e){ log('音箱 WebSocket 错误', '#f00'); };"
        "  ws.onmessage=function(ev){"
        "    var pcm=new Int16Array(ev.data);"
        "    var buf=audioCtx.createBuffer(1,pcm.length,16000);"
        "    var ch=buf.getChannelData(0);"
        "    for(var i=0;i<pcm.length;i++) ch[i]=pcm[i]/32768.0;"
        "    var src=audioCtx.createBufferSource();"
        "    src.buffer=buf;"
        "    src.connect(audioCtx.destination);"
        "    src.start();"
        "  };"
        "});"

        /* -------- 麦克风：浏览器 → ESP32 -------- */
        "var micStream=null, micCtx=null, micProcessor=null, micWs=null;"
        "document.getElementById('mic-btn').addEventListener('click', async function(){"
        "  var btn=document.getElementById('mic-btn');"
        "  if(micWs){"
        "    if(micStream) micStream.getTracks().forEach(t=>t.stop());"
        "    if(micCtx) micCtx.close();"
        "    micWs.close();"
        "    micStream=null; micCtx=null; micWs=null;"
        "    btn.innerText='🎤 开启麦克风';"
        "    log('麦克风已关闭', '#ff0'); return;"
        "  }"
        "  try {"
        "    micStream = await navigator.mediaDevices.getUserMedia({audio:{"
        "      channelCount:1, sampleRate:16000,"
        "      echoCancellation:true, noiseSuppression:true, autoGainControl:true"
        "    }});"
        "    log('麦克风采集成功', '#0f0');"
        "  } catch(e) { log('麦克风获取失败: '+e.message, '#f00'); return; }"
        "  micCtx = new AudioContext({sampleRate:16000});"
        "  var src = micCtx.createMediaStreamSource(micStream);"
        "  micProcessor = micCtx.createScriptProcessor(1024, 1, 1);"
        "  micProcessor.onaudioprocess = function(e){"
        "    if(!micWs || micWs.readyState !== WebSocket.OPEN) return;"
        "    var input = e.inputBuffer.getChannelData(0);"
        "    var pcm = new Int16Array(input.length);"
        "    for(var i=0;i<input.length;i++){"
        "      var s = Math.max(-1, Math.min(1, input[i]));"
        "      pcm[i] = s < 0 ? s*32768 : s*32767;"
        "    }"
        "    micWs.send(pcm.buffer);"
        "  };"
        "  var silentGain = micCtx.createGain();"
        "  silentGain.gain.value = 0;"
        "  src.connect(micProcessor);"
        "  micProcessor.connect(silentGain);"
        "  silentGain.connect(micCtx.destination);"
        "  micWs = new WebSocket('ws://' + location.host + '/mic');"
        "  micWs.binaryType = 'arraybuffer';"
        "  micWs.onopen = function(){ log('麦克风已连接', '#0f0');"
        "    btn.innerText='🛑 关闭麦克风'; };"
        "  micWs.onclose = function(){ log('麦克风已关闭', '#ff0');"
        "    btn.innerText='🎤 开启麦克风'; micWs=null; };"
        "  micWs.onerror = function(e){ log('麦克风 WebSocket 错误', '#f00'); };"
        "});"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

/* ==================== /capture：单张照片 ==================== */
static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { httpd_resp_send_500(req); return ESP_FAIL; }
    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

/* ==================== /stream：异步视频流 ==================== */
static void video_stream_task(void *arg)
{
    stream_ctx_t ctx;
    char part_buf[128];

    while (1) {
        if (xQueueReceive(s_stream_queue, &ctx, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        httpd_req_t *req = ctx.req;
        ESP_LOGI(TAG, "🎥 视频流客户端已连接");

        httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" PART_BOUNDARY);

        while (1) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (!fb) break;

            size_t hlen = snprintf(part_buf, sizeof(part_buf),
                "\r\n--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
                PART_BOUNDARY, fb->len);

            if (httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK ||
                httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len) != ESP_OK) {
                esp_camera_fb_return(fb);
                break;
            }

            // 每 3 帧刷一次 LCD和计算运动
            static int cnt = 0;
            if (++cnt % 3 == 0) {
                lcd_display_jpeg(fb->buf, fb->len);
            }

            esp_camera_fb_return(fb);
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        httpd_resp_send_chunk(req, NULL, 0);
        httpd_req_async_handler_complete(req);
        ESP_LOGI(TAG, "🎥 视频流客户端已断开");
    }
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    httpd_req_t *ar = NULL;
    esp_err_t ret = httpd_req_async_handler_begin(req, &ar);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "异步请求创建失败: %d", ret);
        return ret;
    }

    stream_ctx_t ctx = { .req = ar };
    if (xQueueSend(s_stream_queue, &ctx, 0) != pdTRUE) {
        ESP_LOGE(TAG, "视频流队列已满");
        httpd_req_async_handler_complete(ar);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ==================== WebSocket /audio：ESP32 → 浏览器 ==================== */

/**
 * @brief 预握手回调：获取并登记音箱客户端 fd
 */
static esp_err_t audio_ws_pre_handshake_cb(httpd_req_t *req)
{
    s_audio_client_fd = httpd_req_to_sockfd(req);
    ESP_LOGI(TAG, "✅ 预握手: 音箱客户端 fd=%d 已登记", s_audio_client_fd);
    return ESP_OK;
}

static esp_err_t audio_ws_handler(httpd_req_t *req)
{
    // 数据帧处理（客户端一般不发送数据给音箱端点）
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;
    return ESP_OK;
}

/**
 * @brief 通过 WebSocket 推送 PCM 音频到浏览器
 */
bool audio_ws_send_pcm(const int16_t *pcm, size_t count)
{
    if (s_audio_client_fd < 0 || s_http_server == NULL) return false;

    // 检查 fd 是否仍为有效 WebSocket 客户端
    if (httpd_ws_get_fd_info(s_http_server, s_audio_client_fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
        s_audio_client_fd = -1;
        return false;
    }

    httpd_ws_frame_t ws_pkt = {
        .payload = (uint8_t *)pcm,
        .len = count * sizeof(int16_t),
        .type = HTTPD_WS_TYPE_BINARY,
    };
    esp_err_t err = httpd_ws_send_frame_async(s_http_server, s_audio_client_fd, &ws_pkt);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "推送音频失败: %d，断开连接", err);
        s_audio_client_fd = -1;
        return false;
    }
    return true;
}

/* ==================== WebSocket /mic：浏览器 → ESP32 ==================== */
static esp_err_t mic_ws_handler(httpd_req_t *req)
{
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;

    // 查询帧长度
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.type == HTTPD_WS_TYPE_BINARY && ws_pkt.len > 0 && ws_pkt.len < 8192) {
        uint8_t *buf = malloc(ws_pkt.len);
        if (buf) {
            ws_pkt.payload = buf;
            ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            if (ret == ESP_OK) {
                audio_playback_push_pcm((int16_t *)buf, ws_pkt.len / 2);
            }
            free(buf);
        }
    }
    return ESP_OK;
}

/* ==================== HTTP 服务器启动 ==================== */
httpd_handle_t app_http_start(void)
{
    if (s_stream_queue == NULL) {
        s_stream_queue = xQueueCreate(2, sizeof(stream_ctx_t));
        xTaskCreate(video_stream_task, "video_stream", 8192, NULL, 3, NULL);
    }

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.lru_purge_enable = true;
    config.stack_size = 16384;
    config.max_open_sockets = 7;
    config.max_uri_handlers = 8;

    if (httpd_start(&server, &config) == ESP_OK) {
        s_http_server = server;

        httpd_uri_t root = { "/", HTTP_GET, root_get_handler, NULL };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t stream = { "/stream", HTTP_GET, stream_handler, NULL };
        httpd_register_uri_handler(server, &stream);

        httpd_uri_t capture = { "/capture", HTTP_GET, capture_handler, NULL };
        httpd_register_uri_handler(server, &capture);

        // /audio：ESP32 → 浏览器（音箱）
        httpd_uri_t audio_ws = {
            .uri = "/audio",
            .method = HTTP_GET,
            .handler = audio_ws_handler,
            .user_ctx = NULL,
            .is_websocket = true,
            .ws_pre_handshake_cb = audio_ws_pre_handshake_cb,
        };
        esp_err_t err = httpd_register_uri_handler(server, &audio_ws);
        ESP_LOGI(TAG, "注册 /audio: %s", esp_err_to_name(err));

        // /mic：浏览器 → ESP32（麦克风）
        httpd_uri_t mic_ws = {
            .uri = "/mic",
            .method = HTTP_GET,
            .handler = mic_ws_handler,
            .user_ctx = NULL,
            .is_websocket = true,
        };
        esp_err_t err2 = httpd_register_uri_handler(server, &mic_ws);
        ESP_LOGI(TAG, "注册 /mic: %s", esp_err_to_name(err2));

        ESP_LOGI(TAG, "HTTP 服务器启动成功");
    } else {
        ESP_LOGE(TAG, "HTTP 服务器启动失败");
    }
    return server;
}