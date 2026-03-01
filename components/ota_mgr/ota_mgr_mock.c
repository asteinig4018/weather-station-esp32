/**
 * ota_mgr_mock.c — Mock OTA manager for QEMU/CI builds.
 */

#include "ota_mgr.h"
#include "esp_log.h"

static const char *TAG = "ota_mgr";

esp_err_t ota_mgr_init(void)
{
    ESP_LOGI(TAG, "Mock OTA manager — no updates");
    return ESP_OK;
}

void ota_mgr_check_now(void)
{
    ESP_LOGI(TAG, "Mock OTA check triggered (no-op)");
}
