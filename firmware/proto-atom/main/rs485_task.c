#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "gateway.h"

static const char *TAG = "rs485";

/* ── CRC-16/IBM (Modbus) ── */
uint16_t modbus_crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= *buf++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

/* 3.5 character times in ticks (min 2ms) */
static TickType_t t35(void)
{
    uint32_t us = (uint32_t)(38.5 * 1000000.0 / gw.baud);
    if (us < 1750) us = 1750;
    TickType_t t = pdMS_TO_TICKS((us + 999) / 1000);
    return t < 1 ? 1 : t;
}

static void uart_setup(void)
{
    uart_word_length_t db = (gw.data_bits == 7) ? UART_DATA_7_BITS : UART_DATA_8_BITS;
    uart_parity_t      pa = UART_PARITY_DISABLE;
    if (gw.parity == 1) pa = UART_PARITY_ODD;
    if (gw.parity == 2) pa = UART_PARITY_EVEN;
    uart_stop_bits_t   sb = (gw.stop_bits == 2) ? UART_STOP_BITS_2 : UART_STOP_BITS_1;

    uart_config_t cfg = {
        .baud_rate  = (int)gw.baud,
        .data_bits  = db,
        .parity     = pa,
        .stop_bits  = sb,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(RS485_UART, &cfg);
    ESP_LOGI(TAG, "%lu %d%c%d  tmo=%ums",
             (unsigned long)gw.baud, gw.data_bits,
             "NOE"[gw.parity], gw.stop_bits, gw.rtu_timeout);
}

void rs485_reconfigure(void) { uart_setup(); }

void rs485_task(void *arg)
{
    uart_driver_install(RS485_UART, 512, 0, 0, NULL, 0);
    uart_set_pin(RS485_UART, RS485_TX_PIN, RS485_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_setup();
    ESP_LOGI(TAG, "RS485 task ready");

    rtu_txn_t *txn;
    while (1) {
        if (xQueueReceive(rtu_queue, &txn, portMAX_DELAY) != pdTRUE)
            continue;

        /* ── Build RTU frame ── */
        uint8_t frame[MAX_ADU];
        frame[0] = txn->unit_id;
        memcpy(frame + 1, txn->pdu, txn->pdu_len);
        uint16_t flen = txn->pdu_len + 1;
        uint16_t crc  = modbus_crc16(frame, flen);
        frame[flen]     = crc & 0xFF;
        frame[flen + 1] = crc >> 8;
        flen += 2;

        /* Pre-TX silence + flush */
        vTaskDelay(t35());
        uart_flush_input(RS485_UART);

        /* TX */
        uart_write_bytes(RS485_UART, frame, flen);
        uart_wait_tx_done(RS485_UART, pdMS_TO_TICKS(1000));
        gw.tx_count++;

        /* ── RX with inter-frame silence detection ── */
        uint16_t rxlen = 0;
        TickType_t gap = t35();
        TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(gw.rtu_timeout);

        while (rxlen < MAX_ADU && xTaskGetTickCount() < end) {
            int n = uart_read_bytes(RS485_UART, txn->resp + rxlen,
                                    MAX_ADU - rxlen, gap);
            if (n > 0)       rxlen += n;
            else if (rxlen)  break;          /* silence → frame complete */
        }

        /* ── Validate ── */
        if (rxlen < 4) {
            txn->ok = false;
            txn->resp_len = 0;
            gw.err_count++;
            GW_LOCK();
            snprintf(gw.last_error, sizeof(gw.last_error),
                     "Timeout (%d bytes)", rxlen);
            GW_UNLOCK();
        } else {
            uint16_t got  = txn->resp[rxlen - 2] | (txn->resp[rxlen - 1] << 8);
            uint16_t calc = modbus_crc16(txn->resp, rxlen - 2);
            if (got != calc) {
                txn->ok = false;
                txn->resp_len = 0;
                gw.err_count++;
                GW_LOCK();
                snprintf(gw.last_error, sizeof(gw.last_error),
                         "CRC rx=%04X calc=%04X", got, calc);
                GW_UNLOCK();
                ESP_LOGW(TAG, "%s", gw.last_error);
            } else {
                txn->ok = true;
                txn->resp_len = rxlen - 2;   /* strip CRC */
                gw.rx_count++;
            }
        }

        xSemaphoreGive(txn->done);
    }
}

bool rs485_transact(rtu_txn_t *txn)
{
    txn->done     = xSemaphoreCreateBinary();
    txn->ok       = false;
    txn->resp_len = 0;

    rtu_txn_t *ptr = txn;
    if (xQueueSend(rtu_queue, &ptr, pdMS_TO_TICKS(5000)) != pdTRUE) {
        vSemaphoreDelete(txn->done);
        return false;
    }

    bool got = xSemaphoreTake(txn->done,
                   pdMS_TO_TICKS(gw.rtu_timeout + 2000)) == pdTRUE;
    vSemaphoreDelete(txn->done);
    return got && txn->ok;
}
