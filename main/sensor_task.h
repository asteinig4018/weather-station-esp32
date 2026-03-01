#pragma once
/**
 * sensor_task.h — Periodic sensor polling task.
 *
 * Reads all sensors via the HAL at APP_SENSOR_POLL_MS intervals and posts
 * SENSOR_EVT_DATA events to the application event loop.
 */

#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Start the sensor polling task.
 *
 * Initialises the HAL sensor layer and creates a FreeRTOS task that
 * periodically reads sensors and posts SENSOR_EVT_DATA events.
 *
 * @param loop  Application event loop to post events to.
 * @return      ESP_OK on success.
 */
esp_err_t sensor_task_start(esp_event_loop_handle_t loop);

#ifdef __cplusplus
}
#endif
