#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "gateway.h"

static const char *TAG = "http";

/* ───────────────────── File serving ───────────────────── */

static esp_err_t send_file(httpd_req_t *req, const char *path, const char *mime)
{
    char fp[48];
    snprintf(fp, sizeof(fp), "/spiffs%s", path);
    FILE *f = fopen(fp, "r");
    if (!f) { httpd_resp_send_404(req); return ESP_FAIL; }
    httpd_resp_set_type(req, mime);
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        httpd_resp_send_chunk(req, buf, n);
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t h_root(httpd_req_t *r) { return send_file(r, "/index.html", "text/html"); }
static esp_err_t h_css(httpd_req_t *r)  { return send_file(r, "/style.css",  "text/css"); }
static esp_err_t h_js(httpd_req_t *r)   { return send_file(r, "/app.js",     "application/javascript"); }

/* ───────────────────── GET /api/status ───────────────────── */

static esp_err_t api_status(httpd_req_t *req)
{
    int64_t up = (esp_timer_get_time() - gw.boot_time) / 1000000;
    GW_LOCK();
    char json[768];
    snprintf(json, sizeof(json),
        "{\"wifiConnected\":%s,\"apMode\":%s,\"ip\":\"%s\","
        "\"staIp\":\"%s\",\"apIp\":\"%s\","
        "\"deviceId\":\"%s\",\"hostname\":\"%s\",\"apSsid\":\"%s\","
        "\"natEnabled\":%s,"
        "\"port\":%u,\"baud\":%lu,\"dataBits\":%u,\"parity\":%u,\"stopBits\":%u,"
        "\"mode\":%u,\"rtuTimeout\":%u,\"lastError\":\"%s\","
        "\"tx\":%lu,\"rx\":%lu,\"err\":%lu,\"uptime\":%lld,"
        "\"mac\":\"%s\",\"fw\":\"%s\"}",
        gw.wifi_connected ? "true" : "false",
        gw.ap_mode ? "true" : "false",
        gw.ip_addr,
        gw.sta_ip, AP_IP,
        gw.device_id, gw.hostname, gw.ap_ssid,
        gw.nat_enabled ? "true" : "false",
        gw.tcp_port, (unsigned long)gw.baud,
        gw.data_bits, gw.parity, gw.stop_bits,
        gw.mode, gw.rtu_timeout, gw.last_error,
        (unsigned long)gw.tx_count, (unsigned long)gw.rx_count,
        (unsigned long)gw.err_count, (long long)up,
        gw.mac_addr, FW_VERSION);
    GW_UNLOCK();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

/* ───────────────────── GET /api/config ───────────────────── */

static esp_err_t api_config(httpd_req_t *req)
{
    GW_LOCK();
    char json[384];
    snprintf(json, sizeof(json),
        "{\"ssid\":\"%s\",\"baud\":%lu,\"dataBits\":%u,"
        "\"parity\":%u,\"stopBits\":%u,\"rtuTimeout\":%u,\"tcpPort\":%u,\"mode\":%u}",
        gw.wifi_ssid, (unsigned long)gw.baud,
        gw.data_bits, gw.parity, gw.stop_bits,
        gw.rtu_timeout, gw.tcp_port, gw.mode);
    GW_UNLOCK();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

/* ───────────────────── POST /api/wifi ───────────────────── */

static esp_err_t api_wifi(httpd_req_t *req)
{
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_FAIL; }

    cJSON *s = cJSON_GetObjectItem(root, "ssid");
    cJSON *p = cJSON_GetObjectItem(root, "pass");
    GW_LOCK();
    if (cJSON_IsString(s)) { memset(gw.wifi_ssid, 0, sizeof(gw.wifi_ssid)); strncpy(gw.wifi_ssid, s->valuestring, sizeof(gw.wifi_ssid)-1); }
    if (cJSON_IsString(p)) { memset(gw.wifi_pass, 0, sizeof(gw.wifi_pass)); strncpy(gw.wifi_pass, p->valuestring, sizeof(gw.wifi_pass)-1); }
    GW_UNLOCK();
    gw_save_config();
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* ───────────────────── POST /api/rs485 ───────────────────── */

static esp_err_t api_rs485(httpd_req_t *req)
{
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_FAIL; }

    cJSON *j;
    GW_LOCK();
    if ((j = cJSON_GetObjectItem(root, "baud"))       && cJSON_IsNumber(j)) gw.baud        = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "dataBits"))   && cJSON_IsNumber(j)) gw.data_bits   = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "parity"))     && cJSON_IsNumber(j)) gw.parity      = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "stopBits"))   && cJSON_IsNumber(j)) gw.stop_bits   = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "rtuTimeout")) && cJSON_IsNumber(j)) gw.rtu_timeout = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "tcpPort"))    && cJSON_IsNumber(j)) gw.tcp_port    = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "mode"))       && cJSON_IsNumber(j)) gw.mode        = j->valueint;
    GW_UNLOCK();
    gw_save_config();
    rs485_reconfigure();
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ───────────────────── POST /api/test ───────────────────── */

static esp_err_t api_test(httpd_req_t *req)
{
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_FAIL; }

    cJSON *jS = cJSON_GetObjectItem(root, "slaveId");
    cJSON *jF = cJSON_GetObjectItem(root, "fc");
    cJSON *jA = cJSON_GetObjectItem(root, "addr");
    cJSON *jC = cJSON_GetObjectItem(root, "count");
    if (!jS || !jF || !jA || !jC) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing fields");
        return ESP_FAIL;
    }

    uint8_t  slave = jS->valueint;
    uint8_t  fc    = jF->valueint;
    uint16_t addr  = jA->valueint;
    uint16_t count = jC->valueint;
    cJSON_Delete(root);

    uint8_t pdu[5];
    pdu[0] = fc;
    pdu[1] = addr >> 8;
    pdu[2] = addr & 0xFF;
    uint16_t val = count;
    if (fc == 5) val = count ? 0xFF00 : 0x0000;
    pdu[3] = val >> 8;
    pdu[4] = val & 0xFF;

    rtu_txn_t txn;
    txn.unit_id = slave;
    memcpy(txn.pdu, pdu, 5);
    txn.pdu_len = 5;

    bool ok = rs485_transact(&txn);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", ok);

    char hex[MAX_ADU * 3 + 1];
    int p = 0;
    p += snprintf(hex + p, sizeof(hex) - p, "%02X", slave);
    for (int i = 0; i < 5; i++)
        p += snprintf(hex + p, sizeof(hex) - p, " %02X", pdu[i]);
    cJSON_AddStringToObject(resp, "txHex", hex);
    cJSON_AddNumberToObject(resp, "startAddr", addr);

    if (ok && txn.resp_len > 0) {
        p = 0;
        for (int i = 0; i < txn.resp_len; i++)
            p += snprintf(hex + p, sizeof(hex) - p, "%s%02X", i ? " " : "", txn.resp[i]);
        cJSON_AddStringToObject(resp, "rxHex", hex);

        cJSON *vals = cJSON_CreateArray();
        uint8_t *rd = txn.resp + 1;
        uint8_t rfc = rd[0];

        if ((rfc == 3 || rfc == 4) && txn.resp_len > 3) {
            uint8_t bc = rd[1];
            for (int i = 0; i < bc / 2; i++) {
                uint16_t v = (rd[2 + i * 2] << 8) | rd[2 + i * 2 + 1];
                cJSON_AddItemToArray(vals, cJSON_CreateNumber(v));
            }
        } else if ((rfc == 1 || rfc == 2) && txn.resp_len > 3) {
            uint8_t bc = rd[1];
            for (int i = 0; i < bc * 8 && i < count; i++) {
                int bit = (rd[2 + i / 8] >> (i % 8)) & 1;
                cJSON_AddItemToArray(vals, cJSON_CreateNumber(bit));
            }
        } else if (rfc == 5 || rfc == 6) {
            uint16_t wv = (rd[3] << 8) | rd[4];
            cJSON_AddItemToArray(vals, cJSON_CreateNumber(wv));
        }
        cJSON_AddItemToObject(resp, "values", vals);
    } else {
        cJSON_AddStringToObject(resp, "rxHex", "");
        cJSON_AddItemToObject(resp, "values", cJSON_CreateArray());
        GW_LOCK();
        cJSON_AddStringToObject(resp, "error", gw.last_error);
        GW_UNLOCK();
    }

    char *json = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    cJSON_Delete(resp);
    return ESP_OK;
}

/* ───────────────────── GET /api/scan ───────────────────── */

static esp_err_t api_scan(httpd_req_t *req)
{
    wifi_scan_config_t sc = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&sc, true);

    if (err != ESP_OK) {
        ESP_LOGI(TAG, "scan failed err=%d", err);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"networks\":[]}");
        return ESP_OK;
    }

    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    ESP_LOGI(TAG, "scan ok n=%d", n);
    if (n > 20) n = 20;

    wifi_ap_record_t *ap = calloc(n, sizeof(wifi_ap_record_t));
    if (!ap) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"networks\":[]}");
        return ESP_OK;
    }
    esp_wifi_scan_get_ap_records(&n, ap);

    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (ap[j].rssi > ap[i].rssi) {
                wifi_ap_record_t tmp = ap[i]; ap[i] = ap[j]; ap[j] = tmp;
            }

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", (char *)ap[i].ssid);
        cJSON_AddNumberToObject(net, "rssi", ap[i].rssi);
        cJSON_AddNumberToObject(net, "auth", ap[i].authmode);
        cJSON_AddItemToArray(arr, net);
    }
    cJSON_AddItemToObject(root, "networks", arr);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    cJSON_Delete(root);
    free(ap);
    return ESP_OK;
}

/* ───────────────────── POST /api/ota/upload ───────────────────── */

static esp_err_t api_ota_upload(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA upload start, size=%d", req->content_len);

    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (!update) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(update, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin failed: %s", esp_err_to_name(err));
        char e[64];
        snprintf(e, sizeof(e), "{\"error\":\"%s\"}", esp_err_to_name(err));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, e);
        return ESP_OK;
    }

    int remaining = req->content_len;
    char buf[1024];
    while (remaining > 0) {
        int toread = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int recv_len = httpd_req_recv(req, buf, toread);
        if (recv_len <= 0) {
            ESP_LOGE(TAG, "OTA recv failed");
            esp_ota_abort(handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
            return ESP_FAIL;
        }
        err = esp_ota_write(handle, buf, recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(handle);
            char e[64];
            snprintf(e, sizeof(e), "{\"error\":\"%s\"}", esp_err_to_name(err));
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, e);
            return ESP_OK;
        }
        remaining -= recv_len;
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_end failed: %s", esp_err_to_name(err));
        char e[64];
        snprintf(e, sizeof(e), "{\"error\":\"%s\"}", esp_err_to_name(err));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, e);
        return ESP_OK;
    }

    err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot failed: %s", esp_err_to_name(err));
        char e[64];
        snprintf(e, sizeof(e), "{\"error\":\"%s\"}", esp_err_to_name(err));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, e);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "OTA success — rebooting");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"rebooting\"}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* ───────────────────── GET /api/ota/check ───────────────────── */

static esp_err_t api_ota_check(httpd_req_t *req)
{
    char remote_version[16] = {0};
    char notes[128] = {0};
    char url[256] = {0};
    bool fetch_ok = false;

    esp_http_client_config_t cfg = {
        .url = "https://fieldtunnel.com/releases/latest.json",
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    ESP_LOGI(TAG, "OTA check: fetching %s", cfg.url);
    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    char buf[512] = {0};
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        int content_len = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "OTA check: http status=%d content_len=%d", status, content_len);
        int len = esp_http_client_read(client, buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            ESP_LOGI(TAG, "OTA check: response=%s", buf);
            cJSON *j = cJSON_Parse(buf);
            if (j) {
                cJSON *v = cJSON_GetObjectItem(j, "version");
                cJSON *n = cJSON_GetObjectItem(j, "notes");
                cJSON *u = cJSON_GetObjectItem(j, "url");
                if (cJSON_IsString(v)) strncpy(remote_version, v->valuestring, 15);
                if (cJSON_IsString(n)) strncpy(notes, n->valuestring, 127);
                if (cJSON_IsString(u)) strncpy(url, u->valuestring, 255);
                cJSON_Delete(j);
                fetch_ok = true;
            }
        } else {
            ESP_LOGW(TAG, "OTA check: read returned %d", len);
        }
    } else {
        ESP_LOGW(TAG, "OTA check: fetch failed: %s", esp_err_to_name(err));
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    ESP_LOGI(TAG, "OTA check: remote=%s current=%s fetch_ok=%d",
             remote_version, FW_VERSION, fetch_ok);

    bool update_available = fetch_ok && strcmp(remote_version, FW_VERSION) != 0;

    char json[512];
    snprintf(json, sizeof(json),
        "{\"current\":\"%s\",\"available\":\"%s\","
        "\"updateAvailable\":%s,\"notes\":\"%s\","
        "\"url\":\"%s\",\"checkFailed\":%s}",
        FW_VERSION,
        fetch_ok ? remote_version : FW_VERSION,
        update_available ? "true" : "false",
        notes, url,
        fetch_ok ? "false" : "true");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

/* ───────────────────── POST /api/ota/fetch ───────────────────── */

static esp_err_t api_ota_fetch(httpd_req_t *req)
{
    char buf[384];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_FAIL; }

    cJSON *u = cJSON_GetObjectItem(root, "url");
    if (!cJSON_IsString(u)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing url");
        return ESP_FAIL;
    }

    char url[256];
    strncpy(url, u->valuestring, sizeof(url) - 1);
    url[sizeof(url) - 1] = '\0';
    cJSON_Delete(root);

    ESP_LOGI(TAG, "OTA fetch from: %s", url);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t err = esp_https_ota(&ota_cfg);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA fetch success — rebooting");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"rebooting\"}");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA fetch failed: %s", esp_err_to_name(err));
        char e[128];
        snprintf(e, sizeof(e), "{\"error\":\"%s\"}", esp_err_to_name(err));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, e);
    }
    return ESP_OK;
}

/* ───────────────────── GET/POST /api/bacnet ───────────────────── */

static esp_err_t api_bacnet_get(httpd_req_t *req)
{
    char json[128];
    snprintf(json, sizeof(json),
        "{\"mac\":%u,\"maxMaster\":%u,\"port\":%u}",
        gw.bacnet_mac, gw.bacnet_max_master, gw.bacnet_port);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

static esp_err_t api_bacnet_post(httpd_req_t *req)
{
    char buf[128];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return ESP_FAIL;
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json"); return ESP_FAIL; }

    cJSON *j;
    GW_LOCK();
    if ((j = cJSON_GetObjectItem(root, "mac"))       && cJSON_IsNumber(j)) gw.bacnet_mac        = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "maxMaster")) && cJSON_IsNumber(j)) gw.bacnet_max_master = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "port"))      && cJSON_IsNumber(j)) gw.bacnet_port       = j->valueint;
    GW_UNLOCK();
    gw_save_config();
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ───────────────────── POST /api/reboot ───────────────────── */

static esp_err_t api_reboot(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* ───────────────────── POST /api/resetstats ───────────────────── */

static esp_err_t api_resetstats(httpd_req_t *req)
{
    gw.tx_count  = 0;
    gw.rx_count  = 0;
    gw.err_count = 0;
    gw.boot_time = esp_timer_get_time();
    GW_LOCK(); gw.last_error[0] = '\0'; GW_UNLOCK();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

/* ───────────────────── Server start ───────────────────── */

void http_server_task(void *arg)
{
    while (!gw.wifi_connected && !gw.ap_mode)
        vTaskDelay(pdMS_TO_TICKS(500));

    /* SPIFFS */
    esp_vfs_spiffs_conf_t sc = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&sc));

    /* HTTP server */
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 20;
    cfg.stack_size = 10240;
    cfg.recv_wait_timeout = 30;
    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        vTaskDelete(NULL);
        return;
    }

    const httpd_uri_t uris[] = {
        { "/",               HTTP_GET,  h_root,         NULL },
        { "/index.html",     HTTP_GET,  h_root,         NULL },
        { "/style.css",      HTTP_GET,  h_css,          NULL },
        { "/app.js",         HTTP_GET,  h_js,           NULL },
        { "/api/status",     HTTP_GET,  api_status,     NULL },
        { "/api/config",     HTTP_GET,  api_config,     NULL },
        { "/api/wifi",       HTTP_POST, api_wifi,       NULL },
        { "/api/rs485",      HTTP_POST, api_rs485,      NULL },
        { "/api/test",       HTTP_POST, api_test,       NULL },
        { "/api/reboot",     HTTP_POST, api_reboot,     NULL },
        { "/api/resetstats", HTTP_POST, api_resetstats, NULL },
        { "/api/scan",       HTTP_GET,  api_scan,       NULL },
        { "/api/ota/upload", HTTP_POST, api_ota_upload, NULL },
        { "/api/ota/check",  HTTP_GET,  api_ota_check,  NULL },
        { "/api/ota/fetch",  HTTP_POST, api_ota_fetch,  NULL },
        { "/api/bacnet",     HTTP_GET,  api_bacnet_get, NULL },
        { "/api/bacnet",     HTTP_POST, api_bacnet_post,NULL },
    };
    for (int i = 0; i < sizeof(uris) / sizeof(uris[0]); i++)
        httpd_register_uri_handler(srv, &uris[i]);

    ESP_LOGI(TAG, "HTTP server on port 80 (%d endpoints)", (int)(sizeof(uris)/sizeof(uris[0])));
    while (1) { vTaskDelay(pdMS_TO_TICKS(60000)); }
}
