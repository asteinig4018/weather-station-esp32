#pragma once
/**
 * web_server.h — Minimal HTTP server for OTA push + status API.
 *
 * Real mode: runs esp_http_server on port 80 with endpoints:
 *   GET  /api/status  — latest sensor data (JSON)
 *   GET  /api/info    — device info (JSON)
 *   POST /api/ota     — receive firmware binary, write to OTA partition, reboot
 *
 * Mock mode: no-op.
 */

#include "esp_err.h"
#include "hal_sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the HTTP server. */
esp_err_t web_server_init(void);

/** Cache the latest sensor snapshot (thread-safe). */
void web_server_feed_sensor_data(const hal_sensor_data_t *data);

/** Stop the HTTP server. */
esp_err_t web_server_deinit(void);

#ifdef __cplusplus
}
#endif
