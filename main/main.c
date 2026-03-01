/**
 * main.c — Weather station entry point.
 *
 * Initialises shared hardware (I2C bus, GPIOs) and dispatches to
 * either the debug/hardware-test app or the production application
 * depending on CONFIG_APP_MODE_DEBUG / CONFIG_APP_MODE_PRODUCTION.
 *
 * All hardware pin assignments live in board.h.
 * Tune polling rates and stack sizes in app_config.h.
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "board.h"
#include "app_config.h"

#if CONFIG_APP_MODE_DEBUG
#include "debug_app.h"
#elif CONFIG_APP_MODE_PRODUCTION
#include "production_app.h"
#endif

static const char *TAG = APP_LOG_TAG;

/* =========================================================================
 * I2C bus initialisation (shared by all three sensors)
 * ========================================================================= */
static esp_err_t i2c_bus_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = BOARD_I2C_SDA,
        .scl_io_num       = BOARD_I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BOARD_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(BOARD_I2C_PORT, &conf),
                        TAG, "i2c_param_config");
    ESP_RETURN_ON_ERROR(i2c_driver_install(BOARD_I2C_PORT,
                                            I2C_MODE_MASTER, 0, 0, 0),
                        TAG, "i2c_driver_install");
    ESP_LOGI(TAG, "I2C bus %d ready (SDA=%d SCL=%d @ %lu Hz)",
             BOARD_I2C_PORT, BOARD_I2C_SDA, BOARD_I2C_SCL,
             (unsigned long)BOARD_I2C_FREQ_HZ);
    return ESP_OK;
}

/* =========================================================================
 * GPIO init for buttons, LEDs, switches, power-good
 * ========================================================================= */
static void gpio_misc_init(void)
{
    /* Buttons — input, pull-up, active LOW */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BOARD_BTN_UX)   |
                        (1ULL << BOARD_BTN_DBG0)  |
                        (1ULL << BOARD_BTN_DBG1),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    /* Switches — input, pull-up */
    gpio_config_t sw_cfg = {
        .pin_bit_mask = (1ULL << BOARD_SW0) | (1ULL << BOARD_SW1),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&sw_cfg);

    /* LEDs — output, default off */
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << BOARD_LED_STATUS) | (1ULL << BOARD_LED_ALERT),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_cfg);
    gpio_set_level(BOARD_LED_STATUS, 0);
    gpio_set_level(BOARD_LED_ALERT,  0);

    /* Power good — input, no pull (driven by PMIC) */
    gpio_config_t pg_cfg = {
        .pin_bit_mask = (1ULL << BOARD_PWRGD),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&pg_cfg);
}

/* =========================================================================
 * app_main
 * ========================================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "Weather station starting...");

    /* --- Shared hardware init -------------------------------------------- */
#if !CONFIG_HAL_USE_MOCK
    gpio_misc_init();
    gpio_set_level(BOARD_LED_STATUS, 1);  /* LED on during init */
    ESP_ERROR_CHECK(i2c_bus_init());
#else
    ESP_LOGW(TAG, "*** MOCK MODE — skipping GPIO/I2C hardware init ***");
#endif

    /* --- Dispatch by build mode ----------------------------------------- */
#if CONFIG_APP_MODE_DEBUG
    ESP_LOGW(TAG, "*** DEBUG MODE — running hardware test ***");
    debug_app_run();
    /* debug_app_run() does not return */
#elif CONFIG_APP_MODE_PRODUCTION
    ESP_LOGI(TAG, "Production mode — starting application tasks...");
    production_app_run();
    /* production_app_run() does not return */
#endif
}
