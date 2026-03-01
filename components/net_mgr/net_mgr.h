#pragma once
/**
 * net_mgr.h — WiFi station manager + HTTP telemetry uploader.
 *
 * Connects to WiFi, posts sensor data to a configurable server URL
 * at regular intervals. Handles reconnection automatically.
 *
 * In mock mode: no WiFi/HTTP — just logs what would be uploaded.
 */

#include "esp_err.h"
#include "hal_sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialise the network manager.
 *
 * Real mode: starts WiFi STA, connects to configured SSID, starts
 * the upload task.
 * Mock mode: logs "WiFi mock — no connection".
 *
 * @return      ESP_OK on success.
 */
esp_err_t net_mgr_init(void);

/**
 * @brief  Feed sensor data to the network manager for upload.
 *
 * Called from the sensor event handler. The net_mgr buffers the latest
 * data and uploads it at the configured interval.
 *
 * @param data  Sensor snapshot.
 */
void net_mgr_feed_sensor_data(const hal_sensor_data_t *data);

/**
 * @brief  Check if WiFi is currently connected.
 */
bool net_mgr_is_connected(void);

#ifdef __cplusplus
}
#endif
