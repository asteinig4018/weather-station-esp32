/**
 * main.c — Weather station top-level application.
 *
 * Architecture:
 *   sensor_task  — reads SEN66, BMP851, ISM330DHC periodically; posts to queue
 *   display_task — consumes queue; updates display
 *   button_task  — debounces buttons; sends events
 *
 * All hardware pin assignments live in board.h.
 * Tune polling rates and stack sizes in app_config.h.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "board.h"
#include "app_config.h"

#include "ili9341.h"
#include "sen66.h"
#include "ism330dhc.h"
#include "bmp851.h"

static const char *TAG = APP_LOG_TAG;

/* =========================================================================
 * Shared sensor data (protected by mutex in a real app; kept simple here)
 * ========================================================================= */
typedef struct {
    sen66_data_t     air;       /*!< SEN66 air quality + T/RH */
    bmp851_data_t    baro;      /*!< BMP851 pressure + temperature */
    ism330dhc_vec3_t accel;     /*!< ISM330DHC accelerometer [m/s²] */
    ism330dhc_vec3_t gyro;      /*!< ISM330DHC gyroscope    [dps]  */
    bool             pwrgd;     /*!< Power good signal state */
} sensor_snapshot_t;

static QueueHandle_t s_sensor_queue;

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
             BOARD_I2C_PORT, BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ_HZ);
    return ESP_OK;
}

/* =========================================================================
 * GPIO init for buttons, LEDs, switches, power-good
 * ========================================================================= */
static void gpio_misc_init(void)
{
    /* Buttons — input, pull-up */
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
 * Display initialisation helper
 * ========================================================================= */
static ili9341_handle_t display_init(void)
{
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

    ili9341_handle_t disp = NULL;
    esp_err_t ret = ili9341_init(&cfg, &disp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(ret));
        return NULL;
    }
    return disp;
}

/* =========================================================================
 * Sensor task
 * ========================================================================= */
static void sensor_task(void *arg)
{
    sen66_handle_t     sen66    = NULL;
    bmp851_handle_t    bmp851   = NULL;
    ism330dhc_handle_t ism330   = NULL;

    /* --- SEN66 ----------------------------------------------------------- */
    sen66_config_t sen66_cfg = SEN66_CONFIG_DEFAULT(BOARD_I2C_PORT);
    if (sen66_init(&sen66_cfg, &sen66) == ESP_OK) {
        sen66_start_measurement(sen66);
        /* SEN66 needs ~1 s to produce first valid frame */
        vTaskDelay(pdMS_TO_TICKS(1200));
    } else {
        ESP_LOGE(TAG, "SEN66 init failed — air quality unavailable");
    }

    /* --- BMP851 ---------------------------------------------------------- */
    bmp851_config_t bmp851_cfg = BMP851_CONFIG_DEFAULT(BOARD_I2C_PORT);
    if (bmp851_init(&bmp851_cfg, &bmp851) != ESP_OK) {
        ESP_LOGE(TAG, "BMP851 init failed — baro unavailable");
    }

    /* --- ISM330DHC ------------------------------------------------------- */
    ism330dhc_config_t imu_cfg = ISM330DHC_CONFIG_DEFAULT(BOARD_I2C_PORT);
    if (ism330dhc_init(&imu_cfg, &ism330) != ESP_OK) {
        ESP_LOGE(TAG, "ISM330DHC init failed — IMU unavailable");
    }

    /* --- Poll loop ------------------------------------------------------- */
    for (;;) {
        sensor_snapshot_t snap = {0};

        /* SEN66 */
        if (sen66) {
            bool ready = false;
            if (sen66_data_ready(sen66, &ready) == ESP_OK && ready) {
                sen66_read_measurement(sen66, NULL, &snap.air);
            }
        }

        /* BMP851 */
        if (bmp851) {
            bmp851_read(bmp851, &snap.baro);
        }

        /* ISM330DHC */
        if (ism330) {
            ism330dhc_read_accel(ism330, &snap.accel);
            ism330dhc_read_gyro(ism330, &snap.gyro);
        }

        /* Power good */
        snap.pwrgd = gpio_get_level(BOARD_PWRGD);

        /* Push to display queue (overwrite oldest if full) */
        xQueueOverwrite(s_sensor_queue, &snap);

        /* Log summary */
        ESP_LOGD(TAG,
                 "PM2.5=%.1f µg/m³  P=%.0f Pa  T_baro=%.1f°C  T_air=%.1f°C  "
                 "RH=%.0f%%  VOC=%.0f  pwrgd=%d",
                 snap.air.pm2p5,
                 snap.baro.pressure_pa,
                 snap.baro.temp_c,
                 snap.air.temp_c,
                 snap.air.hum_pct,
                 snap.air.voc,
                 snap.pwrgd);

        vTaskDelay(pdMS_TO_TICKS(APP_SENSOR_POLL_MS));
    }
}

/* =========================================================================
 * Display task — draws a simple text-based dashboard
 * ========================================================================= */

/* RGB565 colour helpers */
#define RGB565(r, g, b)  (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
#define COLOR_BLACK   RGB565(0,   0,   0)
#define COLOR_WHITE   RGB565(255, 255, 255)
#define COLOR_CYAN    RGB565(0,   255, 255)
#define COLOR_YELLOW  RGB565(255, 255, 0)
#define COLOR_RED     RGB565(255, 0,   0)
#define COLOR_GREEN   RGB565(0,   200, 50)

static void display_task(void *arg)
{
    ili9341_handle_t disp = (ili9341_handle_t)arg;
    if (!disp) {
        ESP_LOGE(TAG, "display_task: NULL handle, exiting");
        vTaskDelete(NULL);
    }

    /* Clear screen */
    ili9341_fill_rect(disp, 0, 0, BOARD_LCD_WIDTH - 1, BOARD_LCD_HEIGHT - 1,
                      COLOR_BLACK);

    sensor_snapshot_t snap = {0};

    for (;;) {
        if (xQueuePeek(s_sensor_queue, &snap, pdMS_TO_TICKS(100)) == pdTRUE) {
            /*
             * TODO: Implement a proper UI with text rendering or LVGL.
             *
             * For now, draw coloured status bars as a placeholder:
             *   - Green  = PM2.5 OK (< 12)
             *   - Yellow = PM2.5 moderate (12–35)
             *   - Red    = PM2.5 elevated (> 35)
             */
            uint16_t pm_color;
            if (snap.air.pm2p5 < 12.0f)       pm_color = COLOR_GREEN;
            else if (snap.air.pm2p5 < 35.0f)  pm_color = COLOR_YELLOW;
            else                                pm_color = COLOR_RED;

            /* PM2.5 bar — top strip 40 px tall */
            ili9341_fill_rect(disp, 0, 0, BOARD_LCD_WIDTH - 1, 39, pm_color);

            /* Power good indicator — bottom-right 20x20 box */
            uint16_t pg_color = snap.pwrgd ? COLOR_GREEN : COLOR_RED;
            ili9341_fill_rect(disp,
                              BOARD_LCD_WIDTH - 21, BOARD_LCD_HEIGHT - 21,
                              BOARD_LCD_WIDTH - 1,  BOARD_LCD_HEIGHT - 1,
                              pg_color);

            /*
             * Add LVGL or a font renderer here to display numeric values.
             * Recommended: esp_lvgl_port + lv_label_set_text_fmt()
             */
        }

        vTaskDelay(pdMS_TO_TICKS(APP_DISPLAY_REFRESH_MS));
    }
}

/* =========================================================================
 * Button task — debounce and handle presses
 * ========================================================================= */
static void button_task(void *arg)
{
    bool last_ux   = true;  /* pulled up = released */
    bool last_dbg0 = true;
    bool last_dbg1 = true;

    for (;;) {
        bool ux   = gpio_get_level(BOARD_BTN_UX);
        bool dbg0 = gpio_get_level(BOARD_BTN_DBG0);
        bool dbg1 = gpio_get_level(BOARD_BTN_DBG1);

        if (!ux && last_ux) {
            ESP_LOGI(TAG, "UX button pressed");
            /* TODO: cycle display page, toggle backlight, etc. */
        }
        if (!dbg0 && last_dbg0) {
            ESP_LOGI(TAG, "Debug button 0 pressed");
        }
        if (!dbg1 && last_dbg1) {
            ESP_LOGI(TAG, "Debug button 1 pressed");
        }

        last_ux   = ux;
        last_dbg0 = dbg0;
        last_dbg1 = dbg1;

        vTaskDelay(pdMS_TO_TICKS(20));  /* 50 Hz poll = ~10 ms debounce */
    }
}

/* =========================================================================
 * app_main
 * ========================================================================= */
void app_main(void)
{
    ESP_LOGI(TAG, "Weather station starting...");

    /* --- GPIO (buttons, LEDs, power good) -------------------------------- */
    gpio_misc_init();

    /* --- Status LED: on during init ------------------------------------- */
    gpio_set_level(BOARD_LED_STATUS, 1);

    /* --- I2C bus --------------------------------------------------------- */
    ESP_ERROR_CHECK(i2c_bus_init());

    /* --- Display --------------------------------------------------------- */
    ili9341_handle_t disp = display_init();
    /* Display failure is non-fatal for sensor operation */

    /* --- Sensor queue (depth=1, newest snapshot only) ------------------- */
    s_sensor_queue = xQueueCreate(1, sizeof(sensor_snapshot_t));
    configASSERT(s_sensor_queue);

    /* --- Tasks ----------------------------------------------------------- */
    xTaskCreate(sensor_task, "sensor",
                APP_TASK_SENSOR_STACK, NULL,
                APP_TASK_SENSOR_PRIO, NULL);

    xTaskCreate(display_task, "display",
                APP_TASK_DISPLAY_STACK, disp,
                APP_TASK_DISPLAY_PRIO, NULL);

    xTaskCreate(button_task, "button",
                APP_TASK_BUTTON_STACK, NULL,
                APP_TASK_BUTTON_PRIO, NULL);

    /* --- Init done ------------------------------------------------------- */
    gpio_set_level(BOARD_LED_STATUS, 0);
    ESP_LOGI(TAG, "Init complete. Tasks running.");

    /* app_main can return; FreeRTOS scheduler continues. */
}
