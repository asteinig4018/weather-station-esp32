#pragma once
/**
 * ota_mgr.h — OTA firmware update manager.
 *
 * Checks for firmware updates at a configurable URL and performs
 * OTA updates using esp_https_ota. Supports periodic checks and
 * on-demand triggering.
 *
 * In mock mode: logs check/update actions but does nothing.
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialise the OTA manager.
 *
 * Real mode: starts a background task that periodically checks for updates.
 * Mock mode: logs initialisation.
 *
 * @return  ESP_OK on success.
 */
esp_err_t ota_mgr_init(void);

/**
 * @brief  Trigger an immediate OTA check.
 *
 * Wakes the OTA task to check for updates now instead of waiting
 * for the next periodic check.
 */
void ota_mgr_check_now(void);

#ifdef __cplusplus
}
#endif
