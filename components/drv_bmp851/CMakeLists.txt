idf_component_register(
    SRCS "bmp851.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        log
        esp_check
)
