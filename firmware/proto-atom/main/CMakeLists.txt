idf_component_register(
    SRCS "main.c" "wifi_task.c" "rs485_task.c" "tcp_server.c" "http_server.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_http_server spiffs driver esp_event esp_netif json esp_timer app_update
    PRIV_REQUIRES mdns
)
