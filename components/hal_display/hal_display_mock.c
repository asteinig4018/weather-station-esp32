/**
 * hal_display_mock.c — Mock backend for hal_display.
 *
 * Compiled only when CONFIG_HAL_USE_MOCK is set.
 * Logs drawing operations to UART instead of driving real hardware.
 */

#include "hal_display.h"
#include "board.h"
#include "esp_log.h"

static const char *TAG = "hal_display_mock";

static bool s_inited = false;

esp_err_t hal_display_init(void)
{
    ESP_LOGW(TAG, "*** MOCK DISPLAY (%u x %u) ***", BOARD_LCD_WIDTH, BOARD_LCD_HEIGHT);
    s_inited = true;
    return ESP_OK;
}

esp_err_t hal_display_fill_rect(uint16_t x1, uint16_t y1,
                                 uint16_t x2, uint16_t y2,
                                 uint16_t color)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    /* Decode RGB565 for readability */
    uint8_t r = (color >> 11) << 3;
    uint8_t g = ((color >> 5) & 0x3F) << 2;
    uint8_t b = (color & 0x1F) << 3;
    ESP_LOGD(TAG, "fill_rect (%u,%u)-(%u,%u) color=#%02X%02X%02X",
             x1, y1, x2, y2, r, g, b);
    return ESP_OK;
}

esp_err_t hal_display_draw_bitmap(uint16_t x1, uint16_t y1,
                                   uint16_t x2, uint16_t y2,
                                   const uint16_t *data)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    uint32_t pixels = (uint32_t)(x2 - x1 + 1) * (y2 - y1 + 1);
    ESP_LOGD(TAG, "draw_bitmap (%u,%u)-(%u,%u) %lu px",
             x1, y1, x2, y2, (unsigned long)pixels);
    return ESP_OK;
}

esp_err_t hal_display_set_backlight(bool on)
{
    ESP_LOGI(TAG, "backlight %s", on ? "ON" : "OFF");
    return ESP_OK;
}

uint16_t hal_display_width(void)  { return BOARD_LCD_WIDTH; }
uint16_t hal_display_height(void) { return BOARD_LCD_HEIGHT; }
