if(CONFIG_HAL_USE_MOCK)
    set(WEB_SRC "web_server_mock.c")
else()
    set(WEB_SRC "web_server_real.c")
endif()

idf_component_register(
    SRCS ${WEB_SRC}
    INCLUDE_DIRS "."
    REQUIRES
        esp_common
        esp_http_server
        app_update
        esp_timer
        esp_wifi
        esp_netif
        freertos
        log
        hal_sensors
        data_store
)
