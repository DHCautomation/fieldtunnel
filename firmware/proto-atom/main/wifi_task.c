#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mdns.h"
#include "dhcpserver/dhcpserver.h"
#include "gateway.h"

static const char *TAG = "wifi";

static esp_netif_t *s_ap_netif  = NULL;
static int s_retry = 0;
#define MAX_RETRY 10

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            GW_LOCK();
            bool has = gw.wifi_ssid[0] != '\0';
            GW_UNLOCK();
            if (has) esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            GW_LOCK();
            gw.wifi_connected = false;
            gw.sta_ip[0] = '\0';
            GW_UNLOCK();
            if (s_retry < MAX_RETRY) {
                s_retry++;
                ESP_LOGI(TAG, "STA retry %d/%d", s_retry, MAX_RETRY);
                vTaskDelay(pdMS_TO_TICKS(2000));
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "STA retries exhausted — AP still at %s", AP_IP);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        GW_LOCK();
        gw.wifi_connected = true;
        snprintf(gw.sta_ip, sizeof(gw.sta_ip), IPSTR, IP2STR(&e->ip_info.ip));
        snprintf(gw.ip_addr, sizeof(gw.ip_addr), "%s", gw.sta_ip);
        GW_UNLOCK();
        s_retry = 0;
        ESP_LOGI(TAG, "STA IP: %s", gw.sta_ip);

        /* Enable NAT + DNS forwarding for AP clients */
        if (s_ap_netif) {
            /* 1. Enable NAPT on AP interface */
            esp_err_t napt_err = esp_netif_napt_enable(s_ap_netif);
            ESP_LOGI(TAG, "NAPT enable: %s", esp_err_to_name(napt_err));

            /* 2. Set AP DNS to upstream gateway (usually the DNS server) */
            esp_netif_dns_info_t dns;
            dns.ip.u_addr.ip4 = e->ip_info.gw;
            dns.ip.type = ESP_IPADDR_TYPE_V4;

            /* 3. Restart DHCPS with DNS offer enabled */
            esp_netif_dhcps_stop(s_ap_netif);

            dhcps_offer_t dns_offer = OFFER_DNS;
            esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET,
                ESP_NETIF_DOMAIN_NAME_SERVER, &dns_offer, sizeof(dns_offer));
            esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);

            esp_netif_dhcps_start(s_ap_netif);

            ESP_LOGI(TAG, "NAT+DNS ready — route via %s, DNS via " IPSTR,
                     gw.sta_ip, IP2STR(&e->ip_info.gw));

            GW_LOCK();
            gw.nat_enabled = true;
            GW_UNLOCK();
        }
    }
}

void wifi_task(void *arg)
{
    esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &event_handler, NULL, NULL);

    /* ── Always APSTA ── */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    /* Configure AP with device-unique SSID */
    wifi_config_t ap_cfg = {
        .ap = {
            .password = AP_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)ap_cfg.ap.ssid, gw.ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(gw.ap_ssid);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

    /* Configure STA if credentials saved */
    GW_LOCK();
    bool has_ssid = gw.wifi_ssid[0] != '\0';
    GW_UNLOCK();

    if (has_ssid) {
        wifi_config_t sta_cfg = {};
        GW_LOCK();
        strncpy((char *)sta_cfg.sta.ssid, gw.wifi_ssid, sizeof(sta_cfg.sta.ssid));
        strncpy((char *)sta_cfg.sta.password, gw.wifi_pass, sizeof(sta_cfg.sta.password));
        GW_UNLOCK();
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
        ESP_LOGI(TAG, "STA target: '%s'", (char *)sta_cfg.sta.ssid);
    }

    /* Start WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());

    GW_LOCK();
    gw.ap_mode = true;
    strncpy(gw.ip_addr, AP_IP, sizeof(gw.ip_addr));
    GW_UNLOCK();

    ESP_LOGI(TAG, "APSTA  AP: %s (%s)  STA: %s",
             gw.ap_ssid, AP_IP, has_ssid ? "connecting..." : "no credentials");

    /* ── mDNS ── */
    ESP_ERROR_CHECK(mdns_init());
    mdns_hostname_set(gw.hostname);
    mdns_instance_name_set("FieldTunnel Gateway");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    mdns_service_add(NULL, "_modbus", "_tcp", gw.tcp_port, NULL, 0);
    ESP_LOGI(TAG, "mDNS: %s.local", gw.hostname);

    /* Keep alive */
    while (1) { vTaskDelay(pdMS_TO_TICKS(30000)); }
}
