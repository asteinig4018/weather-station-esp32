#pragma once
/**
 * sen66.h — Driver for the Sensirion SEN66 environmental sensor.
 *
 * Measures: PM1.0, PM2.5, PM4.0, PM10, temperature, relative humidity,
 *           VOC index, NOx index.
 *
 * Interface: Sensirion SEI2C (I2C-based, 16-bit commands, CRC-8 per word).
 * Address  : 0x6B (fixed).
 *
 * Datasheet commands used:
 *   0x0021  Start Continuous Measurement
 *   0x0104  Stop Measurement
 *   0x0202  Get Data Ready Flag
 *   0x03C4  Read Measurement
 *   0xD304  Reset
 *   0xD313  Get Serial Number
 */

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Raw measurement output
 *
 * Conversion (per SEN66 datasheet):
 *   pm1p0  [µg/m³] = raw_pm1p0  / 10.0f
 *   pm2p5  [µg/m³] = raw_pm2p5  / 10.0f
 *   pm4p0  [µg/m³] = raw_pm4p0  / 10.0f
 *   pm10   [µg/m³] = raw_pm10   / 10.0f
 *   temp   [°C]    = raw_temp   / 200.0f
 *   hum    [%RH]   = raw_hum    / 100.0f
 *   voc              0–500 index (raw = scaled)
 *   nox              1–500 index (raw = scaled)
 * ------------------------------------------------------------------------- */
typedef struct {
    uint16_t raw_pm1p0;
    uint16_t raw_pm2p5;
    uint16_t raw_pm4p0;
    uint16_t raw_pm10;
    int16_t  raw_temp;
    uint16_t raw_hum;
    int16_t  raw_voc;
    int16_t  raw_nox;
} sen66_raw_t;

typedef struct {
    float pm1p0;   /*!< PM1.0  µg/m³ */
    float pm2p5;   /*!< PM2.5  µg/m³ */
    float pm4p0;   /*!< PM4.0  µg/m³ */
    float pm10;    /*!< PM10   µg/m³ */
    float temp_c;  /*!< Temperature °C (from SEN66 internal sensor) */
    float hum_pct; /*!< Relative humidity % */
    float voc;     /*!< VOC index (1–500) */
    float nox;     /*!< NOx index (1–500) */
} sen66_data_t;

/* -------------------------------------------------------------------------
 * Configuration
 * ------------------------------------------------------------------------- */
typedef struct {
    i2c_port_t i2c_port;   /*!< I2C peripheral number */
    uint8_t    i2c_addr;   /*!< Default: 0x6B */
} sen66_config_t;

#define SEN66_ADDR_DEFAULT  0x6B

#define SEN66_CONFIG_DEFAULT(port) { \
    .i2c_port = (port),             \
    .i2c_addr = SEN66_ADDR_DEFAULT, \
}

typedef struct sen66_dev *sen66_handle_t;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/**
 * @brief  Allocate driver state, verify device presence.
 *
 * Does NOT start measurement — call sen66_start_measurement() after.
 */
esp_err_t sen66_init(const sen66_config_t *cfg, sen66_handle_t *handle);

/** @brief  Send device reset command and wait for restart (~10 ms). */
esp_err_t sen66_reset(sen66_handle_t handle);

/** @brief  Start continuous measurement mode (≥ 1 s between reads). */
esp_err_t sen66_start_measurement(sen66_handle_t handle);

/** @brief  Stop continuous measurement mode. */
esp_err_t sen66_stop_measurement(sen66_handle_t handle);

/**
 * @brief  Poll data-ready flag.
 *
 * @param[out] ready  true when fresh data is available.
 */
esp_err_t sen66_data_ready(sen66_handle_t handle, bool *ready);

/**
 * @brief  Read latest measurement.
 *
 * Call sen66_data_ready() first; reading before data is ready returns stale
 * or zeroed values.
 *
 * @param[out] raw   Raw integer values (optional, pass NULL to skip).
 * @param[out] data  Converted floating-point values (optional).
 */
esp_err_t sen66_read_measurement(sen66_handle_t handle,
                                  sen66_raw_t *raw,
                                  sen66_data_t *data);

/** @brief  Free driver resources. */
esp_err_t sen66_deinit(sen66_handle_t handle);

#ifdef __cplusplus
}
#endif
