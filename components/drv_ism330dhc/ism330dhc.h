#pragma once
/**
 * ism330dhc.h — Driver for the ST ISM330DHC 6-axis IMU (accel + gyro).
 *
 * Interface: I2C, address 0x6A (SDO=GND) or 0x6B (SDO=VDD).
 *
 * Features used:
 *   - Accelerometer: ±2g, ±4g, ±8g, ±16g, ODR 12.5–6664 Hz
 *   - Gyroscope:     ±125–2000 dps, ODR 12.5–6664 Hz
 *
 * Note: ST also makes ISM330DHCX — register map is identical for the
 *       registers used here.
 */

#include <stdint.h>
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Scale / ODR enumerations
 * ------------------------------------------------------------------------- */

typedef enum {
    ISM330DHC_ACCEL_FS_2G  = 0,
    ISM330DHC_ACCEL_FS_16G = 1,
    ISM330DHC_ACCEL_FS_4G  = 2,
    ISM330DHC_ACCEL_FS_8G  = 3,
} ism330dhc_accel_fs_t;

typedef enum {
    ISM330DHC_GYRO_FS_125DPS  = 1,
    ISM330DHC_GYRO_FS_250DPS  = 0,
    ISM330DHC_GYRO_FS_500DPS  = 2,
    ISM330DHC_GYRO_FS_1000DPS = 4,
    ISM330DHC_GYRO_FS_2000DPS = 6,
} ism330dhc_gyro_fs_t;

typedef enum {
    ISM330DHC_ODR_OFF    = 0,
    ISM330DHC_ODR_12HZ5  = 1,
    ISM330DHC_ODR_26HZ   = 2,
    ISM330DHC_ODR_52HZ   = 3,
    ISM330DHC_ODR_104HZ  = 4,
    ISM330DHC_ODR_208HZ  = 5,
    ISM330DHC_ODR_417HZ  = 6,
    ISM330DHC_ODR_833HZ  = 7,
} ism330dhc_odr_t;

/* -------------------------------------------------------------------------
 * Configuration
 * ------------------------------------------------------------------------- */

typedef struct {
    i2c_port_t          i2c_port;
    uint8_t             i2c_addr;    /*!< 0x6A or 0x6B */
    ism330dhc_accel_fs_t accel_fs;
    ism330dhc_gyro_fs_t  gyro_fs;
    ism330dhc_odr_t      accel_odr;
    ism330dhc_odr_t      gyro_odr;
} ism330dhc_config_t;

#define ISM330DHC_ADDR_SDO_LOW   0x6A
#define ISM330DHC_ADDR_SDO_HIGH  0x6B

#define ISM330DHC_CONFIG_DEFAULT(port) {        \
    .i2c_port  = (port),                        \
    .i2c_addr  = ISM330DHC_ADDR_SDO_LOW,        \
    .accel_fs  = ISM330DHC_ACCEL_FS_4G,         \
    .gyro_fs   = ISM330DHC_GYRO_FS_500DPS,      \
    .accel_odr = ISM330DHC_ODR_104HZ,           \
    .gyro_odr  = ISM330DHC_ODR_104HZ,           \
}

/* -------------------------------------------------------------------------
 * Data types
 * ------------------------------------------------------------------------- */

typedef struct {
    float x, y, z;  /*!< m/s² or dps depending on sensor */
} ism330dhc_vec3_t;

typedef struct ism330dhc_dev *ism330dhc_handle_t;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/**
 * @brief  Initialise the ISM330DHC (checks WHO_AM_I, configures ODR/FS).
 */
esp_err_t ism330dhc_init(const ism330dhc_config_t *cfg,
                          ism330dhc_handle_t *handle);

/**
 * @brief  Read raw accelerometer (converted to m/s²).
 */
esp_err_t ism330dhc_read_accel(ism330dhc_handle_t handle,
                                ism330dhc_vec3_t *accel);

/**
 * @brief  Read raw gyroscope (converted to degrees per second).
 */
esp_err_t ism330dhc_read_gyro(ism330dhc_handle_t handle,
                               ism330dhc_vec3_t *gyro);

/**
 * @brief  Read temperature from the IMU's internal sensor (°C).
 *
 * Accuracy is ±3 °C typically — prefer the BMP851 for precision.
 */
esp_err_t ism330dhc_read_temp(ism330dhc_handle_t handle, float *temp_c);

/** @brief  Free driver resources. */
esp_err_t ism330dhc_deinit(ism330dhc_handle_t handle);

#ifdef __cplusplus
}
#endif
