/**
 * events.c — Event base definitions.
 *
 * ESP_EVENT_DEFINE_BASE must appear in exactly one translation unit per base.
 */

#include "events.h"

ESP_EVENT_DEFINE_BASE(SENSOR_EVENTS);
ESP_EVENT_DEFINE_BASE(BUTTON_EVENTS);
ESP_EVENT_DEFINE_BASE(SYSTEM_EVENTS);
