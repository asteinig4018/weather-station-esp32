#pragma once
/**
 * ili9341.h — Thin driver for the ILI9341 TFT controller over an
 *             Intel 8080 8-bit parallel bus via ESP-IDF esp_lcd.
 *
 * Display: NHD-2.4-240320CF-CTXI#-F (240 × 320, portrait default)
 *
 * Pixel format: RGB565, little-endian (host byte order expected by esp_lcd).
 *
 * Usage:
 *   ili9341_config_t cfg = ILI9341_CONFIG_DEFAULT();
 *   // override cfg.xxx with board-specific values
 *   ili9341_handle_t disp;
 *   ili9341_init(&cfg, &disp);
 *
 *   // draw a full-screen buffer:
 *   ili9341_draw_bitmap(disp, 0, 0, 240, 320, pixel_buf);
 */

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Configuration
 * ------------------------------------------------------------------------- */

typedef struct {
    /* I80 bus data lines D[7:0] */
    int data_gpio[8];

    /* Control lines */
    int wr_gpio;    /*!< Write strobe */
    int rd_gpio;    /*!< Read strobe (tie HIGH if reads unused) */
    int dc_gpio;    /*!< Data/Command (RS) */
    int cs_gpio;    /*!< Chip select, active LOW */
    int rst_gpio;   /*!< Hardware reset, active LOW */
    int bl_gpio;    /*!< Backlight; -1 to skip */

    /* Timing */
    uint32_t pclk_hz;   /*!< Parallel clock frequency, e.g. 10 MHz */

    /* Panel geometry */
    uint16_t width;     /*!< Horizontal pixels */
    uint16_t height;    /*!< Vertical pixels */

    /* Orientation */
    bool swap_xy;       /*!< Swap X/Y axes (landscape) */
    bool mirror_x;      /*!< Mirror horizontal */
    bool mirror_y;      /*!< Mirror vertical */
} ili9341_config_t;

/** Default config — matches board.h values; override before calling init. */
#define ILI9341_CONFIG_DEFAULT() {              \
    .data_gpio = {-1,-1,-1,-1,-1,-1,-1,-1},    \
    .wr_gpio   = -1,                            \
    .rd_gpio   = -1,                            \
    .dc_gpio   = -1,                            \
    .cs_gpio   = -1,                            \
    .rst_gpio  = -1,                            \
    .bl_gpio   = -1,                            \
    .pclk_hz   = 10 * 1000 * 1000,             \
    .width     = 240,                           \
    .height    = 320,                           \
    .swap_xy   = false,                         \
    .mirror_x  = false,                         \
    .mirror_y  = false,                         \
}

/* -------------------------------------------------------------------------
 * Handle
 * ------------------------------------------------------------------------- */

typedef struct ili9341_dev *ili9341_handle_t;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/**
 * @brief  Initialise the I80 bus, ILI9341 panel, and backlight.
 *
 * @param[in]  cfg     Board-specific configuration.
 * @param[out] handle  Opaque handle used by all other calls.
 * @return     ESP_OK on success.
 */
esp_err_t ili9341_init(const ili9341_config_t *cfg, ili9341_handle_t *handle);

/**
 * @brief  Draw a rectangular region from a pixel buffer (RGB565).
 *
 * The buffer must contain (x2-x1+1)*(y2-y1+1) pixels in RGB565.
 *
 * @param handle  Display handle.
 * @param x1,y1   Top-left corner (inclusive).
 * @param x2,y2   Bottom-right corner (inclusive).
 * @param data    RGB565 pixel buffer.
 */
esp_err_t ili9341_draw_bitmap(ili9341_handle_t handle,
                               uint16_t x1, uint16_t y1,
                               uint16_t x2, uint16_t y2,
                               const uint16_t *data);

/**
 * @brief  Fill a rectangular region with a solid colour (RGB565).
 */
esp_err_t ili9341_fill_rect(ili9341_handle_t handle,
                             uint16_t x1, uint16_t y1,
                             uint16_t x2, uint16_t y2,
                             uint16_t color);

/**
 * @brief  Turn the backlight on or off (if bl_gpio is configured).
 */
esp_err_t ili9341_set_backlight(ili9341_handle_t handle, bool on);

#ifdef __cplusplus
}
#endif
