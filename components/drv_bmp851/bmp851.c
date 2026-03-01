/**
 * bmp851.c — Bosch BMP851 I2C driver.
 *
 * Register map and compensation formulae follow the BMP388/BMP390 pattern.
 * Update CHIP_ID_VALUE and coefficient extraction if BMP851 datasheet differs.
 *
 * TODO: Cross-reference the BMP851 datasheet when it becomes available and
 *       verify:
 *   1. REG_CHIP_ID value (0x50 assumed; BMP388 = 0x50, BMP390 = 0x60)
 *   2. Calibration register layout (NVM_PAR_* addresses below)
 *   3. Pressure / temperature compensation formula variants
 */

#include "bmp851.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "drv_bmp851";

/* -------------------------------------------------------------------------
 * Register map (BMP388/390 compatible — verify for BMP851)
 * ------------------------------------------------------------------------- */
#define REG_CHIP_ID     0x00  /* Expected chip ID */
#define REG_ERR         0x02
#define REG_STATUS      0x03
#define REG_DATA_0      0x04  /* Pressure XLSB */
#define REG_DATA_3      0x07  /* Temperature XLSB */
#define REG_PWR_CTRL    0x1B  /* press_en[0], temp_en[1], mode[5:4] */
#define REG_OSR         0x1C  /* osr_p[2:0], osr_t[5:3] */
#define REG_ODR         0x1D
#define REG_NVM_PAR     0x31  /* Start of calibration NVM regs */

#define CHIP_ID_VALUE   0x50  /* TODO: verify against BMP851 datasheet */

#define MODE_FORCED     0x10  /* Trigger single measurement */
#define STATUS_DRDY     0x60  /* cmd_rdy[4], drdy_press[5], drdy_temp[6] */

#define I2C_TIMEOUT_MS  50

/* -------------------------------------------------------------------------
 * Calibration coefficients (BMP388/390 layout)
 * ------------------------------------------------------------------------- */
typedef struct {
    double par_t1, par_t2, par_t3;
    double par_p1, par_p2, par_p3, par_p4, par_p5, par_p6;
    double par_p7, par_p8, par_p9, par_p10, par_p11;
} bmp851_calib_t;

/* -------------------------------------------------------------------------
 * Device state
 * ------------------------------------------------------------------------- */
struct bmp851_dev {
    i2c_port_t    port;
    uint8_t       addr;
    bmp851_calib_t calib;
};

/* -------------------------------------------------------------------------
 * Low-level I2C
 * ------------------------------------------------------------------------- */
static esp_err_t reg_write(struct bmp851_dev *dev, uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (dev->addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(h, reg, true);
    i2c_master_write_byte(h, val, true);
    i2c_master_stop(h);
    esp_err_t ret = i2c_master_cmd_begin(dev->port, h, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(h);
    return ret;
}

static esp_err_t reg_read(struct bmp851_dev *dev,
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
    esp_err_t ret = i2c_master_cmd_begin(dev->port, h, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(h);
    return ret;
}

/* -------------------------------------------------------------------------
 * Calibration loading (BMP388 layout — 21 bytes from 0x31)
 * ------------------------------------------------------------------------- */
static esp_err_t load_calibration(struct bmp851_dev *dev)
{
    uint8_t raw[21];
    ESP_RETURN_ON_ERROR(reg_read(dev, REG_NVM_PAR, raw, 21),
                        TAG, "read calib");

    /* Helper macros for signed/unsigned word extraction */
#define U16(lo, hi)  ((uint16_t)((raw[hi] << 8) | raw[lo]))
#define S16(lo, hi)  ((int16_t)U16(lo, hi))
#define S8(i)        ((int8_t)raw[i])

    bmp851_calib_t *c = &dev->calib;
    c->par_t1 = (double)U16(0,1)  / 1.6384e-2;
    c->par_t2 = (double)U16(2,3)  / 1.073741824e9;
    c->par_t3 = (double)S8(4)     / 2.81474976710656e14;

    c->par_p1 = ((double)S16(5,6)  - 16384.0) / 1.048576e6;
    c->par_p2 = ((double)S16(7,8)  - 16384.0) / 5.36870912e11;
    c->par_p3 = (double)S8(9)      / 4.294967296e9;
    c->par_p4 = (double)S8(10)     / 1.374389534720e14;
    c->par_p5 = (double)U16(11,12) / 1.6384e-2;
    c->par_p6 = (double)U16(13,14) / 6.4e4;
    c->par_p7 = (double)S8(15)     / 6.4e4;
    c->par_p8 = (double)S8(16)     / 3.2768e10;
    c->par_p9 = (double)S16(17,18) / 1.4073748835532800e15;
    c->par_p10= (double)S8(19)     / 2.81474976710656e14;
    c->par_p11= (double)S8(20)     / 3.6893488147419103e19;

#undef U16
#undef S16
#undef S8
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Compensation (BMP388 formulae — verify for BMP851)
 * ------------------------------------------------------------------------- */
static double compensate_temp(const bmp851_calib_t *c, uint32_t raw_t,
                               double *t_lin_out)
{
    double pd1 = (double)raw_t - c->par_t1;
    double pd2 = pd1 * c->par_t2;
    double t_lin = pd2 + (pd1 * pd1) * c->par_t3;
    if (t_lin_out) *t_lin_out = t_lin;
    return t_lin;
}

static double compensate_press(const bmp851_calib_t *c, uint32_t raw_p,
                                double t_lin)
{
    double pd1 = c->par_p6 * t_lin;
    double pd2 = c->par_p7 * (t_lin * t_lin);
    double pd3 = c->par_p8 * (t_lin * t_lin * t_lin);
    double po1 = c->par_p5 + pd1 + pd2 + pd3;

    pd1 = c->par_p2 * t_lin;
    pd2 = c->par_p3 * (t_lin * t_lin);
    pd3 = c->par_p4 * (t_lin * t_lin * t_lin);
    double po2 = (double)raw_p * (c->par_p1 + pd1 + pd2 + pd3);

    pd1 = (double)raw_p * (double)raw_p;
    pd2 = c->par_p9 + c->par_p10 * t_lin;
    pd3 = pd1 * pd2;
    double pd4 = pd3 + (double)raw_p * (double)raw_p * (double)raw_p * c->par_p11;

    return po1 + po2 + pd4;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

esp_err_t bmp851_init(const bmp851_config_t *cfg, bmp851_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(cfg && out_handle, ESP_ERR_INVALID_ARG, TAG, "NULL");
    esp_err_t ret = ESP_OK;

    struct bmp851_dev *dev = calloc(1, sizeof(*dev));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "calloc");
    dev->port = cfg->i2c_port;
    dev->addr = cfg->i2c_addr;

    /* Chip ID */
    uint8_t id = 0;
    ESP_GOTO_ON_ERROR(reg_read(dev, REG_CHIP_ID, &id, 1),
                      err, TAG, "chip_id read");
    if (id != CHIP_ID_VALUE) {
        ESP_LOGW(TAG, "BMP851 chip ID: 0x%02X (expected 0x%02X). "
                 "Proceeding — check TODO in bmp851.c if readings are wrong.",
                 id, CHIP_ID_VALUE);
    }

    /* Load calibration */
    ESP_GOTO_ON_ERROR(load_calibration(dev), err, TAG, "calib");

    /* Configure: OSR x2 pressure, x1 temp */
    ESP_GOTO_ON_ERROR(reg_write(dev, REG_OSR, 0x01), err, TAG, "osr");

    ESP_LOGI(TAG, "BMP851 ready at 0x%02X (chip_id=0x%02X)", dev->addr, id);
    *out_handle = dev;
    return ESP_OK;

err:
    free(dev);
    return ret != ESP_OK ? ret : ESP_FAIL;
}

esp_err_t bmp851_read(bmp851_handle_t handle, bmp851_data_t *data)
{
    ESP_RETURN_ON_FALSE(handle && data, ESP_ERR_INVALID_ARG, TAG, "NULL");

    /* Forced mode: enable pressure + temperature, trigger measurement */
    ESP_RETURN_ON_ERROR(
        reg_write(handle, REG_PWR_CTRL, 0x33),  /* temp_en | press_en | forced */
        TAG, "pwr_ctrl forced");

    /* Wait for measurement (~10 ms at OSR x2) */
    vTaskDelay(pdMS_TO_TICKS(15));

    /* Check data ready */
    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(reg_read(handle, REG_STATUS, &status, 1), TAG, "status");
    if ((status & STATUS_DRDY) != STATUS_DRDY) {
        ESP_LOGW(TAG, "Data not ready (STATUS=0x%02X)", status);
    }

    /* Read 6 bytes: 3 pressure + 3 temperature (XLSB, LSB, MSB each) */
    uint8_t raw[6];
    ESP_RETURN_ON_ERROR(reg_read(handle, REG_DATA_0, raw, 6), TAG, "read data");

    uint32_t raw_p = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16);
    uint32_t raw_t = (uint32_t)raw[3] | ((uint32_t)raw[4] << 8) | ((uint32_t)raw[5] << 16);

    double t_lin;
    double temp_c   = compensate_temp(&handle->calib, raw_t, &t_lin);
    double press_pa = compensate_press(&handle->calib, raw_p, t_lin);

    data->temp_c      = (float)temp_c;
    data->pressure_pa = (float)press_pa;

    return ESP_OK;
}

esp_err_t bmp851_deinit(bmp851_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    free(handle);
    return ESP_OK;
}
