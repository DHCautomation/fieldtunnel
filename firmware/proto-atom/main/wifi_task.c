#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "gateway.h"

static const char *TAG = "wifi";

static EventGroupHandle_t s_evt;
#define BIT_OK   BIT0
#define BIT_FAIL BIT1

static int s_retry = 0;
#define MAX_RETRY 5

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            GW_LOCK();
            gw.wifi_connected = false;
            gw.ip_addr[0] = '\0';
            GW_UNLOCK();
            if (s_retry < MAX_RETRY) {
                esp_wifi_connect();
                s_retry++;
            } else {
                xEventGroupSetBits(s_evt, BIT_FAIL);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        GW_LOCK();
        gw.wifi_connected = true;
        gw.ap_mode = false;
        snprintf(gw.ip_addr, sizeof(gw.ip_addr), IPSTR, IP2STR(&e->ip_info.ip));
        GW_UNLOCK();
        s_retry = 0;
        ESP_LOGI(TAG, "STA IP: %s", gw.ip_addr);
        xEventGroupSetBits(s_evt, BIT_OK);
    }
}

static void start_ap(void)
{
    wifi_config_t cfg = {
        .ap = {
            .ssid = AP_SSID,
            .password = AP_PASS,
            .ssid_len = strlen(AP_SSID),
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &cfg);
    esp_wifi_start();

    GW_LOCK();
    gw.ap_mode = true;
    gw.wifi_connected = false;
    strncpy(gw.ip_addr, "192.168.4.1", sizeof(gw.ip_addr));
    GW_UNLOCK();
    ESP_LOGI(TAG, "AP  SSID: %s  IP: 192.168.4.1", AP_SSID);
}

void wifi_task(void *arg)
{
    s_evt = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &event_handler, NULL, NULL);

    GW_LOCK();
    bool has_ssid = gw.wifi_ssid[0] != '\0';
    GW_UNLOCK();

    if (has_ssid) {
        wifi_config_t sta = {};
        GW_LOCK();
        strncpy((char *)sta.sta.ssid, gw.wifi_ssid, sizeof(sta.sta.ssid));
        strncpy((char *)sta.sta.password, gw.wifi_pass, sizeof(sta.sta.password));
        GW_UNLOCK();

        ESP_LOGI(TAG, "Connecting to '%s'...", (char *)sta.sta.ssid);
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &sta);
        esp_wifi_start();

        EventBits_t bits = xEventGroupWaitBits(s_evt,
            BIT_OK | BIT_FAIL, pdTRUE, pdFALSE, pdMS_TO_TICKS(15000));

        if (!(bits & BIT_OK)) {
            ESP_LOGW(TAG, "STA failed — falling back to AP");
            esp_wifi_stop();
            start_ap();
        }
    } else {
        start_ap();
    }

    /* Keep alive — could add reconnect logic here */
    while (1) { vTaskDelay(pdMS_TO_TICKS(30000)); }
}
