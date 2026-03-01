# Header-only component — board pin definitions and app config.
# No source files; just makes board.h and app_config.h available
# to all components that REQUIRE board_config.
idf_component_register(
    INCLUDE_DIRS "."
    REQUIRES
        esp_common
        soc
        driver
        esp_driver_gpio
        esp_driver_i2c
)
