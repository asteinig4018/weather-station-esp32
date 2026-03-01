/**
 * ili9341.c — ILI9341 driver over ESP32-S3 Intel 8080 8-bit parallel bus.
 *
 * Uses esp_lcd peripheral (LCD_CAM) built into ESP-IDF 5.x.
 * The ILI9341 init sequence is inlined here so no extra component is needed.
 */

#include "ili9341.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "drv_ili9341";

/* -------------------------------------------------------------------------
 * ILI9341 command / parameter bytes
 * ------------------------------------------------------------------------- */
#define ILI9341_CMD_NOP         0x00
#define ILI9341_CMD_SWRESET     0x01
#define ILI9341_CMD_SLPOUT      0x11
#define ILI9341_CMD_NORON       0x13
#define ILI9341_CMD_DISPON      0x29
#define ILI9341_CMD_CASET       0x2A
#define ILI9341_CMD_RASET       0x2B
#define ILI9341_CMD_RAMWR       0x2C
#define ILI9341_CMD_MADCTL      0x36
#define ILI9341_CMD_COLMOD      0x3A
#define ILI9341_CMD_FRMCTR1     0xB1
#define ILI9341_CMD_DFUNCTR     0xB6
#define ILI9341_CMD_PWCTR1      0xC0
#define ILI9341_CMD_PWCTR2      0xC1
#define ILI9341_CMD_VMCTR1      0xC5
#define ILI9341_CMD_VMCTR2      0xC7
#define ILI9341_CMD_GAMMASET    0x26
#define ILI9341_CMD_GMCTRP1     0xE0
#define ILI9341_CMD_GMCTRN1     0xE1

/* MADCTL bits */
#define MADCTL_MY   0x80
#define MADCTL_MX   0x40
#define MADCTL_MV   0x20
#define MADCTL_BGR  0x08

/* -------------------------------------------------------------------------
 * Internal device state
 * ------------------------------------------------------------------------- */
struct ili9341_dev {
    esp_lcd_i80_bus_handle_t  bus;
    esp_lcd_panel_io_handle_t io;
    ili9341_config_t          cfg;

    /* Scratch buffer for fill_rect (one row of pixels, heap-allocated) */
    uint16_t                 *row_buf;
    size_t                    row_buf_pixels;
};

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static esp_err_t lcd_cmd(esp_lcd_panel_io_handle_t io, uint8_t cmd,
                          const uint8_t *param, size_t param_len)
{
    return esp_lcd_panel_io_tx_param(io, cmd, param, param_len);
}

static void hw_reset(int rst_gpio)
{
    gpio_set_level(rst_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rst_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

/* -------------------------------------------------------------------------
 * ILI9341 full init sequence
 * Adapted from Adafruit / Espressif reference; suitable for the
 * NHD-2.4-240320CF module at Vcc = 3.3 V.
 * ------------------------------------------------------------------------- */
static esp_err_t ili9341_send_init_cmds(esp_lcd_panel_io_handle_t io,
                                         bool mirror_x, bool mirror_y,
                                         bool swap_xy)
{
    uint8_t madctl = MADCTL_BGR;
    if (mirror_x) madctl |= MADCTL_MX;
    if (mirror_y) madctl |= MADCTL_MY;
    if (swap_xy)  madctl |= MADCTL_MV;

    /* Power control */
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_PWCTR1, (uint8_t[]){0x23}, 1), TAG, "PWCTR1");
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_PWCTR2, (uint8_t[]){0x10}, 1), TAG, "PWCTR2");
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_VMCTR1, (uint8_t[]){0x3E, 0x28}, 2), TAG, "VMCTR1");
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_VMCTR2, (uint8_t[]){0x86}, 1), TAG, "VMCTR2");

    /* Memory access + pixel format */
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_MADCTL, &madctl, 1), TAG, "MADCTL");
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_COLMOD, (uint8_t[]){0x55}, 1), TAG, "COLMOD 16bpp");

    /* Frame rate: 79 Hz */
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_FRMCTR1, (uint8_t[]){0x00, 0x18}, 2), TAG, "FRMCTR1");

    /* Display function */
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_DFUNCTR, (uint8_t[]){0x08, 0x82, 0x27}, 3), TAG, "DFUNCTR");

    /* Gamma */
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_GAMMASET, (uint8_t[]){0x01}, 1), TAG, "GAMMASET");
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_GMCTRP1,
        (uint8_t[]){0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00}, 15),
        TAG, "GMCTRP1");
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_GMCTRN1,
        (uint8_t[]){0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F}, 15),
        TAG, "GMCTRN1");

    /* Exit sleep → display on */
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_SLPOUT, NULL, 0), TAG, "SLPOUT");
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_NORON,  NULL, 0), TAG, "NORON");
    ESP_RETURN_ON_ERROR(lcd_cmd(io, ILI9341_CMD_DISPON, NULL, 0), TAG, "DISPON");

    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

esp_err_t ili9341_init(const ili9341_config_t *cfg, ili9341_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(cfg && out_handle, ESP_ERR_INVALID_ARG, TAG, "NULL arg");
    esp_err_t ret = ESP_OK;

    struct ili9341_dev *dev = calloc(1, sizeof(*dev));
    ESP_RETURN_ON_FALSE(dev, ESP_ERR_NO_MEM, TAG, "calloc failed");
    memcpy(&dev->cfg, cfg, sizeof(*cfg));

    /* --- Hardware reset -------------------------------------------------- */
    if (cfg->rst_gpio >= 0) {
        gpio_config_t rst_cfg = {
            .pin_bit_mask = 1ULL << cfg->rst_gpio,
            .mode         = GPIO_MODE_OUTPUT,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&rst_cfg), err_free, TAG, "RST gpio_config");
        hw_reset(cfg->rst_gpio);
    }

    /* --- Backlight (off during init) ------------------------------------- */
    if (cfg->bl_gpio >= 0) {
        gpio_config_t bl_cfg = {
            .pin_bit_mask = 1ULL << cfg->bl_gpio,
            .mode         = GPIO_MODE_OUTPUT,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&bl_cfg), err_free, TAG, "BL gpio_config");
        gpio_set_level(cfg->bl_gpio, 0);
    }

    /* --- Create I80 bus -------------------------------------------------- */
    esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num  = cfg->dc_gpio,
        .wr_gpio_num  = cfg->wr_gpio,
        .clk_src      = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            cfg->data_gpio[0], cfg->data_gpio[1],
            cfg->data_gpio[2], cfg->data_gpio[3],
            cfg->data_gpio[4], cfg->data_gpio[5],
            cfg->data_gpio[6], cfg->data_gpio[7],
        },
        .bus_width         = 8,
        .max_transfer_bytes = cfg->width * cfg->height * 2 + 10,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_i80_bus(&bus_cfg, &dev->bus),
                      err_free, TAG, "new_i80_bus");

    /* --- Create panel IO ------------------------------------------------- */
    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num     = cfg->cs_gpio,
        .pclk_hz         = cfg->pclk_hz,
        .trans_queue_depth = 10,
        .dc_levels = {
            .dc_idle_level  = 0,
            .dc_cmd_level   = 0,
            .dc_dummy_level = 0,
            .dc_data_level  = 1,
        },
        .lcd_cmd_bits   = 8,
        .lcd_param_bits = 8,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_i80(dev->bus, &io_cfg, &dev->io),
                      err_bus, TAG, "new_panel_io_i80");

    /* --- Software reset + init sequence ---------------------------------- */
    ESP_GOTO_ON_ERROR(lcd_cmd(dev->io, ILI9341_CMD_SWRESET, NULL, 0),
                      err_io, TAG, "SWRESET");
    vTaskDelay(pdMS_TO_TICKS(150));

    ESP_GOTO_ON_ERROR(ili9341_send_init_cmds(dev->io,
                                              cfg->mirror_x, cfg->mirror_y,
                                              cfg->swap_xy),
                      err_io, TAG, "init_cmds");

    /* --- Row buffer for fill_rect ---------------------------------------- */
    dev->row_buf_pixels = cfg->width;
    dev->row_buf = malloc(dev->row_buf_pixels * 2);
    ESP_GOTO_ON_FALSE(dev->row_buf, ESP_ERR_NO_MEM, err_io, TAG, "row_buf alloc");

    /* --- Backlight on ----------------------------------------------------- */
    if (cfg->bl_gpio >= 0) {
        gpio_set_level(cfg->bl_gpio, 1);
    }

    ESP_LOGI(TAG, "ILI9341 ready (%u × %u @ %.1f MHz)",
             cfg->width, cfg->height, cfg->pclk_hz / 1e6f);
    *out_handle = dev;
    return ESP_OK;

err_io:  esp_lcd_panel_io_del(dev->io);
err_bus: esp_lcd_del_i80_bus(dev->bus);
err_free: free(dev);
    return ESP_FAIL;
}

esp_err_t ili9341_draw_bitmap(ili9341_handle_t handle,
                               uint16_t x1, uint16_t y1,
                               uint16_t x2, uint16_t y2,
                               const uint16_t *data)
{
    ESP_RETURN_ON_FALSE(handle && data, ESP_ERR_INVALID_ARG, TAG, "NULL arg");

    /* Set column address */
    uint8_t caset[4] = { x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF };
    ESP_RETURN_ON_ERROR(lcd_cmd(handle->io, ILI9341_CMD_CASET, caset, 4),
                        TAG, "CASET");

    /* Set row address */
    uint8_t raset[4] = { y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF };
    ESP_RETURN_ON_ERROR(lcd_cmd(handle->io, ILI9341_CMD_RASET, raset, 4),
                        TAG, "RASET");

    /* Write pixel data */
    size_t bytes = (size_t)(x2 - x1 + 1) * (y2 - y1 + 1) * 2;
    return esp_lcd_panel_io_tx_color(handle->io, ILI9341_CMD_RAMWR,
                                      data, bytes);
}

esp_err_t ili9341_fill_rect(ili9341_handle_t handle,
                             uint16_t x1, uint16_t y1,
                             uint16_t x2, uint16_t y2,
                             uint16_t color)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "NULL handle");

    uint16_t w = x2 - x1 + 1;
    uint16_t h = y2 - y1 + 1;

    /* Fill row buffer with the colour */
    if (w > handle->row_buf_pixels) {
        free(handle->row_buf);
        handle->row_buf = malloc(w * 2);
        ESP_RETURN_ON_FALSE(handle->row_buf, ESP_ERR_NO_MEM, TAG, "row_buf grow");
        handle->row_buf_pixels = w;
    }
    for (uint16_t i = 0; i < w; i++) {
        handle->row_buf[i] = color;
    }

    /* Set address window once and write h rows */
    uint8_t caset[4] = { x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF };
    uint8_t raset[4] = { y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF };
    ESP_RETURN_ON_ERROR(lcd_cmd(handle->io, ILI9341_CMD_CASET, caset, 4), TAG, "CASET");
    ESP_RETURN_ON_ERROR(lcd_cmd(handle->io, ILI9341_CMD_RASET, raset, 4), TAG, "RASET");

    /* Send RAMWR command, then stream rows */
    ESP_RETURN_ON_ERROR(lcd_cmd(handle->io, ILI9341_CMD_RAMWR, NULL, 0), TAG, "RAMWR");
    for (uint16_t row = 0; row < h; row++) {
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_io_tx_color(handle->io, -1, handle->row_buf, w * 2),
            TAG, "tx row");
    }
    return ESP_OK;
}

esp_err_t ili9341_set_backlight(ili9341_handle_t handle, bool on)
{
    ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "NULL handle");
    if (handle->cfg.bl_gpio >= 0) {
        gpio_set_level(handle->cfg.bl_gpio, on ? 1 : 0);
    }
    return ESP_OK;
}
