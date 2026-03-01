idf_component_register(
    SRCS "ili9341.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_lcd
        esp_rom
        log
)
