#include "web_server.h"
#include "esp_log.h"

static const char *TAG = "web_server";

esp_err_t web_server_init(void)
{
    ESP_LOGI(TAG, "Mock web server — no HTTP");
    return ESP_OK;
}

void web_server_feed_sensor_data(const hal_sensor_data_t *data)
{
    (void)data;
}

esp_err_t web_server_deinit(void)
{
    return ESP_OK;
}
