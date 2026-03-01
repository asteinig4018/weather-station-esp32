# Select real or mock backend at compile time
if(CONFIG_HAL_USE_MOCK)
    set(OTA_MGR_SRC "ota_mgr_mock.c")
else()
    set(OTA_MGR_SRC "ota_mgr_real.c")
endif()

idf_component_register(
    SRCS ${OTA_MGR_SRC}
    INCLUDE_DIRS "."
    REQUIRES
        esp_common
        esp_https_ota
        esp_http_client
        app_update
        log
        freertos
)
