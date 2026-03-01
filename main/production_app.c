/**
 * production_app.c — Full application: event loop + tasks + UI.
 *
 * Sets up a dedicated application event loop, starts the sensor and button
 * tasks, initialises the UI, and runs the LVGL display task.
 */

#include "production_app.h"
#include "events.h"
#include "sensor_task.h"
#include "button_task.h"
#include "hal_display.h"
#include "data_store.h"
#include "ui.h"
#include "net_mgr.h"
#include "ota_mgr.h"
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

    /* Update UI */
    ui_update_sensor_data(d);

    /* Feed to network manager for upload */
    net_mgr_feed_sensor_data(d);

    ESP_LOGD(TAG, "SENSOR | T=%.1f°C  P=%.0fPa  PM2.5=%.1f  stored=%u",
             d->baro.temp_c, d->baro.pressure_pa,
             d->air.pm2p5, (unsigned)data_store_count());
}

static void on_button(void *handler_arg, esp_event_base_t base,
                       int32_t id, void *event_data)
{
    (void)handler_arg;
    (void)base;
    (void)event_data;

    switch (id) {
        case BUTTON_EVT_UX_PRESS:
            ESP_LOGI(TAG, "BUTTON | UX → navigate");
            ui_navigate_next();
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
 * Display task — runs LVGL tick handler
 * ========================================================================= */

static void display_task(void *arg)
{
    (void)arg;

    for (;;) {
        ui_tick();
        vTaskDelay(pdMS_TO_TICKS(5));
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

    /* --- Initialise UI --------------------------------------------------- */
    ESP_ERROR_CHECK(ui_init());
    ESP_LOGI(TAG, "UI initialised");

    /* --- Register event handlers ----------------------------------------- */
    ESP_ERROR_CHECK(esp_event_handler_register_with(
        loop, SENSOR_EVENTS, SENSOR_EVT_DATA, on_sensor_data, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register_with(
        loop, BUTTON_EVENTS, ESP_EVENT_ANY_ID, on_button, NULL));

    ESP_LOGI(TAG, "Event handlers registered");

    /* --- Initialise network manager -------------------------------------- */
    ret = net_mgr_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Network manager init failed: %s (continuing without upload)",
                 esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Network manager initialised");
    }

    /* --- Initialise OTA manager ------------------------------------------ */
    ret = ota_mgr_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OTA manager init failed: %s (continuing without OTA)",
                 esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "OTA manager initialised");
    }

    /* --- Start tasks ----------------------------------------------------- */
    ESP_ERROR_CHECK(sensor_task_start(loop));
    ESP_ERROR_CHECK(button_task_start(loop));

    /* Display task — runs LVGL tick handler at ~200 Hz */
    BaseType_t ok = xTaskCreate(display_task, "display",
                                 APP_TASK_DISPLAY_STACK / sizeof(StackType_t),
                                 NULL, APP_TASK_DISPLAY_PRIO, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display task");
    }

    ESP_LOGI(TAG, "All tasks started — running");

    /* Main task idles */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
