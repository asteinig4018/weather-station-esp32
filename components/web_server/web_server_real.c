/**
 * web_server_real.c — HTTP server: status API + push OTA endpoint.
 *
 * Endpoints:
 *   GET  /api/status  → latest sensor data as JSON
 *   GET  /api/info    → firmware version, uptime, heap, IP, RSSI
 *   POST /api/ota     → receive .bin, write to inactive OTA partition, reboot
 */

#include "web_server.h"

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "data_store.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "web_server";

/* --- Cached sensor data -------------------------------------------------- */

static hal_sensor_data_t s_latest;
static bool              s_data_valid = false;
static SemaphoreHandle_t s_mutex;
static httpd_handle_t    s_server = NULL;

/* --- JSON formatting helper ---------------------------------------------- */

static int format_sensor_json(char *buf, size_t size,
                               const hal_sensor_data_t *d)
{
    return snprintf(buf, size,
        "{"
        "\"timestamp_us\":%lld,"
        "\"temperature_c\":%.2f,"
        "\"pressure_pa\":%.1f,"
        "\"humidity_pct\":%.1f,"
        "\"pm1p0\":%.1f,"
        "\"pm2p5\":%.1f,"
        "\"pm4p0\":%.1f,"
        "\"pm10\":%.1f,"
        "\"voc\":%.1f,"
        "\"nox\":%.1f,"
        "\"accel_x\":%.3f,"
        "\"accel_y\":%.3f,"
        "\"accel_z\":%.3f,"
        "\"power_good\":%s"
        "}",
        d->timestamp_us,
        d->baro.temp_c,
        d->baro.pressure_pa,
        d->air.hum_pct,
        d->air.pm1p0,
        d->air.pm2p5,
        d->air.pm4p0,
        d->air.pm10,
        d->air.voc,
        d->air.nox,
        d->accel.x,
        d->accel.y,
        d->accel.z,
        d->pwrgd ? "true" : "false");
}

/* =========================================================================
 * GET /api/status — latest sensor readings
 * ========================================================================= */

static esp_err_t handle_api_status(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    hal_sensor_data_t snap;
    bool valid;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snap  = s_latest;
    valid = s_data_valid;
    xSemaphoreGive(s_mutex);

    if (!valid) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "{\"error\":\"No sensor data yet\"}");
        return ESP_OK;
    }

    char buf[512];
    int n = format_sensor_json(buf, sizeof(buf), &snap);
    return httpd_resp_send(req, buf, n);
}

/* =========================================================================
 * GET /api/info — device information
 * ========================================================================= */

static esp_err_t handle_api_info(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    const esp_app_desc_t *app = esp_app_get_description();

    /* IP address */
    esp_netif_ip_info_t ip_info = {};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_get_ip_info(netif, &ip_info);
    }

    /* WiFi RSSI */
    int8_t rssi = 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
    }

    char buf[512];
    int n = snprintf(buf, sizeof(buf),
        "{"
        "\"fw_version\":\"%s\","
        "\"fw_date\":\"%s %s\","
        "\"idf_version\":\"%s\","
        "\"uptime_s\":%lld,"
        "\"free_heap\":%lu,"
        "\"min_free_heap\":%lu,"
        "\"ip\":\"" IPSTR "\","
        "\"rssi\":%d,"
        "\"data_store_count\":%u"
        "}",
        app->version,
        app->date, app->time,
        IDF_VER,
        (long long)(esp_timer_get_time() / 1000000LL),
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)esp_get_minimum_free_heap_size(),
        IP2STR(&ip_info.ip),
        (int)rssi,
        (unsigned)data_store_count());

    return httpd_resp_send(req, buf, n);
}

/* =========================================================================
 * POST /api/ota — receive firmware binary, flash, reboot
 * ========================================================================= */

#define OTA_BUF_SIZE 1024

static esp_err_t handle_api_ota(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA push started, content_len=%d", req->content_len);

    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "{\"error\":\"Content-Length required\"}");
        return ESP_FAIL;
    }

    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (!update) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "{\"error\":\"No OTA partition\"}");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing to partition '%s' at 0x%lx, size %d bytes",
             update->label, (unsigned long)update->address, req->content_len);

    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(update, (size_t)req->content_len, &ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "{\"error\":\"esp_ota_begin failed\"}");
        return ESP_FAIL;
    }

    char buf[OTA_BUF_SIZE];
    int remaining = req->content_len;
    int total_recv = 0;
    bool failed = false;

    while (remaining > 0) {
        int to_read = remaining < OTA_BUF_SIZE ? remaining : OTA_BUF_SIZE;
        int recv_len = httpd_req_recv(req, buf, to_read);

        if (recv_len < 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "OTA recv error at %d/%d bytes",
                     total_recv, req->content_len);
            failed = true;
            break;
        }
        if (recv_len == 0) {
            break;
        }

        err = esp_ota_write(ota, buf, (size_t)recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            failed = true;
            break;
        }

        remaining -= recv_len;
        total_recv += recv_len;

        /* Progress log every ~100 KB */
        if ((total_recv % (100 * 1024)) < OTA_BUF_SIZE) {
            ESP_LOGI(TAG, "OTA progress: %d / %d bytes",
                     total_recv, req->content_len);
        }
    }

    err = esp_ota_end(ota);
    if (failed || err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed (recv_ok=%d, end=%s)",
                 !failed, esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "{\"error\":\"OTA write/verify failed\"}");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s",
                 esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "{\"error\":\"Failed to set boot partition\"}");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA complete (%d bytes) — rebooting in 500 ms", total_recv);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req,
        "{\"status\":\"ok\",\"message\":\"Update applied, rebooting...\"}");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK; /* unreachable */
}

/* =========================================================================
 * Server start / stop
 * ========================================================================= */

static esp_err_t start_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = CONFIG_WEB_SERVER_PORT;
    config.task_priority    = tskIDLE_PRIORITY + 3;
    config.stack_size       = 8192;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout  = 10;  /* seconds */
    config.send_wait_timeout  = 10;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Register handlers — order matters for matching */
    static const httpd_uri_t uri_status = {
        .uri = "/api/status", .method = HTTP_GET,
        .handler = handle_api_status
    };
    static const httpd_uri_t uri_info = {
        .uri = "/api/info", .method = HTTP_GET,
        .handler = handle_api_info
    };
    static const httpd_uri_t uri_ota = {
        .uri = "/api/ota", .method = HTTP_POST,
        .handler = handle_api_ota
    };

    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_info);
    httpd_register_uri_handler(s_server, &uri_ota);

    return ESP_OK;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

esp_err_t web_server_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = start_server();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "Web server started on port %d", CONFIG_WEB_SERVER_PORT);
    return ESP_OK;
}

void web_server_feed_sensor_data(const hal_sensor_data_t *data)
{
    if (!data || !s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_latest     = *data;
    s_data_valid = true;
    xSemaphoreGive(s_mutex);
}

esp_err_t web_server_deinit(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    return ESP_OK;
}
