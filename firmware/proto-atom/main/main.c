#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "gateway.h"

static const char *TAG = "main";

gw_state_t   gw;
QueueHandle_t rtu_queue;

/* ── NVS config ── */
void gw_load_config(void)
{
    nvs_handle_t h;
    if (nvs_open("gw", NVS_READONLY, &h) != ESP_OK) return;
    size_t len;
    len = sizeof(gw.wifi_ssid); nvs_get_str(h, "ssid", gw.wifi_ssid, &len);
    len = sizeof(gw.wifi_pass); nvs_get_str(h, "pass", gw.wifi_pass, &len);
    nvs_get_u32(h, "baud", &gw.baud);
    uint8_t u8;
    if (nvs_get_u8(h, "dbits", &u8) == ESP_OK) gw.data_bits = u8;
    if (nvs_get_u8(h, "par",   &u8) == ESP_OK) gw.parity    = u8;
    if (nvs_get_u8(h, "stop",  &u8) == ESP_OK) gw.stop_bits = u8;
    uint16_t u16;
    if (nvs_get_u16(h, "tmo",  &u16) == ESP_OK) gw.rtu_timeout = u16;
    if (nvs_get_u16(h, "port", &u16) == ESP_OK) gw.tcp_port    = u16;
    nvs_close(h);
}

void gw_save_config(void)
{
    nvs_handle_t h;
    if (nvs_open("gw", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", gw.wifi_ssid);
    nvs_set_str(h, "pass", gw.wifi_pass);
    nvs_set_u32(h, "baud", gw.baud);
    nvs_set_u8(h,  "dbits", gw.data_bits);
    nvs_set_u8(h,  "par",   gw.parity);
    nvs_set_u8(h,  "stop",  gw.stop_bits);
    nvs_set_u16(h, "tmo",   gw.rtu_timeout);
    nvs_set_u16(h, "port",  gw.tcp_port);
    nvs_commit(h);
    nvs_close(h);
}

void app_main(void)
{
    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Network stack (once, before any task) */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Defaults */
    memset(&gw, 0, sizeof(gw));
    gw.lock        = xSemaphoreCreateMutex();
    gw.boot_time   = esp_timer_get_time();
    gw.baud        = DEFAULT_BAUD;
    gw.data_bits   = DEFAULT_DATA_BITS;
    gw.parity      = DEFAULT_PARITY;
    gw.stop_bits   = DEFAULT_STOP_BITS;
    gw.rtu_timeout = DEFAULT_RTU_TMO;
    gw.tcp_port    = DEFAULT_TCP_PORT;

    gw_load_config();

    /* MAC */
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(gw.mac_addr, sizeof(gw.mac_addr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "=== FieldTunnel v%s ===", FW_VERSION);
    ESP_LOGI(TAG, "MAC: %s", gw.mac_addr);

    /* RTU request queue (pointer-sized items) */
    rtu_queue = xQueueCreate(4, sizeof(rtu_txn_t *));

    /* FreeRTOS tasks */
    xTaskCreate(wifi_task,        "wifi",     4096, NULL, 5, NULL);
    xTaskCreate(rs485_task,       "rs485",    4096, NULL, 6, NULL);
    xTaskCreate(tcp_server_task,  "tcp_srv",  4096, NULL, 5, NULL);
    xTaskCreate(http_server_task, "http_srv", 8192, NULL, 4, NULL);
}
