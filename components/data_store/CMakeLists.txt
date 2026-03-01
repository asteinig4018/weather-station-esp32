# Select real or mock backend at compile time
if(CONFIG_HAL_USE_MOCK)
    set(DATA_STORE_SRC "data_store_mock.c")
else()
    set(DATA_STORE_SRC "data_store_real.c")
endif()

idf_component_register(
    SRCS ${DATA_STORE_SRC}
    INCLUDE_DIRS "."
    REQUIRES
        esp_common
        log
        hal_sensors
)
