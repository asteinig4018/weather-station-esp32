/**
 * button_task.c — Button polling task with software debounce.
 *
 * All buttons are active-LOW with pull-ups. The task polls at 20 ms and
 * requires DEBOUNCE_COUNT consecutive LOW reads before registering a press.
 * After a press event, the button must return HIGH before another press
 * is registered (edge-triggered, not level-triggered).
 */

#include "button_task.h"
#include "events.h"
#include "board.h"
#include "app_config.h"

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#if !CONFIG_HAL_USE_MOCK
#include "driver/gpio.h"
#endif

static const char *TAG = "button_task";

#define POLL_MS         20
#define DEBOUNCE_COUNT  3  /* 3 × 20 ms = 60 ms debounce */

static esp_event_loop_handle_t s_loop;

typedef struct {
    int          gpio;
    int32_t      event_id;
    uint8_t      low_count;
    bool         pressed;    /* true while held, prevents repeat */
} btn_state_t;

static int btn_read_level(int gpio)
{
#if CONFIG_HAL_USE_MOCK
    (void)gpio;
    return 1;  /* always released in mock mode */
#else
    return gpio_get_level(gpio);
#endif
}

static void button_task(void *arg)
{
    (void)arg;

    btn_state_t btns[] = {
        { BOARD_BTN_UX,   BUTTON_EVT_UX_PRESS,   0, false },
        { BOARD_BTN_DBG0, BUTTON_EVT_DBG0_PRESS,  0, false },
        { BOARD_BTN_DBG1, BUTTON_EVT_DBG1_PRESS,  0, false },
    };
    const int n = sizeof(btns) / sizeof(btns[0]);

    ESP_LOGI(TAG, "Button task started (poll=%d ms, debounce=%d ms)",
             POLL_MS, POLL_MS * DEBOUNCE_COUNT);

    for (;;) {
        for (int i = 0; i < n; i++) {
            int level = btn_read_level(btns[i].gpio);

            if (level == 0) {
                /* Button is LOW (pressed) */
                if (!btns[i].pressed) {
                    btns[i].low_count++;
                    if (btns[i].low_count >= DEBOUNCE_COUNT) {
                        btns[i].pressed = true;
                        ESP_LOGI(TAG, "Button press: event_id=%ld", btns[i].event_id);
                        esp_event_post_to(s_loop, BUTTON_EVENTS,
                                          btns[i].event_id,
                                          NULL, 0, pdMS_TO_TICKS(10));
                    }
                }
            } else {
                /* Button is HIGH (released) */
                btns[i].low_count = 0;
                btns[i].pressed = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t button_task_start(esp_event_loop_handle_t loop)
{
    s_loop = loop;

    BaseType_t ok = xTaskCreate(button_task, "button",
                                 APP_TASK_BUTTON_STACK / sizeof(StackType_t),
                                 NULL, APP_TASK_BUTTON_PRIO, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
