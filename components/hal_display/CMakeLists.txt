# Select real or mock backend at compile time
if(CONFIG_HAL_USE_MOCK)
    set(HAL_DISPLAY_SRC "hal_display_mock.c")
else()
    set(HAL_DISPLAY_SRC "hal_display_real.c")
endif()

idf_component_register(
    SRCS ${HAL_DISPLAY_SRC}
    INCLUDE_DIRS "."
    REQUIRES
        esp_common
        log
        board_config
        drv_ili9341
)
