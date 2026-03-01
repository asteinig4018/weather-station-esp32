#pragma once
/**
 * bmp851.h — Driver for the Bosch BMP851 barometric pressure / temperature sensor.
 *
 * Interface: I2C, address 0x76 (SDO=GND) or 0x77 (SDO=VDD).
 *
 * NOTE: The BMP851 is a relatively new Bosch part. If your device identifies
 *       itself with a different chip ID than expected (REG_CHIP_ID = 0x50 assumed),
 *       check the datasheet and update CHIP_ID_VALUE below. The compensation
 *       math here follows the BMP3xx family pattern (BMP388/390); adjust
 *       coefficient extraction if the BMP851 datasheet differs.
 *
 * Outputs: pressure (Pa), temperature (°C).
 */

#include <stdint.h>
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Configuration
 * ------------------------------------------------------------------------- */
typedef struct {
    i2c_port_t i2c_port;
    uint8_t    i2c_addr;  /*!< 0x76 or 0x77 */
} bmp851_config_t;

#define BMP851_ADDR_SDO_LOW   0x76
#define BMP851_ADDR_SDO_HIGH  0x77

#define BMP851_CONFIG_DEFAULT(port) {       \
    .i2c_port = (port),                     \
    .i2c_addr = BMP851_ADDR_SDO_LOW,        \
}

/* -------------------------------------------------------------------------
 * Data
 * ------------------------------------------------------------------------- */
typedef struct {
    float pressure_pa;  /*!< Barometric pressure in Pascal */
    float temp_c;       /*!< Temperature in °C */
} bmp851_data_t;

typedef struct bmp851_dev *bmp851_handle_t;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/**
 * @brief  Initialise the BMP851: verify chip ID, read calibration coefficients,
 *         configure oversampling and output data rate.
 */
esp_err_t bmp851_init(const bmp851_config_t *cfg, bmp851_handle_t *handle);

/**
 * @brief  Trigger a forced-mode measurement and block until ready (~10 ms),
 *         then read compensated pressure and temperature.
 */
esp_err_t bmp851_read(bmp851_handle_t handle, bmp851_data_t *data);

/** @brief  Free driver resources. */
esp_err_t bmp851_deinit(bmp851_handle_t handle);

#ifdef __cplusplus
}
#endif
