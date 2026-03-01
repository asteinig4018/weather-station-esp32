/**
 * data_store_mock.c — RAM-backed ring buffer for mock/QEMU builds.
 *
 * Uses a statically allocated array. No persistence across reboots.
 */

#include "data_store.h"

#include <string.h>
#include "esp_log.h"

static const char *TAG = "data_store";

static hal_sensor_data_t s_ring[DATA_STORE_MAX_ENTRIES];
static size_t s_head  = 0;  /* next write position */
static size_t s_count = 0;  /* number of valid entries */
static bool   s_init  = false;

esp_err_t data_store_init(void)
{
    memset(s_ring, 0, sizeof(s_ring));
    s_head  = 0;
    s_count = 0;
    s_init  = true;
    ESP_LOGI(TAG, "Mock data store initialised (RAM, %d entries max)",
             DATA_STORE_MAX_ENTRIES);
    return ESP_OK;
}

esp_err_t data_store_append(const hal_sensor_data_t *data)
{
    if (!s_init || !data) return ESP_ERR_INVALID_STATE;

    s_ring[s_head] = *data;
    s_head = (s_head + 1) % DATA_STORE_MAX_ENTRIES;
    if (s_count < DATA_STORE_MAX_ENTRIES) {
        s_count++;
    }

    return ESP_OK;
}

size_t data_store_count(void)
{
    return s_count;
}

esp_err_t data_store_read(size_t index, hal_sensor_data_t *data)
{
    if (!s_init || !data) return ESP_ERR_INVALID_STATE;
    if (index >= s_count) return ESP_ERR_INVALID_ARG;

    /* oldest entry is at (head - count) mod capacity */
    size_t oldest = (s_head + DATA_STORE_MAX_ENTRIES - s_count) % DATA_STORE_MAX_ENTRIES;
    size_t actual = (oldest + index) % DATA_STORE_MAX_ENTRIES;
    *data = s_ring[actual];
    return ESP_OK;
}

esp_err_t data_store_read_latest(hal_sensor_data_t *data)
{
    if (!s_init || !data) return ESP_ERR_INVALID_STATE;
    if (s_count == 0) return ESP_ERR_NOT_FOUND;

    size_t latest = (s_head + DATA_STORE_MAX_ENTRIES - 1) % DATA_STORE_MAX_ENTRIES;
    *data = s_ring[latest];
    return ESP_OK;
}

esp_err_t data_store_flush(void)
{
    /* No-op for RAM store */
    return ESP_OK;
}

esp_err_t data_store_deinit(void)
{
    s_init  = false;
    s_count = 0;
    s_head  = 0;
    ESP_LOGI(TAG, "Mock data store de-initialised");
    return ESP_OK;
}
