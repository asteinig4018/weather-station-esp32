/**
 * production_app.c — Full application: event loop + tasks.
 *
 * Sets up a dedicated application event loop, starts the sensor and button
 * tasks, and registers event handlers. The display handler currently logs
 * received data; Phase 4 will replace this with LVGL rendering.
 */

#include "production_app.h"
#include "events.h"
#include "sensor_task.h"
#include "button_task.h"
#include "hal_display.h"
#include "data_store.h"
#include "app_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"

static const char *TAG = "app";

/* =========================================================================
 * Event handlers
 * ========================================================================= */

static void on_sensor_data(void *handler_arg, esp_event_base_t base,
                            int32_t id, void *event_data)
{
    (void)handler_arg;
    (void)base;
    (void)id;

    const hal_sensor_data_t *d = (const hal_sensor_data_t *)event_data;

    /* Store to ring buffer */
    data_store_append(d);

    ESP_LOGI(TAG, "SENSOR | T=%.1f°C  P=%.0fPa  PM2.5=%.1f  RH=%.0f%%  VOC=%.0f  "
                   "stored=%u",
             d->baro.temp_c, d->baro.pressure_pa,
             d->air.pm2p5, d->air.hum_pct, d->air.voc,
             (unsigned)data_store_count());

    /* TODO Phase 4: update LVGL dashboard with latest data */
}

static void on_button(void *handler_arg, esp_event_base_t base,
                       int32_t id, void *event_data)
{
    (void)handler_arg;
    (void)base;
    (void)event_data;

    switch (id) {
        case BUTTON_EVT_UX_PRESS:
            ESP_LOGI(TAG, "BUTTON | UX pressed");
            /* TODO Phase 4: navigate UI pages */
            break;
        case BUTTON_EVT_DBG0_PRESS:
            ESP_LOGI(TAG, "BUTTON | DBG0 pressed");
            break;
        case BUTTON_EVT_DBG1_PRESS:
            ESP_LOGI(TAG, "BUTTON | DBG1 pressed");
            break;
        default:
            break;
    }
}

/* =========================================================================
 * Application entry
 * ========================================================================= */

void production_app_run(void)
{
    ESP_LOGI(TAG, "Starting production application...");

    /* --- Create application event loop ----------------------------------- */
    esp_event_loop_args_t loop_args = {
        .queue_size      = 16,
        .task_name       = "app_evt",
        .task_priority   = tskIDLE_PRIORITY + 1,
        .task_stack_size = 4096,
        .task_core_id    = tskNO_AFFINITY,
    };

    esp_event_loop_handle_t loop;
    ESP_ERROR_CHECK(esp_event_loop_create(&loop_args, &loop));
    ESP_LOGI(TAG, "Application event loop created");

    /* --- Initialise data store ------------------------------------------- */
    ESP_ERROR_CHECK(data_store_init());
    ESP_LOGI(TAG, "Data store initialised (%u existing entries)",
             (unsigned)data_store_count());

    /* --- Initialise display ---------------------------------------------- */
    esp_err_t ret = hal_display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Display initialised");
    }

    /* --- Register event handlers ----------------------------------------- */
    ESP_ERROR_CHECK(esp_event_handler_register_with(
        loop, SENSOR_EVENTS, SENSOR_EVT_DATA, on_sensor_data, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register_with(
        loop, BUTTON_EVENTS, ESP_EVENT_ANY_ID, on_button, NULL));

    ESP_LOGI(TAG, "Event handlers registered");

    /* --- Start tasks ----------------------------------------------------- */
    ESP_ERROR_CHECK(sensor_task_start(loop));
    ESP_ERROR_CHECK(button_task_start(loop));

    ESP_LOGI(TAG, "All tasks started — running");

    /* Main task can idle or be used for watchdog / diagnostics later */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
