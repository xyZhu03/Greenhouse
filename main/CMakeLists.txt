idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_server esp_wifi nvs_flash esp_http_client mqtt driver esp_event esp_netif json bme68x ssd1306 esp_https_ota app_update)