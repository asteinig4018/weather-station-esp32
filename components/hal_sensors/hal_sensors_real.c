/**
 * hal_sensors_real.c — Real hardware backend for hal_sensors.
 *
 * Compiled only when CONFIG_HAL_USE_MOCK is NOT set.
 * Initialises the I2C bus and all three sensor drivers.
 */

#include "hal_sensors.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"

#include "board.h"

static const char *TAG = "hal_sensors_real";

static sen66_handle_t     s_sen66  = NULL;
static bmp851_handle_t    s_bmp851 = NULL;
static ism330dhc_handle_t s_imu    = NULL;
static bool s_i2c_inited = false;

/* -------------------------------------------------------------------------
 * I2C bus init
 * ------------------------------------------------------------------------- */
static esp_err_t i2c_bus_init(void)
{
    if (s_i2c_inited) return ESP_OK;

    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = BOARD_I2C_SDA,
        .scl_io_num       = BOARD_I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(BOARD_I2C_PORT, &conf), TAG, "i2c_param_config");
    ESP_RETURN_ON_ERROR(i2c_driver_install(BOARD_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0),
                        TAG, "i2c_driver_install");
    s_i2c_inited = true;
    ESP_LOGI(TAG, "I2C bus %d ready (SDA=%d SCL=%d @ %lu Hz)",
             BOARD_I2C_PORT, BOARD_I2C_SDA, BOARD_I2C_SCL,
             (unsigned long)BOARD_I2C_FREQ_HZ);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

esp_err_t hal_sensors_init(hal_sensor_status_t *status)
{
    hal_sensor_status_t st = {0};

    /* I2C bus */
    st.i2c_ok = (i2c_bus_init() == ESP_OK);
    if (!st.i2c_ok) {
        ESP_LOGE(TAG, "I2C bus init failed — all sensors unavailable");
        if (status) *status = st;
        return ESP_FAIL;
    }

    /* ISM330DHC */
    ism330dhc_config_t imu_cfg = ISM330DHC_CONFIG_DEFAULT(BOARD_I2C_PORT);
    st.ism330dhc_ok = (ism330dhc_init(&imu_cfg, &s_imu) == ESP_OK);
    if (!st.ism330dhc_ok) {
        ESP_LOGW(TAG, "ISM330DHC init failed");
    }

    /* BMP851 */
    bmp851_config_t baro_cfg = BMP851_CONFIG_DEFAULT(BOARD_I2C_PORT);
    st.bmp851_ok = (bmp851_init(&baro_cfg, &s_bmp851) == ESP_OK);
    if (!st.bmp851_ok) {
        ESP_LOGW(TAG, "BMP851 init failed");
    }

    /* SEN66 */
    sen66_config_t air_cfg = SEN66_CONFIG_DEFAULT(BOARD_I2C_PORT);
    if (sen66_init(&air_cfg, &s_sen66) == ESP_OK) {
        sen66_start_measurement(s_sen66);
        st.sen66_ok = true;
        /* Wait for first valid frame */
        vTaskDelay(pdMS_TO_TICKS(1200));
    } else {
        ESP_LOGW(TAG, "SEN66 init failed");
    }

    if (status) *status = st;

    bool any = st.sen66_ok || st.bmp851_ok || st.ism330dhc_ok;
    ESP_LOGI(TAG, "Sensors init: SEN66=%s BMP851=%s ISM330DHC=%s",
             st.sen66_ok ? "OK" : "FAIL",
             st.bmp851_ok ? "OK" : "FAIL",
             st.ism330dhc_ok ? "OK" : "FAIL");

    return any ? ESP_OK : ESP_FAIL;
}

esp_err_t hal_sensors_read(hal_sensor_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    memset(data, 0, sizeof(*data));
    data->timestamp_us = esp_timer_get_time();

    /* SEN66 */
    if (s_sen66) {
        bool ready = false;
        if (sen66_data_ready(s_sen66, &ready) == ESP_OK && ready) {
            sen66_read_measurement(s_sen66, NULL, &data->air);
        }
    }

    /* BMP851 */
    if (s_bmp851) {
        bmp851_read(s_bmp851, &data->baro);
    }

    /* ISM330DHC */
    if (s_imu) {
        ism330dhc_read_accel(s_imu, &data->accel);
        ism330dhc_read_gyro(s_imu, &data->gyro);
    }

    /* Power good GPIO */
    data->pwrgd = gpio_get_level(BOARD_PWRGD);

    return ESP_OK;
}

esp_err_t hal_sensors_deinit(void)
{
    if (s_sen66) { sen66_deinit(s_sen66); s_sen66 = NULL; }
    if (s_bmp851) { bmp851_deinit(s_bmp851); s_bmp851 = NULL; }
    if (s_imu) { ism330dhc_deinit(s_imu); s_imu = NULL; }
    if (s_i2c_inited) {
        i2c_driver_delete(BOARD_I2C_PORT);
        s_i2c_inited = false;
    }
    return ESP_OK;
}
