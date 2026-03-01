# Select real or mock backend at compile time
if(CONFIG_HAL_USE_MOCK)
    set(NET_MGR_SRC "net_mgr_mock.c")
else()
    set(NET_MGR_SRC "net_mgr_real.c")
endif()

idf_component_register(
    SRCS ${NET_MGR_SRC}
    INCLUDE_DIRS "."
    REQUIRES
        esp_common
        esp_event
        esp_wifi
        esp_netif
        esp_http_client
        esp_timer
        nvs_flash
        log
        freertos
        hal_sensors
)
