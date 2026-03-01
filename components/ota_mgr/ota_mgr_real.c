/**
 * ota_mgr_real.c — OTA firmware update manager using esp_https_ota.
 *
 * Periodically checks CONFIG_OTA_SERVER_URL for a new firmware binary.
 * If the server returns 200 with valid firmware, performs OTA update
 * and reboots. Uses HTTP (not HTTPS) by default for simplicity —
 * enable HTTPS by adding a server certificate.
 *
 * The OTA URL should point directly to the .bin file.
 */

#include "ota_mgr.h"

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_app_format.h"

static const char *TAG = "ota_mgr";

static EventGroupHandle_t s_ota_events;
#define OTA_CHECK_NOW_BIT BIT0

static void do_ota_check(void)
{
    ESP_LOGI(TAG, "Checking for OTA update at %s", CONFIG_OTA_SERVER_URL);

    esp_http_client_config_t http_config = {
        .url = CONFIG_OTA_SERVER_URL,
        .timeout_ms = 10000,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA update successful — rebooting...");
        esp_restart();
    } else if (ret == ESP_ERR_OTA_VALIDATE_FAILED) {
        ESP_LOGW(TAG, "OTA image validation failed");
    } else {
        ESP_LOGD(TAG, "No update available or server unreachable (%s)",
                 esp_err_to_name(ret));
    }
}

static void ota_task(void *arg)
{
    (void)arg;

    /* Log current firmware info */
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Running firmware: %s v%s (built %s %s)",
             app->project_name, app->version, app->date, app->time);

    /* Initial check on boot */
    vTaskDelay(pdMS_TO_TICKS(10000));  /* wait for WiFi to connect */
    do_ota_check();

    /* Periodic checks */
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_ota_events, OTA_CHECK_NOW_BIT,
                                                pdTRUE, pdFALSE,
                                                pdMS_TO_TICKS(CONFIG_OTA_CHECK_INTERVAL_S * 1000));

        if (bits & OTA_CHECK_NOW_BIT) {
            ESP_LOGI(TAG, "Manual OTA check triggered");
        }

        do_ota_check();
    }
}

esp_err_t ota_mgr_init(void)
{
    s_ota_events = xEventGroupCreate();
    if (!s_ota_events) return ESP_ERR_NO_MEM;

    BaseType_t ok = xTaskCreate(ota_task, "ota", 8192, NULL,
                                 tskIDLE_PRIORITY + 1, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA manager started (check every %d s)",
             CONFIG_OTA_CHECK_INTERVAL_S);
    return ESP_OK;
}

void ota_mgr_check_now(void)
{
    if (s_ota_events) {
        xEventGroupSetBits(s_ota_events, OTA_CHECK_NOW_BIT);
    }
}
