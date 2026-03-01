/**
 * net_mgr_real.c — WiFi STA + HTTP POST telemetry uploader.
 *
 * Connects to WiFi using Kconfig credentials. Subscribes to sensor
 * events and uploads data to the configured server URL at a configurable
 * interval (CONFIG_NET_UPLOAD_INTERVAL_S).
 *
 * Uses esp_http_client for POST requests. Data is sent as JSON.
 */

#include "net_mgr.h"

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "net_mgr";

/* WiFi connection state */
static EventGroupHandle_t s_wifi_events;
#define WIFI_CONNECTED_BIT BIT0

static bool s_connected = false;

/* Upload tracking */
static hal_sensor_data_t s_last_data;
static bool s_data_available = false;
static int64_t s_last_upload_us = 0;

/* =========================================================================
 * WiFi event handlers
 * ========================================================================= */

static void on_wifi_event(void *arg, esp_event_base_t base,
                           int32_t event_id, void *event_data)
{
    (void)arg;

    if (base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
                s_connected = false;
                xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
                esp_wifi_connect();
                break;
            default:
                break;
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

/* =========================================================================
 * Sensor data feed (called from production_app event handler)
 * ========================================================================= */

void net_mgr_feed_sensor_data(const hal_sensor_data_t *data)
{
    if (data) {
        s_last_data = *data;
        s_data_available = true;
    }
}

/* =========================================================================
 * HTTP upload task
 * ========================================================================= */

static void upload_data(const hal_sensor_data_t *d)
{
    char json[512];
    int len = snprintf(json, sizeof(json),
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

    esp_http_client_config_t config = {
        .url = CONFIG_NET_SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, len);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Upload OK (HTTP %d, %d bytes)", status, len);
    } else {
        ESP_LOGW(TAG, "Upload failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

static void upload_task(void *arg)
{
    (void)arg;

    int64_t interval_us = (int64_t)CONFIG_NET_UPLOAD_INTERVAL_S * 1000000LL;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!s_connected || !s_data_available) continue;

        int64_t now = esp_timer_get_time();
        if ((now - s_last_upload_us) < interval_us) continue;

        upload_data(&s_last_data);
        s_last_upload_us = now;
    }
}

/* =========================================================================
 * WiFi init
 * ========================================================================= */

static esp_err_t wifi_init(void)
{
    /* NVS init (required by WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register handlers on the default system event loop */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_NET_WIFI_SSID,
            .password = CONFIG_NET_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA started, connecting to \"%s\"...",
             CONFIG_NET_WIFI_SSID);
    return ESP_OK;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

esp_err_t net_mgr_init(void)
{
    /* Start WiFi */
    ESP_ERROR_CHECK(wifi_init());

    /* Start upload task */
    BaseType_t ok = xTaskCreate(upload_task, "upload", 4096, NULL,
                                 tskIDLE_PRIORITY + 2, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create upload task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool net_mgr_is_connected(void)
{
    return s_connected;
}
