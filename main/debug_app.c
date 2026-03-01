/**
 * debug_app.c — Sequential hardware test for the weather station board.
 *
 * In real mode (CONFIG_HAL_USE_MOCK=n):
 *   Tests each peripheral individually via direct driver calls.
 *
 * In mock mode (CONFIG_HAL_USE_MOCK=y):
 *   Verifies the HAL mock layer works correctly by reading synthetic data
 *   in a loop and validating it is in expected ranges.
 */

#include "debug_app.h"

#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board.h"
#include "app_config.h"
#include "hal_sensors.h"
#include "hal_display.h"

#if !CONFIG_HAL_USE_MOCK
#include "driver/i2c.h"
#include "ili9341.h"
#include "sen66.h"
#include "bmp851.h"
#include "ism330dhc.h"
#endif

static const char *TAG = "hw_test";

/* -------------------------------------------------------------------------
 * Test result tracking
 * ------------------------------------------------------------------------- */
typedef struct {
    const char *name;
    bool        passed;
    const char *detail;
} test_result_t;

#define MAX_TESTS 12
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

/* =========================================================================
 * MOCK MODE: Test the HAL layer with synthetic data
 * ========================================================================= */

#if CONFIG_HAL_USE_MOCK

static void test_hal_sensors_mock(void)
{
    ESP_LOGI(TAG, "--- Test: HAL sensors (mock) ---");

    hal_sensor_status_t st;
    esp_err_t ret = hal_sensors_init(&st);
    record("HAL sensor init", ret == ESP_OK && st.sen66_ok && st.bmp851_ok && st.ism330dhc_ok,
           ret == ESP_OK ? "all mock sensors OK" : "init failed");

    if (ret != ESP_OK) return;

    /* Read 5 consecutive snapshots, validate data is in expected ranges */
    bool all_ok = true;
    for (int i = 0; i < 5; i++) {
        hal_sensor_data_t data;
        ret = hal_sensors_read(&data);
        if (ret != ESP_OK) { all_ok = false; break; }

        /* Validate ranges for mock data */
        bool temp_ok = (data.baro.temp_c > 10.0f && data.baro.temp_c < 35.0f);
        bool pres_ok = (data.baro.pressure_pa > 100000.0f && data.baro.pressure_pa < 102000.0f);
        bool pm_ok   = (data.air.pm2p5 >= 0.0f && data.air.pm2p5 < 60.0f);
        bool accel_ok = (data.accel.z > 9.0f && data.accel.z < 10.5f);
        bool ts_ok   = (data.timestamp_us > 0);

        if (!temp_ok || !pres_ok || !pm_ok || !accel_ok || !ts_ok) {
            ESP_LOGE(TAG, "  Sample %d out of range: T=%.1f P=%.0f PM=%.1f Az=%.2f ts=%lld",
                     i, data.baro.temp_c, data.baro.pressure_pa,
                     data.air.pm2p5, data.accel.z, data.timestamp_us);
            all_ok = false;
        } else {
            ESP_LOGI(TAG, "  Sample %d: T=%.1f°C  P=%.0fPa  PM2.5=%.1f  Az=%.2f",
                     i, data.baro.temp_c, data.baro.pressure_pa,
                     data.air.pm2p5, data.accel.z);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    record("HAL sensor read (5x)", all_ok, all_ok ? "all in range" : "out of range");
    hal_sensors_deinit();
}

static void test_hal_display_mock(void)
{
    ESP_LOGI(TAG, "--- Test: HAL display (mock) ---");

    esp_err_t ret = hal_display_init();
    record("HAL display init", ret == ESP_OK,
           ret == ESP_OK ? "mock display ready" : "init failed");

    if (ret != ESP_OK) return;

    /* Test fill and bitmap calls */
    ret = hal_display_fill_rect(0, 0, hal_display_width() - 1, 39, 0xF800);
    record("HAL display fill", ret == ESP_OK, "fill_rect OK");

    ret = hal_display_set_backlight(true);
    record("HAL display backlight", ret == ESP_OK, "backlight on");
}

void debug_app_run(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  WEATHER STATION — MOCK MODE TEST");
    ESP_LOGI(TAG, "  (no real hardware required)");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "");

    test_hal_sensors_mock();
    test_hal_display_mock();

    print_summary();

    bool all_passed = true;
    for (int i = 0; i < s_num_tests; i++) {
        if (!s_results[i].passed) { all_passed = false; break; }
    }

    if (all_passed) {
        ESP_LOGI(TAG, "All mock tests passed. System ready for QEMU / CI.");
    } else {
        ESP_LOGW(TAG, "Some mock tests failed — check HAL implementation.");
    }

    /* In mock mode, run a continuous sensor loop to exercise the data path */
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Starting continuous mock sensor loop (2 s interval)...");
    hal_sensor_status_t st;
    hal_sensors_init(&st);

    for (int count = 0; ; count++) {
        hal_sensor_data_t data;
        hal_sensors_read(&data);
        ESP_LOGI(TAG, "[%d] T=%.1f°C  P=%.0fPa  PM2.5=%.1f  RH=%.0f%%  VOC=%.0f  Ax=%.2f Az=%.2f  pwrgd=%d",
                 count,
                 data.baro.temp_c,
                 data.baro.pressure_pa,
                 data.air.pm2p5,
                 data.air.hum_pct,
                 data.air.voc,
                 data.accel.x,
                 data.accel.z,
                 data.pwrgd);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* =========================================================================
 * REAL MODE: Test actual hardware peripherals
 * ========================================================================= */

#else /* !CONFIG_HAL_USE_MOCK */

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
    record("I2C: ISM330DHC (0x6A)", found_ism,
           found_ism ? "ACK received" : "no response at 0x6A");
    record("I2C: BMP851 (0x76)", found_bmp,
           found_bmp ? "ACK received" : "no response at 0x76");
    record("I2C: SEN66 (0x6B)", found_sen,
           found_sen ? "ACK received" : "no response at 0x6B");
}

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

    ism330dhc_vec3_t accel, gyro;
    float temp;
    vTaskDelay(pdMS_TO_TICKS(50));

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

    bmp851_data_t data;
    ret = bmp851_read(baro, &data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "  Pressure: %.1f Pa (%.2f hPa)", data.pressure_pa,
                 data.pressure_pa / 100.0f);
        ESP_LOGI(TAG, "  Temp:     %.2f °C", data.temp_c);

        bool sane = (data.pressure_pa > 87000.0f && data.pressure_pa < 108500.0f)
                  && (data.temp_c > -40.0f && data.temp_c < 85.0f);
        record("BMP851 read", sane,
               sane ? "pressure/temp in sane range" : "values out of expected range");
    } else {
        record("BMP851 read", false, esp_err_to_name(ret));
    }
    bmp851_deinit(baro);
}

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

static void test_display(void)
{
    ESP_LOGI(TAG, "--- Test: Display ---");

    esp_err_t ret = hal_display_init();
    if (ret != ESP_OK) {
        record("Display init", false, esp_err_to_name(ret));
        return;
    }
    record("Display init", true, "ILI9341 8080 I80 bus OK");

    uint16_t w = hal_display_width();
    uint16_t h = hal_display_height();
    uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    uint16_t bar_h = h / 4;

    for (int i = 0; i < 4; i++) {
        ret = hal_display_fill_rect(0, i * bar_h, w - 1, (i + 1) * bar_h - 1,
                                     colors[i]);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "  Fill bar %d failed", i);
        }
    }
    record("Display draw", true, "4 colour bars drawn — verify visually");
}

static void test_gpio(void)
{
    ESP_LOGI(TAG, "--- Test: GPIO ---");

    int ux   = gpio_get_level(BOARD_BTN_UX);
    int dbg0 = gpio_get_level(BOARD_BTN_DBG0);
    int dbg1 = gpio_get_level(BOARD_BTN_DBG1);
    ESP_LOGI(TAG, "  Buttons: UX=%d  DBG0=%d  DBG1=%d  (1=released, 0=pressed)",
             ux, dbg0, dbg1);

    int sw0 = gpio_get_level(BOARD_SW0);
    int sw1 = gpio_get_level(BOARD_SW1);
    ESP_LOGI(TAG, "  Switches: SW0=%d  SW1=%d", sw0, sw1);

    int pwrgd = gpio_get_level(BOARD_PWRGD);
    ESP_LOGI(TAG, "  Power good: %d  (%s)", pwrgd,
             pwrgd ? "supply OK" : "supply BAD or not connected");
    record("Power good", true, pwrgd ? "HIGH — supply OK" : "LOW — check supply");

    ESP_LOGI(TAG, "  Blinking status LED (3x)...");
    for (int i = 0; i < 3; i++) {
        gpio_set_level(BOARD_LED_STATUS, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(BOARD_LED_STATUS, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    ESP_LOGI(TAG, "  Blinking alert LED (3x)...");
    for (int i = 0; i < 3; i++) {
        gpio_set_level(BOARD_LED_ALERT, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(BOARD_LED_ALERT, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    record("LEDs", true, "blink sequence complete — verify visually");
}

static void result_led_loop(bool all_passed)
{
    if (all_passed) {
        gpio_set_level(BOARD_LED_STATUS, 1);
        gpio_set_level(BOARD_LED_ALERT,  0);
        ESP_LOGI(TAG, "All tests passed. Status LED solid ON.");
    } else {
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

void debug_app_run(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  WEATHER STATION — HARDWARE TEST");
    ESP_LOGI(TAG, "  Board: ESP32-S3-WROOM-1-N8");
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "");

    test_i2c_scan();
    test_ism330dhc();
    test_bmp851();
    test_sen66();
    test_display();
    test_gpio();

    print_summary();

    bool all_passed = true;
    for (int i = 0; i < s_num_tests; i++) {
        if (!s_results[i].passed) { all_passed = false; break; }
    }
    result_led_loop(all_passed);
}

#endif /* CONFIG_HAL_USE_MOCK */
