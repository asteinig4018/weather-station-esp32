/**
 * hal_sensors_mock.c — Mock backend for hal_sensors.
 *
 * Compiled only when CONFIG_HAL_USE_MOCK is set.
 * Returns synthetic, deterministic sensor data for QEMU and CI testing.
 *
 * Data patterns:
 *   - Temperature: sine wave 18–28 °C, period ~60 s
 *   - Humidity: sine wave 30–70 %, offset from temp
 *   - Pressure: slow drift around 101325 Pa
 *   - PM2.5: periodic spike (normal ~5, spike to ~45 every 30 s)
 *   - Accel: gravity (0, 0, 9.81) + small noise
 *   - Gyro: near-zero + small noise
 *   - VOC/NOx: slow ramp
 */

#include "hal_sensors.h"

#include <math.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "hal_sensors_mock";

static uint32_t s_read_count = 0;

esp_err_t hal_sensors_init(hal_sensor_status_t *status)
{
    ESP_LOGW(TAG, "*** MOCK MODE — no real hardware ***");

    if (status) {
        status->i2c_ok      = true;
        status->sen66_ok    = true;
        status->bmp851_ok   = true;
        status->ism330dhc_ok = true;
    }

    s_read_count = 0;
    return ESP_OK;
}

esp_err_t hal_sensors_read(hal_sensor_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    memset(data, 0, sizeof(*data));

    data->timestamp_us = esp_timer_get_time();
    double t = (double)s_read_count;
    s_read_count++;

    /* Temperature: 23 ± 5 °C sine, period ~60 reads */
    data->baro.temp_c  = 23.0f + 5.0f * sinf((float)(t * 2.0 * M_PI / 60.0));
    data->air.temp_c   = data->baro.temp_c + 0.5f; /* SEN66 reads slightly warmer */

    /* Humidity: 50 ± 20 %, anti-phase to temp */
    data->air.hum_pct = 50.0f + 20.0f * sinf((float)(t * 2.0 * M_PI / 60.0 + M_PI));

    /* Pressure: 101325 ± 200 Pa, slow drift */
    data->baro.pressure_pa = 101325.0f + 200.0f * sinf((float)(t * 2.0 * M_PI / 300.0));

    /* PM2.5: base ~5, spike to ~45 every 30 reads */
    float pm_base = 5.0f + 2.0f * sinf((float)(t * 2.0 * M_PI / 20.0));
    int cycle = s_read_count % 30;
    if (cycle >= 25 && cycle <= 28) {
        pm_base += 40.0f; /* spike */
    }
    data->air.pm1p0 = pm_base * 0.6f;
    data->air.pm2p5 = pm_base;
    data->air.pm4p0 = pm_base * 1.1f;
    data->air.pm10  = pm_base * 1.2f;

    /* VOC/NOx: slow ramp 50–200 */
    data->air.voc = 50.0f + 150.0f * (0.5f + 0.5f * sinf((float)(t * 2.0 * M_PI / 120.0)));
    data->air.nox = 10.0f + 30.0f * (0.5f + 0.5f * sinf((float)(t * 2.0 * M_PI / 180.0)));

    /* Accel: gravity pointing down + small oscillation */
    data->accel.x = 0.05f * sinf((float)(t * 0.3));
    data->accel.y = 0.03f * cosf((float)(t * 0.5));
    data->accel.z = 9.81f + 0.02f * sinf((float)(t * 0.2));

    /* Gyro: near-zero with tiny drift */
    data->gyro.x = 0.1f * sinf((float)(t * 0.1));
    data->gyro.y = 0.1f * cosf((float)(t * 0.15));
    data->gyro.z = 0.05f * sinf((float)(t * 0.08));

    /* Power good: always true in mock */
    data->pwrgd = true;

    return ESP_OK;
}

esp_err_t hal_sensors_deinit(void)
{
    ESP_LOGI(TAG, "Mock sensors deinit (noop)");
    return ESP_OK;
}
