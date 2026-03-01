idf_component_register(
    SRCS
        "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_lcd
        freertos
        log
        esp_check
        drv_ili9341
        drv_sen66
        drv_ism330dhc
        drv_bmp851
)
