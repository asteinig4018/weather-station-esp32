idf_component_register(
    SRCS "ism330dhc.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        log
        esp_check
)
