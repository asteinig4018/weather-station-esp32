#pragma once
/**
 * ui.h — LVGL user interface for the weather station.
 *
 * Provides a dashboard page (live readings) and a history page
 * (scrollable stored data). Navigation between pages via button events.
 */

#include "esp_err.h"
#include "hal_sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialise LVGL, create display driver, and build UI screens.
 *
 * Must be called after hal_display_init().
 *
 * @return  ESP_OK on success.
 */
esp_err_t ui_init(void);

/**
 * @brief  Update the dashboard with a new sensor snapshot.
 *
 * Thread-safe — can be called from any task. Internally queues an
 * LVGL update for the next tick handler run.
 *
 * @param data  Sensor snapshot.
 */
void ui_update_sensor_data(const hal_sensor_data_t *data);

/**
 * @brief  Navigate to the next UI page (cycles dashboard → history → dashboard).
 */
void ui_navigate_next(void);

/**
 * @brief  Run the LVGL tick handler. Call this periodically (~5 ms).
 *
 * In real mode, drives LVGL timer handler and display flush.
 * In mock mode, no-op (nothing to render).
 */
void ui_tick(void);

#ifdef __cplusplus
}
#endif
