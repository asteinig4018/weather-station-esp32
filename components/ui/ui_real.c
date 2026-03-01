/**
 * ui_real.c — LVGL-based UI for the weather station.
 *
 * Two screens:
 *   - Dashboard: shows live sensor readings (temperature, humidity,
 *     pressure, PM2.5, VOC, NOx)
 *   - History:   shows data_store count and last few stored readings
 *
 * LVGL is configured with a partial framebuffer (10 lines) to minimise
 * RAM usage. The flush callback draws through hal_display_draw_bitmap().
 */

#include "ui.h"
#include "hal_display.h"
#include "data_store.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "ui";

/* =========================================================================
 * LVGL display driver
 * ========================================================================= */

#define DISP_HOR_RES  240
#define DISP_VER_RES  320
#define DISP_BUF_LINES 20

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_buf1[DISP_HOR_RES * DISP_BUF_LINES];
static lv_disp_drv_t s_disp_drv;

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_p)
{
    hal_display_draw_bitmap(area->x1, area->y1,
                             area->x2, area->y2,
                             (const uint16_t *)color_p);
    lv_disp_flush_ready(drv);
}

/* =========================================================================
 * LVGL tick
 * ========================================================================= */

static uint32_t s_last_tick_ms = 0;

static uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* =========================================================================
 * Screens and widgets
 * ========================================================================= */

static lv_obj_t *s_scr_dashboard;
static lv_obj_t *s_scr_history;
static int s_current_page = 0;

/* Dashboard labels */
static lv_obj_t *s_lbl_temp;
static lv_obj_t *s_lbl_hum;
static lv_obj_t *s_lbl_pres;
static lv_obj_t *s_lbl_pm25;
static lv_obj_t *s_lbl_voc;
static lv_obj_t *s_lbl_nox;
static lv_obj_t *s_lbl_pwrgd;

/* History labels */
static lv_obj_t *s_lbl_hist_count;
static lv_obj_t *s_lbl_hist_data;

/* Thread safety */
static SemaphoreHandle_t s_lvgl_mutex;
static hal_sensor_data_t s_latest_data;
static bool s_data_pending = false;

static lv_obj_t *create_row_label(lv_obj_t *parent, lv_coord_t y,
                                    const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 10, y);
    return lbl;
}

static void build_dashboard(void)
{
    s_scr_dashboard = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_dashboard, lv_color_hex(0x1A1A2E), 0);

    /* Title */
    lv_obj_t *title = lv_label_create(s_scr_dashboard);
    lv_label_set_text(title, "Weather Station");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00D2FF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    /* Sensor readings */
    s_lbl_temp  = create_row_label(s_scr_dashboard, 45,  "Temp:  --.- C");
    s_lbl_hum   = create_row_label(s_scr_dashboard, 70,  "RH:    --.-%");
    s_lbl_pres  = create_row_label(s_scr_dashboard, 95,  "Press: ---- hPa");
    s_lbl_pm25  = create_row_label(s_scr_dashboard, 130, "PM2.5: --.- ug/m3");
    s_lbl_voc   = create_row_label(s_scr_dashboard, 155, "VOC:   ---");
    s_lbl_nox   = create_row_label(s_scr_dashboard, 180, "NOx:   ---");

    /* Power good indicator */
    s_lbl_pwrgd = create_row_label(s_scr_dashboard, 220, "Power: --");
    lv_obj_set_style_text_font(s_lbl_pwrgd, &lv_font_montserrat_14, 0);

    /* Footer */
    lv_obj_t *footer = lv_label_create(s_scr_dashboard);
    lv_label_set_text(footer, "[UX] History");
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(footer, lv_color_hex(0x888888), 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -8);
}

static void build_history(void)
{
    s_scr_history = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_history, lv_color_hex(0x0F3460), 0);

    lv_obj_t *title = lv_label_create(s_scr_history);
    lv_label_set_text(title, "History");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE94560), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    s_lbl_hist_count = create_row_label(s_scr_history, 45, "Stored: 0 entries");

    s_lbl_hist_data = lv_label_create(s_scr_history);
    lv_label_set_text(s_lbl_hist_data, "No data yet");
    lv_obj_set_style_text_font(s_lbl_hist_data, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_hist_data, lv_color_white(), 0);
    lv_obj_align(s_lbl_hist_data, LV_ALIGN_TOP_LEFT, 10, 75);
    lv_obj_set_width(s_lbl_hist_data, 220);
    lv_label_set_long_mode(s_lbl_hist_data, LV_LABEL_LONG_WRAP);

    lv_obj_t *footer = lv_label_create(s_scr_history);
    lv_label_set_text(footer, "[UX] Dashboard");
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(footer, lv_color_hex(0x888888), 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -8);
}

/* =========================================================================
 * Update functions (called with mutex held)
 * ========================================================================= */

static void update_dashboard(const hal_sensor_data_t *d)
{
    char buf[48];

    snprintf(buf, sizeof(buf), "Temp:  %.1f C", d->baro.temp_c);
    lv_label_set_text(s_lbl_temp, buf);

    snprintf(buf, sizeof(buf), "RH:    %.0f%%", d->air.hum_pct);
    lv_label_set_text(s_lbl_hum, buf);

    snprintf(buf, sizeof(buf), "Press: %.1f hPa", d->baro.pressure_pa / 100.0f);
    lv_label_set_text(s_lbl_pres, buf);

    snprintf(buf, sizeof(buf), "PM2.5: %.1f ug/m3", d->air.pm2p5);
    lv_label_set_text(s_lbl_pm25, buf);

    snprintf(buf, sizeof(buf), "VOC:   %.0f", d->air.voc);
    lv_label_set_text(s_lbl_voc, buf);

    snprintf(buf, sizeof(buf), "NOx:   %.0f", d->air.nox);
    lv_label_set_text(s_lbl_nox, buf);

    lv_label_set_text(s_lbl_pwrgd, d->pwrgd ? "Power: OK" : "Power: BAD");
    lv_obj_set_style_text_color(s_lbl_pwrgd,
        d->pwrgd ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000), 0);
}

static void update_history_screen(void)
{
    char buf[48];
    size_t count = data_store_count();

    snprintf(buf, sizeof(buf), "Stored: %u entries", (unsigned)count);
    lv_label_set_text(s_lbl_hist_count, buf);

    if (count == 0) {
        lv_label_set_text(s_lbl_hist_data, "No data yet");
        return;
    }

    /* Show last 5 entries */
    static char hist_buf[512];
    int pos = 0;
    size_t start = (count > 5) ? count - 5 : 0;

    for (size_t i = start; i < count; i++) {
        hal_sensor_data_t d;
        if (data_store_read(i, &d) == ESP_OK) {
            int written = snprintf(hist_buf + pos, sizeof(hist_buf) - pos,
                "#%u: T=%.1fC P=%.0f PM=%.1f\n",
                (unsigned)i, d.baro.temp_c, d.baro.pressure_pa, d.air.pm2p5);
            if (written > 0) pos += written;
        }
    }

    lv_label_set_text(s_lbl_hist_data, hist_buf);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

esp_err_t ui_init(void)
{
    s_lvgl_mutex = xSemaphoreCreateMutex();
    if (!s_lvgl_mutex) return ESP_ERR_NO_MEM;

    lv_init();

    /* Display buffer */
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, NULL,
                           DISP_HOR_RES * DISP_BUF_LINES);

    /* Display driver */
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = DISP_HOR_RES;
    s_disp_drv.ver_res  = DISP_VER_RES;
    s_disp_drv.flush_cb = disp_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    /* Build screens */
    build_dashboard();
    build_history();

    /* Show dashboard first */
    lv_scr_load(s_scr_dashboard);
    s_current_page = 0;

    s_last_tick_ms = millis();

    ESP_LOGI(TAG, "LVGL UI initialised (%dx%d, buf=%d lines)",
             DISP_HOR_RES, DISP_VER_RES, DISP_BUF_LINES);
    return ESP_OK;
}

void ui_update_sensor_data(const hal_sensor_data_t *data)
{
    if (!data) return;

    /* Copy data for deferred update in ui_tick() */
    if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_latest_data = *data;
        s_data_pending = true;
        xSemaphoreGive(s_lvgl_mutex);
    }
}

void ui_navigate_next(void)
{
    if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_current_page = (s_current_page + 1) % 2;

        if (s_current_page == 0) {
            lv_scr_load_anim(s_scr_dashboard, LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                              200, 0, false);
        } else {
            update_history_screen();
            lv_scr_load_anim(s_scr_history, LV_SCR_LOAD_ANIM_MOVE_LEFT,
                              200, 0, false);
        }

        xSemaphoreGive(s_lvgl_mutex);
    }
}

void ui_tick(void)
{
    if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        /* Update LVGL tick */
        uint32_t now = millis();
        uint32_t elapsed = now - s_last_tick_ms;
        s_last_tick_ms = now;
        lv_tick_inc(elapsed);

        /* Apply pending sensor data */
        if (s_data_pending) {
            update_dashboard(&s_latest_data);
            s_data_pending = false;
        }

        /* Run LVGL timer handler */
        lv_timer_handler();

        xSemaphoreGive(s_lvgl_mutex);
    }
}
