idf_component_register(
    SRCS
        "app_main.c"
        "media_crc32.c"
        "media_storage.c"
        "media_uart.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_uart
)
