#include <string.h>
#include <ctype.h>
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
    if (nvs_get_u8(h, "mode",  &u8) == ESP_OK) gw.mode       = u8;
    if (nvs_get_u8(h, "bmac", &u8) == ESP_OK) gw.bacnet_mac = u8;
    if (nvs_get_u8(h, "bmax", &u8) == ESP_OK) gw.bacnet_max_master = u8;
    if (nvs_get_u16(h, "bport", &u16) == ESP_OK) gw.bacnet_port = u16;
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
    nvs_set_u8(h,  "mode",  gw.mode);
    nvs_set_u8(h,  "bmac", gw.bacnet_mac);
    nvs_set_u8(h,  "bmax", gw.bacnet_max_master);
    nvs_set_u16(h, "bport", gw.bacnet_port);
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

    /* Network stack */
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
    gw.mode        = 0;
    gw.bacnet_mac  = 5;
    gw.bacnet_max_master = 127;
    gw.bacnet_port = 47808;

    gw_load_config();

    /* ── Device identity from MAC ── */
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(gw.mac_addr, sizeof(gw.mac_addr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Short ID from last 2 MAC bytes */
    snprintf(gw.device_id, sizeof(gw.device_id), "%02X%02X", mac[4], mac[5]);

    /* Dynamic AP SSID */
    snprintf(gw.ap_ssid, sizeof(gw.ap_ssid), "FieldTunnel-%s", gw.device_id);

    /* Hostname (lowercase) */
    snprintf(gw.hostname, sizeof(gw.hostname), "fieldtunnel-%02x%02x", mac[4], mac[5]);

#ifdef CONFIG_LWIP_IPV4_NAPT
    ESP_LOGI(TAG, "NAPT: compiled in");
#else
    ESP_LOGW(TAG, "NAPT: NOT compiled — check sdkconfig");
#endif
    ESP_LOGI(TAG, "=== FieldTunnel v%s ===", FW_VERSION);
    ESP_LOGI(TAG, "ID: %s  MAC: %s", gw.device_id, gw.mac_addr);
    ESP_LOGI(TAG, "AP SSID: %s  Host: %s", gw.ap_ssid, gw.hostname);

    /* RTU request queue */
    rtu_queue = xQueueCreate(4, sizeof(rtu_txn_t *));

    /* FreeRTOS tasks */
    xTaskCreate(wifi_task,        "wifi",     4096, NULL, 5, NULL);
    xTaskCreate(rs485_task,       "rs485",    4096, NULL, 6, NULL);
    xTaskCreate(tcp_server_task,  "tcp_srv",  4096, NULL, 5, NULL);
    xTaskCreate(http_server_task, "http_srv", 12288, NULL, 4, NULL);
    xTaskCreate(bacnet_task,      "bacnet",   8192, NULL, 7, NULL);
}
