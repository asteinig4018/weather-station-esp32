# Select real or mock backend at compile time
if(CONFIG_HAL_USE_MOCK)
    set(HAL_SENSORS_SRC "hal_sensors_mock.c")
else()
    set(HAL_SENSORS_SRC "hal_sensors_real.c")
endif()

idf_component_register(
    SRCS ${HAL_SENSORS_SRC}
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_timer
        esp_common
        log
        freertos
        board_config
        drv_sen66
        drv_bmp851
        drv_ism330dhc
)
