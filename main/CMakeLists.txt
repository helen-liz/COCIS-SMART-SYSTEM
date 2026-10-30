idf_component_register(SRCS "main.c" "storage.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_sdspi fatfs nvs_flash sdmmc spiffs)
