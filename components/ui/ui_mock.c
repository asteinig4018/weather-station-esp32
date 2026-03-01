/**
 * ui_mock.c — Mock UI for QEMU/CI builds.
 *
 * No LVGL, just logs screen transitions and data updates.
 */

#include "ui.h"
#include "esp_log.h"

static const char *TAG = "ui";
static int s_page = 0;

esp_err_t ui_init(void)
{
    ESP_LOGI(TAG, "Mock UI initialised (no LVGL)");
    return ESP_OK;
}

void ui_update_sensor_data(const hal_sensor_data_t *data)
{
    (void)data;
    /* Data logged by production_app on_sensor_data handler */
}

void ui_navigate_next(void)
{
    s_page = (s_page + 1) % 2;
    ESP_LOGI(TAG, "Navigate → %s", s_page == 0 ? "Dashboard" : "History");
}

void ui_tick(void)
{
    /* No-op in mock mode */
}
