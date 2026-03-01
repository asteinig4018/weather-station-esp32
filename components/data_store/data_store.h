#pragma once
/**
 * data_store.h — Timestamped sensor data ring buffer.
 *
 * Stores hal_sensor_data_t snapshots in a circular buffer. In real mode,
 * backed by a LittleFS partition for persistence across reboots. In mock
 * mode, backed by a RAM ring buffer.
 *
 * The ring buffer has a fixed capacity (DATA_STORE_MAX_ENTRIES). When full,
 * the oldest entry is overwritten.
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal_sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of entries the store can hold. */
#define DATA_STORE_MAX_ENTRIES  2048

/**
 * @brief  Initialise the data store.
 *
 * Real mode: mounts the LittleFS partition and loads the ring buffer index.
 * Mock mode: allocates RAM for the ring buffer.
 *
 * @return  ESP_OK on success.
 */
esp_err_t data_store_init(void);

/**
 * @brief  Append a sensor snapshot to the ring buffer.
 *
 * If the buffer is full, the oldest entry is overwritten.
 *
 * @param data  Sensor snapshot to store.
 * @return      ESP_OK on success.
 */
esp_err_t data_store_append(const hal_sensor_data_t *data);

/**
 * @brief  Get the number of entries currently stored.
 */
size_t data_store_count(void);

/**
 * @brief  Read an entry by index (0 = oldest, count-1 = newest).
 *
 * @param index  Logical index (0-based from oldest).
 * @param data   Output buffer for the sensor snapshot.
 * @return       ESP_OK on success, ESP_ERR_INVALID_ARG if index out of range.
 */
esp_err_t data_store_read(size_t index, hal_sensor_data_t *data);

/**
 * @brief  Read the most recent entry.
 *
 * @param data  Output buffer for the sensor snapshot.
 * @return      ESP_OK, or ESP_ERR_NOT_FOUND if store is empty.
 */
esp_err_t data_store_read_latest(hal_sensor_data_t *data);

/**
 * @brief  Flush any buffered writes to persistent storage.
 *
 * No-op in mock mode.
 */
esp_err_t data_store_flush(void);

/**
 * @brief  De-initialise the data store and free resources.
 */
esp_err_t data_store_deinit(void);

#ifdef __cplusplus
}
#endif
