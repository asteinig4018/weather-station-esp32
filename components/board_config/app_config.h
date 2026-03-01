#pragma once
/**
 * app_config.h — Tunable application parameters.
 *
 * Adjust these without touching driver code.
 */

/* =========================================================================
 * Sensor polling
 * ========================================================================= */
/** How often the sensor task reads all sensors (ms). */
#define APP_SENSOR_POLL_MS          2000

/** SEN66 measurement interval (ms) — must be ≥ 1000 ms per datasheet. */
#define APP_SEN66_MEAS_INTERVAL_MS  1000

/* =========================================================================
 * Display
 * ========================================================================= */
/** Display refresh rate (ms) — limits how often the UI redraws. */
#define APP_DISPLAY_REFRESH_MS      500

/* =========================================================================
 * FreeRTOS task stack sizes (bytes)
 * ========================================================================= */
#define APP_TASK_SENSOR_STACK   4096
#define APP_TASK_DISPLAY_STACK  8192
#define APP_TASK_BUTTON_STACK   2048

/* =========================================================================
 * FreeRTOS task priorities (higher number = higher priority)
 * ========================================================================= */
#define APP_TASK_SENSOR_PRIO    5
#define APP_TASK_DISPLAY_PRIO   4
#define APP_TASK_BUTTON_PRIO    6  /* Buttons need low latency */

/* =========================================================================
 * Logging
 * ========================================================================= */
#define APP_LOG_TAG  "weather_station"
