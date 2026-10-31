idf_component_register(SRCS "main.c" "storage.c" "communication.c"
                "room1.c" "room2.c" "entrance.c"
                "display.c" "ui.c" "webui.c"
                    INCLUDE_DIRS "."
            REQUIRES esp_driver_gpio esp_driver_spi esp_driver_i2c
                 esp_driver_sdspi esp_event esp_netif esp_wifi
                 esp_http_server fatfs nvs_flash sdmmc spiffs)
