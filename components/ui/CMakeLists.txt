# Select real or mock UI backend at compile time
if(CONFIG_HAL_USE_MOCK)
    set(UI_SRC "ui_mock.c")
else()
    set(UI_SRC "ui_real.c")
endif()

idf_component_register(
    SRCS ${UI_SRC}
    INCLUDE_DIRS "."
    REQUIRES
        esp_common
        esp_timer
        log
        freertos
        hal_display
        hal_sensors
        data_store
)
