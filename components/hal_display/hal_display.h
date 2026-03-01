#pragma once
/**
 * hal_display.h — Hardware Abstraction Layer for the display.
 *
 * Abstracts the ILI9341 8080 parallel interface so the application code
 * uses a uniform API regardless of whether real hardware or a UART mock
 * is behind it.
 *
 * Backend selected at compile time via CONFIG_HAL_USE_MOCK in Kconfig.
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialise the display (real or mock).
 *
 * Real mode: sets up I80 bus, sends ILI9341 init sequence, turns on backlight.
 * Mock mode: logs "display init" to UART.
 */
esp_err_t hal_display_init(void);

/**
 * @brief  Fill a rectangular area with a solid RGB565 colour.
 */
esp_err_t hal_display_fill_rect(uint16_t x1, uint16_t y1,
                                 uint16_t x2, uint16_t y2,
                                 uint16_t color);

/**
 * @brief  Draw a rectangular bitmap (RGB565 pixel buffer).
 */
esp_err_t hal_display_draw_bitmap(uint16_t x1, uint16_t y1,
                                   uint16_t x2, uint16_t y2,
                                   const uint16_t *data);

/**
 * @brief  Turn backlight on or off.
 */
esp_err_t hal_display_set_backlight(bool on);

/**
 * @brief  Get display width in pixels.
 */
uint16_t hal_display_width(void);

/**
 * @brief  Get display height in pixels.
 */
uint16_t hal_display_height(void);

#ifdef __cplusplus
}
#endif
