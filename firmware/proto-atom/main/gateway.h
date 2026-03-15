#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>

/* ── Pins (Tail485 — SP485EEN-L auto-direction) ── */
#define RS485_TX_PIN    26
#define RS485_RX_PIN    32
#define RS485_UART      UART_NUM_1

/* ── Defaults ── */
#define DEFAULT_BAUD       19200
#define DEFAULT_DATA_BITS  8
#define DEFAULT_PARITY     0          /* 0=N 1=O 2=E */
#define DEFAULT_STOP_BITS  1
#define DEFAULT_RTU_TMO    500        /* ms */
#define DEFAULT_TCP_PORT   502
#define AP_PASS            "fieldtunnel123"
#define FW_VERSION         "0.2.0"
#define AP_IP              "192.168.4.1"

/* ── Sizes ── */
#define MAX_PDU   253
#define MAX_ADU   260
#define MBAP_LEN  7

/* ── Helpers ── */
#define GW_LOCK()   xSemaphoreTake(gw.lock, portMAX_DELAY)
#define GW_UNLOCK() xSemaphoreGive(gw.lock)

/* ── Shared state ── */
typedef struct {
    char     wifi_ssid[33];
    char     wifi_pass[65];
    bool     wifi_connected;
    bool     ap_mode;
    bool     nat_enabled;
    char     ip_addr[16];
    char     sta_ip[16];
    char     device_id[8];
    char     ap_ssid[32];
    char     hostname[32];

    uint32_t baud;
    uint8_t  data_bits;
    uint8_t  parity;
    uint8_t  stop_bits;
    uint16_t rtu_timeout;
    uint16_t tcp_port;
    uint8_t  mode;         /* 0=Modbus TCP GW  1=Raw Tunnel */

    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t err_count;
    int64_t  boot_time;
    char     last_error[64];
    char     mac_addr[18];

    SemaphoreHandle_t lock;
} gw_state_t;

extern gw_state_t   gw;
extern QueueHandle_t rtu_queue;

/* ── Config ── */
void gw_load_config(void);
void gw_save_config(void);

/* ── Tasks ── */
void wifi_task(void *arg);
void rs485_task(void *arg);
void tcp_server_task(void *arg);
void http_server_task(void *arg);

/* ── RS485 transaction ── */
typedef struct {
    uint8_t  unit_id;
    uint8_t  pdu[MAX_PDU];
    uint16_t pdu_len;
    uint8_t  resp[MAX_ADU];
    uint16_t resp_len;
    bool     ok;
    SemaphoreHandle_t done;
} rtu_txn_t;

bool     rs485_transact(rtu_txn_t *txn);
void     rs485_reconfigure(void);
uint16_t modbus_crc16(const uint8_t *buf, uint16_t len);
