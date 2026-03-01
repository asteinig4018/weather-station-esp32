/**
 * ism330dhc.c — ST ISM330DHC / ISM330DHCX 6-axis IMU driver.
 *
 * Register map reference: ST AN5278 / ISM330DHC datasheet Rev 4.
 */

#include "ism330dhc.h"

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "drv_ism330dhc";

/* -------------------------------------------------------------------------
 * Register addresses
 * ------------------------------------------------------------------------- */
#define REG_WHO_AM_I    0x0F  /* Expected: 0x6B */
#define REG_CTRL1_XL    0x10  /* Accel: ODR[7:4] | FS[3:2] | LPF2[1] | ... */
#define REG_CTRL2_G     0x11  /* Gyro:  ODR[7:4] | FS[3:1] */
#define REG_CTRL3_C     0x12  /* SW_RESET[0], BDU[6] */
#define REG_STATUS_REG  0x1E  /* TDA[2] | GDA[1] | XLDA[0] */
#define REG_OUT_TEMP_L  0x20  /* Temperature low byte */
#define REG_OUT_TEMP_H  0x21
#define REG_OUTX_L_G    0x22  /* Gyro X low byte */
#define REG_OUTX_H_G    0x23
#define REG_OUTY_L_G    0x24
#define REG_OUTY_H_G    0x25
#define REG_OUTZ_L_G    0x26
#define REG_OUTZ_H_G    0x27
#define REG_OUTX_L_A    0x28  /* Accel X low byte */
#define REG_OUTX_H_A    0x29
#define REG_OUTY_L_A    0x2A
#define REG_OUTY_H_A    0x2B
#define REG_OUTZ_L_A    0x2C
#define REG_OUTZ_H_A    0x2D

#define WHO_AM_I_VALUE  0x6B
#define I2C_TIMEOUT_MS  50

/* -------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */
struct ism330dhc_dev {
    i2c_port_t port;
    uint8_t    addr;
    float      accel_scale;  /* LSB → m/s² */
    float      gyro_scale;   /* LSB → dps  */
};

/* -------------------------------------------------------------------------
 * Low-level register I/O
 * ------------------------------------------------------------------------- */
static esp_err_t reg_write(struct ism330dhc_dev *dev, uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (dev->addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(h, reg, true);
    i2c_master_write_byte(h, val, true);
    i2c_master_stop(h);
    esp_err_t ret = i2c_master_cmd_begin(dev->port, h,
                                          pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(h);
    return ret;
}

static esp_err_t reg_read(struct ism330dhc_dev *dev,
                           uint8_t reg, uint8_t *out, size_t len)
{
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (dev->addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(h, reg, true);
    i2c_master_start(h);
    i2c_master_write_byte(h, (dev->addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(h, out, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(h, out + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(h);
    esp_err_t ret = i2c_master_cmd_begin(dev->port, h,
                                          pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(h);
    return ret;
}

/* -------------------------------------------------------------------------
 * Scale helpers
 * ------------------------------------------------------------------------- */
static float accel_fs_to_scale(ism330dhc_accel_fs_t fs)
{
    /* Full-scale → mg/LSB (from datasheet table), then × 9.80665e-3 → m/s² */
    float mg_per_lsb;
    switch (fs) {
        case ISM330DHC_ACCEL_FS_2G:  mg_per_lsb = 0.061f; break;
        case ISM330DHC_ACCEL_FS_4G:  mg_per_lsb = 0.122f; break;
        case ISM330DHC_ACCEL_FS_8G:  mg_per_lsb = 0.244f; break;
        case ISM330DHC_ACCEL_FS_16G: mg_per_lsb = 0.488f; break;
        default:                     mg_per_lsb = 0.122f; break;
    }
    return mg_per_lsb * 9.80665e-3f;
}

static float gyro_fs_to_scale(ism330dhc_gyro_fs_t fs)
{
    /* mdps/LSB → dps/LSB */
    float mdps_per_lsb;
    switch (fs) {
        case ISM330DHC_GYRO_FS_125DPS:  mdps_per_lsb = 4.375f; break;
        case ISM330DHC_GYRO_FS_250DPS:  mdps_per_lsb = 8.75f;  break;
        case ISM330DHC_GYRO_FS_500DPS:  mdps_per_lsb = 17.5f;  break;
        case ISM330DHC_GYRO_FS_1000DPS: mdps_per_lsb = 35.0f;  break;
        case ISM330DHC_GYRO_FS_2000DPS: mdps_per_lsb = 70.0f;  break;
        default:                        mdps_per_lsb = 17.5f;  break;
    }
    return mdps_per_lsb * 1e-3f;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

esp_err_t ism330dhc_init(const ism330dhc_config_t *cfg,
                          ism330dhc_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(cfg && out_handle, ESP_ERR_INVALID_ARG, TAG, "NULL");
    esp_err_t ret = ESP_OK;

    struct ism330dhc_dev *dev = calloc(1, sizeof(*dev));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "calloc");
    dev->port = cfg->i2c_port;
    dev->addr = cfg->i2c_addr;

    /* WHO_AM_I check */
    uint8_t who = 0;
    ESP_GOTO_ON_ERROR(reg_read(dev, REG_WHO_AM_I, &who, 1),
                      err, TAG, "who_am_i read");
    if (who != WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "WHO_AM_I mismatch: got 0x%02X, expected 0x%02X",
                 who, WHO_AM_I_VALUE);
        goto err;
    }

    /* Software reset */
    ESP_GOTO_ON_ERROR(reg_write(dev, REG_CTRL3_C, 0x01), err, TAG, "sw_reset");
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Enable block data update (BDU=1) */
    ESP_GOTO_ON_ERROR(reg_write(dev, REG_CTRL3_C, 0x40), err, TAG, "bdu");

    /* Configure accelerometer: ODR | FS */
    uint8_t ctrl1 = (cfg->accel_odr << 4) | (cfg->accel_fs << 2);
    ESP_GOTO_ON_ERROR(reg_write(dev, REG_CTRL1_XL, ctrl1), err, TAG, "ctrl1_xl");

    /* Configure gyroscope: ODR | FS */
    uint8_t ctrl2 = (cfg->gyro_odr << 4) | (cfg->gyro_fs << 1);
    ESP_GOTO_ON_ERROR(reg_write(dev, REG_CTRL2_G, ctrl2), err, TAG, "ctrl2_g");

    dev->accel_scale = accel_fs_to_scale(cfg->accel_fs);
    dev->gyro_scale  = gyro_fs_to_scale(cfg->gyro_fs);

    ESP_LOGI(TAG, "ISM330DHC ready at 0x%02X", dev->addr);
    *out_handle = dev;
    return ESP_OK;

err:
    free(dev);
    return ret != ESP_OK ? ret : ESP_FAIL;
}

esp_err_t ism330dhc_read_accel(ism330dhc_handle_t handle,
                                ism330dhc_vec3_t *accel)
{
    ESP_RETURN_ON_FALSE(handle && accel, ESP_ERR_INVALID_ARG, TAG, "NULL");
    uint8_t buf[6];
    ESP_RETURN_ON_ERROR(reg_read(handle, REG_OUTX_L_A, buf, 6),
                        TAG, "read_accel");
    int16_t rx = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t ry = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t rz = (int16_t)((buf[5] << 8) | buf[4]);
    accel->x = rx * handle->accel_scale;
    accel->y = ry * handle->accel_scale;
    accel->z = rz * handle->accel_scale;
    return ESP_OK;
}

esp_err_t ism330dhc_read_gyro(ism330dhc_handle_t handle,
                               ism330dhc_vec3_t *gyro)
{
    ESP_RETURN_ON_FALSE(handle && gyro, ESP_ERR_INVALID_ARG, TAG, "NULL");
    uint8_t buf[6];
    ESP_RETURN_ON_ERROR(reg_read(handle, REG_OUTX_L_G, buf, 6),
                        TAG, "read_gyro");
    int16_t rx = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t ry = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t rz = (int16_t)((buf[5] << 8) | buf[4]);
    gyro->x = rx * handle->gyro_scale;
    gyro->y = ry * handle->gyro_scale;
    gyro->z = rz * handle->gyro_scale;
    return ESP_OK;
}

esp_err_t ism330dhc_read_temp(ism330dhc_handle_t handle, float *temp_c)
{
    ESP_RETURN_ON_FALSE(handle && temp_c, ESP_ERR_INVALID_ARG, TAG, "NULL");
    uint8_t buf[2];
    ESP_RETURN_ON_ERROR(reg_read(handle, REG_OUT_TEMP_L, buf, 2),
                        TAG, "read_temp");
    int16_t raw = (int16_t)((buf[1] << 8) | buf[0]);
    /* ISM330DHC: 256 LSB/°C, offset = 25 °C */
    *temp_c = (float)raw / 256.0f + 25.0f;
    return ESP_OK;
}

esp_err_t ism330dhc_deinit(ism330dhc_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    /* Power down accel and gyro */
    reg_write(handle, REG_CTRL1_XL, 0x00);
    reg_write(handle, REG_CTRL2_G,  0x00);
    free(handle);
    return ESP_OK;
}
