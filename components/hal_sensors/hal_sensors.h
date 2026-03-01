#pragma once
/**
 * hal_sensors.h — Hardware Abstraction Layer for all sensors.
 *
 * This abstracts away the I2C drivers so the application code never touches
 * driver handles directly. In mock mode, synthetic data is returned instead
 * of reading from real hardware.
 *
 * Backend selected at compile time via CONFIG_HAL_USE_MOCK in Kconfig.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Pull in the data types from drivers so the app doesn't need to include them */
#include "sen66.h"
#include "bmp851.h"
#include "ism330dhc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Complete sensor snapshot from all sensors on the board. */
typedef struct {
    int64_t          timestamp_us; /*!< esp_timer_get_time() at read */
    sen66_data_t     air;          /*!< SEN66: PM, T, RH, VOC, NOx */
    bmp851_data_t    baro;         /*!< BMP851: pressure, temperature */
    ism330dhc_vec3_t accel;        /*!< ISM330DHC: accelerometer [m/s²] */
    ism330dhc_vec3_t gyro;         /*!< ISM330DHC: gyroscope [dps] */
    bool             pwrgd;        /*!< Power good signal */
} hal_sensor_data_t;

/** Per-sensor init status, reported after hal_sensors_init(). */
typedef struct {
    bool sen66_ok;
    bool bmp851_ok;
    bool ism330dhc_ok;
    bool i2c_ok;
} hal_sensor_status_t;

/**
 * @brief  Initialise all sensors (or mock backends).
 *
 * In real mode: configures I2C bus, probes each sensor, starts SEN66
 * continuous measurement.
 *
 * In mock mode: no hardware init; always succeeds.
 *
 * @param[out] status  Per-sensor success flags (optional, pass NULL to skip).
 * @return     ESP_OK if at least one sensor is usable.
 */
esp_err_t hal_sensors_init(hal_sensor_status_t *status);

/**
 * @brief  Read all sensors into a snapshot.
 *
 * In real mode: polls I2C sensors, reads power-good GPIO.
 * In mock mode: generates synthetic data (sine wave temp, random PM, etc.)
 *
 * @param[out] data  Output snapshot. Timestamp is always filled.
 * @return     ESP_OK.
 */
esp_err_t hal_sensors_read(hal_sensor_data_t *data);

/**
 * @brief  De-initialise all sensors.
 */
esp_err_t hal_sensors_deinit(void);

#ifdef __cplusplus
}
#endif
