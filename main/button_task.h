#pragma once
/**
 * button_task.h — Button polling task with debounce.
 *
 * Polls the three buttons (UX, DBG0, DBG1) and posts BUTTON_EVT_*
 * events on press detection.
 */

#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Start the button polling task.
 *
 * @param loop  Application event loop to post button events to.
 * @return      ESP_OK on success.
 */
esp_err_t button_task_start(esp_event_loop_handle_t loop);

#ifdef __cplusplus
}
#endif
