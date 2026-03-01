idf_component_register(
    SRCS "sen66.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        log
        esp_check
)
