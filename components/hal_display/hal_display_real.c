/**
 * hal_display_real.c — Real hardware backend for hal_display.
 *
 * Compiled only when CONFIG_HAL_USE_MOCK is NOT set.
 * Wraps the drv_ili9341 driver with board.h pin assignments.
 */

#include "hal_display.h"
#include "ili9341.h"
#include "board.h"
#include "esp_log.h"

static const char *TAG = "hal_display_real";

static ili9341_handle_t s_disp = NULL;

esp_err_t hal_display_init(void)
{
    if (s_disp) return ESP_OK; /* already init */

    ili9341_config_t cfg = {
        .data_gpio = {
            BOARD_LCD_D0, BOARD_LCD_D1, BOARD_LCD_D2, BOARD_LCD_D3,
            BOARD_LCD_D4, BOARD_LCD_D5, BOARD_LCD_D6, BOARD_LCD_D7,
        },
        .wr_gpio   = BOARD_LCD_WR,
        .rd_gpio   = BOARD_LCD_RD,
        .dc_gpio   = BOARD_LCD_DC,
        .cs_gpio   = BOARD_LCD_CS,
        .rst_gpio  = BOARD_LCD_RST,
        .bl_gpio   = BOARD_LCD_BL,
        .pclk_hz   = BOARD_LCD_PCLK_HZ,
        .width     = BOARD_LCD_WIDTH,
        .height    = BOARD_LCD_HEIGHT,
        .swap_xy   = false,
        .mirror_x  = false,
        .mirror_y  = false,
    };

    esp_err_t ret = ili9341_init(&cfg, &s_disp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ILI9341 init failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t hal_display_fill_rect(uint16_t x1, uint16_t y1,
                                 uint16_t x2, uint16_t y2,
                                 uint16_t color)
{
    if (!s_disp) return ESP_ERR_INVALID_STATE;
    return ili9341_fill_rect(s_disp, x1, y1, x2, y2, color);
}

esp_err_t hal_display_draw_bitmap(uint16_t x1, uint16_t y1,
                                   uint16_t x2, uint16_t y2,
                                   const uint16_t *data)
{
    if (!s_disp) return ESP_ERR_INVALID_STATE;
    return ili9341_draw_bitmap(s_disp, x1, y1, x2, y2, data);
}

esp_err_t hal_display_set_backlight(bool on)
{
    if (!s_disp) return ESP_ERR_INVALID_STATE;
    return ili9341_set_backlight(s_disp, on);
}

uint16_t hal_display_width(void)  { return BOARD_LCD_WIDTH; }
uint16_t hal_display_height(void) { return BOARD_LCD_HEIGHT; }
