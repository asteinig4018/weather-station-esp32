set(SRCS "main.c" "events.c")

if(CONFIG_APP_MODE_DEBUG)
    list(APPEND SRCS "debug_app.c")
endif()

if(CONFIG_APP_MODE_PRODUCTION)
    list(APPEND SRCS "production_app.c" "sensor_task.c" "button_task.c")
endif()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_lcd
        esp_event
        esp_timer
        freertos
        log
        esp_common
        board_config
        hal_sensors
        hal_display
        data_store
        ui
        net_mgr
        drv_ili9341
        drv_sen66
        drv_ism330dhc
        drv_bmp851
)
