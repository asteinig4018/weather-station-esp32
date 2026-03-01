/**
 * sensor_task.c — Periodic sensor polling task.
 *
 * Reads all sensors via HAL and posts SENSOR_EVT_DATA to the app event loop.
 */

#include "sensor_task.h"
#include "events.h"
#include "hal_sensors.h"
#include "app_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "sensor_task";

static esp_event_loop_handle_t s_loop;

static void sensor_task(void *arg)
{
    (void)arg;

    hal_sensor_status_t st;
    esp_err_t ret = hal_sensors_init(&st);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "hal_sensors_init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Sensors initialised — SEN66:%s  BMP851:%s  ISM330DHC:%s",
             st.sen66_ok    ? "OK" : "FAIL",
             st.bmp851_ok   ? "OK" : "FAIL",
             st.ism330dhc_ok ? "OK" : "FAIL");

    TickType_t wake_time = xTaskGetTickCount();

    for (;;) {
        hal_sensor_data_t data;
        ret = hal_sensors_read(&data);
        if (ret == ESP_OK) {
            esp_event_post_to(s_loop, SENSOR_EVENTS, SENSOR_EVT_DATA,
                              &data, sizeof(data), pdMS_TO_TICKS(50));
        } else {
            ESP_LOGW(TAG, "hal_sensors_read failed: %s", esp_err_to_name(ret));
        }

        vTaskDelayUntil(&wake_time, pdMS_TO_TICKS(APP_SENSOR_POLL_MS));
    }
}

esp_err_t sensor_task_start(esp_event_loop_handle_t loop)
{
    s_loop = loop;

    BaseType_t ok = xTaskCreate(sensor_task, "sensor",
                                 APP_TASK_SENSOR_STACK / sizeof(StackType_t),
                                 NULL, APP_TASK_SENSOR_PRIO, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sensor task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
