#pragma once
/**
 * events.h — Application event definitions.
 *
 * All inter-task communication uses esp_event. Each subsystem defines its
 * own event base and event IDs here so every file sees the same types.
 *
 * Usage:
 *   esp_event_handler_register_with(loop, SENSOR_EVENTS,
 *                                    SENSOR_EVT_DATA, handler, ctx);
 */

#include "esp_event.h"
#include "hal_sensors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Event bases — one per subsystem
 * ========================================================================= */
ESP_EVENT_DECLARE_BASE(SENSOR_EVENTS);
ESP_EVENT_DECLARE_BASE(BUTTON_EVENTS);
ESP_EVENT_DECLARE_BASE(SYSTEM_EVENTS);

/* =========================================================================
 * Sensor events
 * ========================================================================= */
typedef enum {
    SENSOR_EVT_DATA = 0,    /*!< New sensor snapshot available */
} sensor_event_id_t;

/* Payload for SENSOR_EVT_DATA is hal_sensor_data_t (from hal_sensors.h) */

/* =========================================================================
 * Button events
 * ========================================================================= */
typedef enum {
    BUTTON_EVT_UX_PRESS = 0,   /*!< UX button pressed */
    BUTTON_EVT_DBG0_PRESS,     /*!< Debug button 0 pressed */
    BUTTON_EVT_DBG1_PRESS,     /*!< Debug button 1 pressed */
} button_event_id_t;

/* =========================================================================
 * System events
 * ========================================================================= */
typedef enum {
    SYSTEM_EVT_POWER_GOOD = 0,  /*!< Power supply is good */
    SYSTEM_EVT_POWER_BAD,       /*!< Power supply lost / battery low */
} system_event_id_t;

#ifdef __cplusplus
}
#endif
