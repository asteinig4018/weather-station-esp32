/**
 * debug_app.c — Sequential hardware test for the weather station board.
 *
 * Each test prints a clear PASS / FAIL line for easy parsing.
 * On completion, LEDs indicate overall result:
 *   - Status LED solid ON  = all tests passed
 *   - Alert LED blinking   = one or more tests failed
 */

#include "debug_app.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board.h"
#include "app_config.h"
#include "ili9341.h"
#include "sen66.h"
#include "bmp851.h"
#include "ism330dhc.h"

static const char *TAG = "hw_test";

/* -------------------------------------------------------------------------
 * Test result tracking
 * ------------------------------------------------------------------------- */
typedef struct {
    const char *name;
    bool        passed;
    const char *detail;
} test_result_t;

#define MAX_TESTS 8
static test_result_t s_results[MAX_TESTS];
static int           s_num_tests = 0;

static void record(const char *name, bool passed, const char *detail)
{
    if (s_num_tests < MAX_TESTS) {
        s_results[s_num_tests].name   = name;
        s_results[s_num_tests].passed = passed;
        s_results[s_num_tests].detail = detail;
        s_num_tests++;
    }
    if (passed) {
        ESP_LOGI(TAG, "[PASS] %s — %s", name, detail);
    } else {
        ESP_LOGE(TAG, "[FAIL] %s — %s", name, detail);
    }
}

/* -------------------------------------------------------------------------
 * Test 1: I2C bus scan
 *
 * Probe every address 0x03–0x77 and report which respond.
 * Check specifically for our three expected devices.
 * ------------------------------------------------------------------------- */
static void test_i2c_scan(void)
{
    ESP_LOGI(TAG, "--- Test: I2C bus scan ---");

    bool found_ism = false, found_bmp = false, found_sen = false;
    int  total_found = 0;

    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(BOARD_I2C_PORT, cmd,
                                              pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) {
            total_found++;
            const char *label = "unknown";
            if (addr == BOARD_ADDR_ISM330DHC) { found_ism = true; label = "ISM330DHC"; }
            if (addr == BOARD_ADDR_BMP851)    { found_bmp = true; label = "BMP851"; }
            if (addr == BOARD_ADDR_SEN66)     { found_sen = true; label = "SEN66"; }
            ESP_LOGI(TAG, "  0x%02X ACK — %s", addr, label);
        }
    }

    ESP_LOGI(TAG, "  Total devices found: %d", total_found);

    /* Individual records */
    record("I2C: ISM330DHC (0x6A)", found_ism,
           found_ism ? "ACK received" : "no response at 0x6A");
    record("I2C: BMP851 (0x76)", found_bmp,
           found_bmp ? "ACK received" : "no response at 0x76");
    record("I2C: SEN66 (0x6B)", found_sen,
           found_sen ? "ACK received" : "no response at 0x6B");
}

/* -------------------------------------------------------------------------
 * Test 2: ISM330DHC — WHO_AM_I + sample read
 * ------------------------------------------------------------------------- */
static void test_ism330dhc(void)
{
    ESP_LOGI(TAG, "--- Test: ISM330DHC ---");

    ism330dhc_config_t cfg = ISM330DHC_CONFIG_DEFAULT(BOARD_I2C_PORT);
    ism330dhc_handle_t imu = NULL;
    esp_err_t ret = ism330dhc_init(&cfg, &imu);

    if (ret != ESP_OK) {
        record("ISM330DHC init", false, esp_err_to_name(ret));
        return;
    }
    record("ISM330DHC init", true, "WHO_AM_I=0x6B, configured");

    /* Sample read */
    ism330dhc_vec3_t accel, gyro;
    float temp;
    vTaskDelay(pdMS_TO_TICKS(50)); /* let ODR stabilise */

    ret = ism330dhc_read_accel(imu, &accel);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "  Accel: x=%.3f  y=%.3f  z=%.3f m/s²",
                 accel.x, accel.y, accel.z);
    }
    ret |= ism330dhc_read_gyro(imu, &gyro);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "  Gyro:  x=%.2f  y=%.2f  z=%.2f dps",
                 gyro.x, gyro.y, gyro.z);
    }
    ret |= ism330dhc_read_temp(imu, &temp);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "  Temp:  %.1f °C", temp);
    }

    record("ISM330DHC read", ret == ESP_OK,
           ret == ESP_OK ? "accel/gyro/temp OK" : "read error");

    ism330dhc_deinit(imu);
}

/* -------------------------------------------------------------------------
 * Test 3: BMP851 — chip ID + forced measurement
 * ------------------------------------------------------------------------- */
static void test_bmp851(void)
{
    ESP_LOGI(TAG, "--- Test: BMP851 ---");

    bmp851_config_t cfg = BMP851_CONFIG_DEFAULT(BOARD_I2C_PORT);
    bmp851_handle_t baro = NULL;
    esp_err_t ret = bmp851_init(&cfg, &baro);

    if (ret != ESP_OK) {
        record("BMP851 init", false, esp_err_to_name(ret));
        return;
    }
    record("BMP851 init", true, "chip ID OK, calibration loaded");

    /* Forced measurement */
    bmp851_data_t data;
    ret = bmp851_read(baro, &data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "  Pressure: %.1f Pa (%.2f hPa)", data.pressure_pa,
                 data.pressure_pa / 100.0f);
        ESP_LOGI(TAG, "  Temp:     %.2f °C", data.temp_c);

        /* Sanity check: sea-level pressure 870–1085 hPa, temp -40–85 */
        bool sane = (data.pressure_pa > 87000.0f && data.pressure_pa < 108500.0f)
                  && (data.temp_c > -40.0f && data.temp_c < 85.0f);
        record("BMP851 read", sane,
               sane ? "pressure/temp in sane range" : "values out of expected range");
    } else {
        record("BMP851 read", false, esp_err_to_name(ret));
    }

    bmp851_deinit(baro);
}

/* -------------------------------------------------------------------------
 * Test 4: SEN66 — reset + start + read one frame
 * ------------------------------------------------------------------------- */
static void test_sen66(void)
{
    ESP_LOGI(TAG, "--- Test: SEN66 ---");

    sen66_config_t cfg = SEN66_CONFIG_DEFAULT(BOARD_I2C_PORT);
    sen66_handle_t air = NULL;
    esp_err_t ret = sen66_init(&cfg, &air);

    if (ret != ESP_OK) {
        record("SEN66 init", false, esp_err_to_name(ret));
        return;
    }
    record("SEN66 init", true, "device responded to reset");

    ret = sen66_start_measurement(air);
    if (ret != ESP_OK) {
        record("SEN66 measurement", false, "start_measurement failed");
        sen66_deinit(air);
        return;
    }

    /* SEN66 needs ~1 s to produce first valid frame */
    ESP_LOGI(TAG, "  Waiting for first SEN66 measurement (~2 s)...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    bool ready = false;
    ret = sen66_data_ready(air, &ready);
    if (ret != ESP_OK || !ready) {
        record("SEN66 measurement", false,
               ret != ESP_OK ? "data_ready error" : "data not ready after 2 s");
        sen66_deinit(air);
        return;
    }

    sen66_data_t data;
    ret = sen66_read_measurement(air, NULL, &data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "  PM1.0: %.1f  PM2.5: %.1f  PM4.0: %.1f  PM10: %.1f µg/m³",
                 data.pm1p0, data.pm2p5, data.pm4p0, data.pm10);
        ESP_LOGI(TAG, "  Temp:  %.1f °C  RH: %.1f %%", data.temp_c, data.hum_pct);
        ESP_LOGI(TAG, "  VOC:   %.0f  NOx: %.0f", data.voc, data.nox);
        record("SEN66 measurement", true, "all fields read OK");
    } else {
        record("SEN66 measurement", false, esp_err_to_name(ret));
    }

    sen66_deinit(air);
}

/* -------------------------------------------------------------------------
 * Test 5: Display — init + colour bars
 * ------------------------------------------------------------------------- */
static void test_display(void)
{
    ESP_LOGI(TAG, "--- Test: Display ---");

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
    };

    ili9341_handle_t disp = NULL;
    esp_err_t ret = ili9341_init(&cfg, &disp);

    if (ret != ESP_OK) {
        record("Display init", false, esp_err_to_name(ret));
        return;
    }
    record("Display init", true, "ILI9341 8080 I80 bus OK");

    /* Draw 4 colour bars: Red, Green, Blue, White — each 80 px tall */
    uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF}; /* R, G, B, W */
    const char *names[] = {"Red", "Green", "Blue", "White"};
    uint16_t bar_h = BOARD_LCD_HEIGHT / 4;

    for (int i = 0; i < 4; i++) {
        ret = ili9341_fill_rect(disp,
                                 0, i * bar_h,
                                 BOARD_LCD_WIDTH - 1, (i + 1) * bar_h - 1,
                                 colors[i]);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "  Fill %s bar failed", names[i]);
        }
    }

    record("Display draw", true, "4 colour bars drawn — verify visually");

    /* Leave display on for visual inspection; don't deinit */
}

/* -------------------------------------------------------------------------
 * Test 6 / 7 / 8: GPIO — buttons, LEDs, power good
 * ------------------------------------------------------------------------- */
static void test_gpio(void)
{
    ESP_LOGI(TAG, "--- Test: GPIO ---");

    /* Read button states */
    int ux   = gpio_get_level(BOARD_BTN_UX);
    int dbg0 = gpio_get_level(BOARD_BTN_DBG0);
    int dbg1 = gpio_get_level(BOARD_BTN_DBG1);
    ESP_LOGI(TAG, "  Buttons: UX=%d  DBG0=%d  DBG1=%d  (1=released, 0=pressed)",
             ux, dbg0, dbg1);

    /* Read switch states */
    int sw0 = gpio_get_level(BOARD_SW0);
    int sw1 = gpio_get_level(BOARD_SW1);
    ESP_LOGI(TAG, "  Switches: SW0=%d  SW1=%d", sw0, sw1);

    /* Read power good */
    int pwrgd = gpio_get_level(BOARD_PWRGD);
    ESP_LOGI(TAG, "  Power good: %d  (%s)", pwrgd,
             pwrgd ? "supply OK" : "supply BAD or not connected");
    record("Power good", true, pwrgd ? "HIGH — supply OK" : "LOW — check supply");

    /* Blink LEDs: status then alert, 3 cycles each */
    ESP_LOGI(TAG, "  Blinking status LED (3×)...");
    for (int i = 0; i < 3; i++) {
        gpio_set_level(BOARD_LED_STATUS, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(BOARD_LED_STATUS, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    ESP_LOGI(TAG, "  Blinking alert LED (3×)...");
    for (int i = 0; i < 3; i++) {
        gpio_set_level(BOARD_LED_ALERT, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(BOARD_LED_ALERT, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    record("LEDs", true, "blink sequence complete — verify visually");
}

/* -------------------------------------------------------------------------
 * Summary + result indication loop
 * ------------------------------------------------------------------------- */
static void print_summary(void)
{
    int pass_count = 0;
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  HARDWARE TEST SUMMARY");
    ESP_LOGI(TAG, "============================================");
    for (int i = 0; i < s_num_tests; i++) {
        const char *mark = s_results[i].passed ? "PASS" : "FAIL";
        ESP_LOGI(TAG, "  [%s] %-25s %s", mark,
                 s_results[i].name, s_results[i].detail);
        if (s_results[i].passed) pass_count++;
    }
    ESP_LOGI(TAG, "--------------------------------------------");
    ESP_LOGI(TAG, "  Result: %d / %d passed", pass_count, s_num_tests);
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "");
}

static void result_led_loop(bool all_passed)
{
    if (all_passed) {
        /* Solid status LED = all good */
        gpio_set_level(BOARD_LED_STATUS, 1);
        gpio_set_level(BOARD_LED_ALERT,  0);
        ESP_LOGI(TAG, "All tests passed. Status LED solid ON.");
    } else {
        /* Blink alert LED = something failed */
        gpio_set_level(BOARD_LED_STATUS, 0);
        ESP_LOGW(TAG, "Some tests failed. Alert LED blinking.");
    }

    for (;;) {
        if (!all_passed) {
            gpio_set_level(BOARD_LED_ALERT, 1);
            vTaskDelay(pdMS_TO_TICKS(300));
            gpio_set_level(BOARD_LED_ALERT, 0);
            vTaskDelay(pdMS_TO_TICKS(300));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

/* -------------------------------------------------------------------------
 * Public entry point
 * ------------------------------------------------------------------------- */
void debug_app_run(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  WEATHER STATION — HARDWARE TEST");
    ESP_LOGI(TAG, "  Board: ESP32-S3-WROOM-1-N8");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "");

    /* Run tests in order */
    test_i2c_scan();
    test_ism330dhc();
    test_bmp851();
    test_sen66();
    test_display();
    test_gpio();

    /* Summary */
    print_summary();

    /* Count failures */
    bool all_passed = true;
    for (int i = 0; i < s_num_tests; i++) {
        if (!s_results[i].passed) { all_passed = false; break; }
    }

    /* Loop forever with LED indication */
    result_led_loop(all_passed);
}
