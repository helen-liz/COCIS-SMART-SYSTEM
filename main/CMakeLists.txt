idf_component_register(SRCS "main.c" "room1.c" "room2.c" "entrance.c"
                            "communication.c" "storage.c" "display.c" "ui.c" "webui.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_i2c esp_event
                             esp_netif esp_wifi nvs_flash fatfs sdmmc esp_http_server)
