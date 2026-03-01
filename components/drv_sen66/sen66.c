/**
 * sen66.c — Sensirion SEN66 I2C driver.
 *
 * SEI2C protocol:
 *   Write: [addr_w | 2-byte cmd | data bytes + CRC-8 per 2-byte word]
 *   Read:  [addr_w | 2-byte cmd] → [addr_r | data bytes with CRC-8 per word]
 *
 * CRC polynomial: 0x31, init: 0xFF, no reflect, no XOR-out.
 */

#include "sen66.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "drv_sen66";

/* -------------------------------------------------------------------------
 * SEI2C command codes (16-bit big-endian)
 * ------------------------------------------------------------------------- */
#define CMD_START_MEAS    0x0021
#define CMD_STOP_MEAS     0x0104
#define CMD_DATA_READY    0x0202
#define CMD_READ_MEAS     0x03C4
#define CMD_RESET         0xD304
#define CMD_GET_SERIAL    0xD313

#define I2C_TIMEOUT_MS    100

/* -------------------------------------------------------------------------
 * CRC-8 per Sensirion spec  (poly=0x31, init=0xFF)
 * ------------------------------------------------------------------------- */
static uint8_t sen66_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
        }
    }
    return crc;
}

/* -------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */
struct sen66_dev {
    i2c_port_t port;
    uint8_t    addr;
};

/* -------------------------------------------------------------------------
 * Low-level I2C helpers
 * ------------------------------------------------------------------------- */

static esp_err_t sei2c_send_cmd(struct sen66_dev *dev, uint16_t cmd)
{
    uint8_t buf[2] = { cmd >> 8, cmd & 0xFF };
    i2c_cmd_handle_t h = i2c_cmd_link_create();
    i2c_master_start(h);
    i2c_master_write_byte(h, (dev->addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(h, buf, 2, true);
    i2c_master_stop(h);
    esp_err_t ret = i2c_master_cmd_begin(dev->port, h,
                                          pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(h);
    return ret;
}

/**
 * Read `n_words` 16-bit words from the device after issuing `cmd`.
 * Each word is followed by a CRC byte; this function checks all CRCs.
 * out[] receives the raw big-endian uint16 values (already decoded).
 */
static esp_err_t sei2c_read_words(struct sen66_dev *dev,
                                   uint16_t cmd,
                                   uint16_t *out, size_t n_words)
{
    /* Send command */
    {
        uint8_t cbuf[2] = { cmd >> 8, cmd & 0xFF };
        i2c_cmd_handle_t h = i2c_cmd_link_create();
        i2c_master_start(h);
        i2c_master_write_byte(h, (dev->addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write(h, cbuf, 2, true);
        i2c_master_stop(h);
        esp_err_t ret = i2c_master_cmd_begin(dev->port, h,
                                              pdMS_TO_TICKS(I2C_TIMEOUT_MS));
        i2c_cmd_link_delete(h);
        ESP_RETURN_ON_ERROR(ret, TAG, "send cmd 0x%04X", cmd);
    }

    /* Small gap required by SEI2C spec */
    vTaskDelay(1);

    /* Read: each word = 2 data bytes + 1 CRC byte = 3 bytes */
    size_t rx_len = n_words * 3;
    uint8_t *rx = malloc(rx_len);
    ESP_RETURN_ON_FALSE(rx, ESP_ERR_NO_MEM, TAG, "malloc rx");

    {
        i2c_cmd_handle_t h = i2c_cmd_link_create();
        i2c_master_start(h);
        i2c_master_write_byte(h, (dev->addr << 1) | I2C_MASTER_READ, true);
        i2c_master_read(h, rx, rx_len, I2C_MASTER_LAST_NACK);
        i2c_master_stop(h);
        esp_err_t ret = i2c_master_cmd_begin(dev->port, h,
                                              pdMS_TO_TICKS(I2C_TIMEOUT_MS));
        i2c_cmd_link_delete(h);
        if (ret != ESP_OK) {
            free(rx);
            ESP_RETURN_ON_ERROR(ret, TAG, "read words");
        }
    }

    /* Decode + CRC check */
    for (size_t w = 0; w < n_words; w++) {
        uint8_t d0  = rx[w * 3];
        uint8_t d1  = rx[w * 3 + 1];
        uint8_t crc = rx[w * 3 + 2];
        uint8_t calc = sen66_crc8((uint8_t[]){d0, d1}, 2);
        if (calc != crc) {
            ESP_LOGE(TAG, "CRC error word %u: got 0x%02X expect 0x%02X",
                     (unsigned)w, crc, calc);
            free(rx);
            return ESP_ERR_INVALID_CRC;
        }
        out[w] = ((uint16_t)d0 << 8) | d1;
    }

    free(rx);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

esp_err_t sen66_init(const sen66_config_t *cfg, sen66_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(cfg && out_handle, ESP_ERR_INVALID_ARG, TAG, "NULL arg");

    struct sen66_dev *dev = calloc(1, sizeof(*dev));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "calloc");
    dev->port = cfg->i2c_port;
    dev->addr = cfg->i2c_addr;

    /* Quick presence check: send reset and verify no NAK */
    esp_err_t ret = sei2c_send_cmd(dev, CMD_RESET);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SEN66 not found at 0x%02X (err %s)", dev->addr,
                 esp_err_to_name(ret));
        free(dev);
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "SEN66 found at 0x%02X", dev->addr);
    *out_handle = dev;
    return ESP_OK;
}

esp_err_t sen66_reset(sen66_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "NULL");
    ESP_RETURN_ON_ERROR(sei2c_send_cmd(handle, CMD_RESET), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

esp_err_t sen66_start_measurement(sen66_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "NULL");
    return sei2c_send_cmd(handle, CMD_START_MEAS);
}

esp_err_t sen66_stop_measurement(sen66_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "NULL");
    return sei2c_send_cmd(handle, CMD_STOP_MEAS);
}

esp_err_t sen66_data_ready(sen66_handle_t handle, bool *ready)
{
    ESP_RETURN_ON_FALSE(handle && ready, ESP_ERR_INVALID_ARG, TAG, "NULL");
    uint16_t flag = 0;
    ESP_RETURN_ON_ERROR(sei2c_read_words(handle, CMD_DATA_READY, &flag, 1),
                        TAG, "data_ready");
    *ready = (flag & 0x0001) != 0;
    return ESP_OK;
}

esp_err_t sen66_read_measurement(sen66_handle_t handle,
                                  sen66_raw_t *raw_out,
                                  sen66_data_t *data_out)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "NULL");

    /* 8 words: PM1.0, PM2.5, PM4.0, PM10, Temp, Hum, VOC, NOx */
    uint16_t words[8] = {0};
    ESP_RETURN_ON_ERROR(sei2c_read_words(handle, CMD_READ_MEAS, words, 8),
                        TAG, "read_meas");

    if (raw_out) {
        raw_out->raw_pm1p0 = words[0];
        raw_out->raw_pm2p5 = words[1];
        raw_out->raw_pm4p0 = words[2];
        raw_out->raw_pm10  = words[3];
        raw_out->raw_temp  = (int16_t)words[4];
        raw_out->raw_hum   = words[5];
        raw_out->raw_voc   = (int16_t)words[6];
        raw_out->raw_nox   = (int16_t)words[7];
    }

    if (data_out) {
        data_out->pm1p0   = words[0] / 10.0f;
        data_out->pm2p5   = words[1] / 10.0f;
        data_out->pm4p0   = words[2] / 10.0f;
        data_out->pm10    = words[3] / 10.0f;
        data_out->temp_c  = (int16_t)words[4] / 200.0f;
        data_out->hum_pct = words[5] / 100.0f;
        data_out->voc     = (int16_t)words[6] / 10.0f;
        data_out->nox     = (int16_t)words[7] / 10.0f;
    }

    return ESP_OK;
}

esp_err_t sen66_deinit(sen66_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    sen66_stop_measurement(handle);
    free(handle);
    return ESP_OK;
}
