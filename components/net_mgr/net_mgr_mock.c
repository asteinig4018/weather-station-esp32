/**
 * net_mgr_mock.c — Mock network manager for QEMU/CI builds.
 *
 * No WiFi or HTTP — just logs that uploads would occur.
 */

#include "net_mgr.h"
#include "esp_log.h"

static const char *TAG = "net_mgr";

esp_err_t net_mgr_init(void)
{
    ESP_LOGI(TAG, "Mock network manager — no WiFi/HTTP");
    return ESP_OK;
}

void net_mgr_feed_sensor_data(const hal_sensor_data_t *data)
{
    (void)data;
    /* No-op in mock mode */
}

bool net_mgr_is_connected(void)
{
    return false;
}
