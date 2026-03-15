#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "gateway.h"

static const char *TAG = "tcp";

/* ── Raw TCP Tunnel: bidirectional byte pipe ── */
static void handle_raw_client(int sock)
{
    uint8_t buf[256];

    /* Make socket non-blocking for select() */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    ESP_LOGI(TAG, "Raw tunnel active");

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 }; /* 100 ms */

        int sel = select(sock + 1, &rfds, NULL, NULL, &tv);

        /* sock → RS485 */
        if (sel > 0 && FD_ISSET(sock, &rfds)) {
            int n = recv(sock, buf, sizeof(buf), 0);
            if (n <= 0) break;                        /* client gone */
            uart_write_bytes(RS485_UART, buf, n);
            uart_wait_tx_done(RS485_UART, pdMS_TO_TICKS(500));
            gw.tx_count++;
        }

        /* RS485 → sock */
        int avail = 0;
        uart_get_buffered_data_len(RS485_UART, (size_t *)&avail);
        if (avail > 0) {
            if (avail > (int)sizeof(buf)) avail = sizeof(buf);
            int n = uart_read_bytes(RS485_UART, buf, avail, 0);
            if (n > 0) {
                if (send(sock, buf, n, 0) <= 0) break;
                gw.rx_count++;
            }
        }
    }
}

/* ── Modbus TCP Gateway ── */
static void handle_modbus_client(int sock)
{
    uint8_t mbap[MBAP_LEN];

    while (1) {
        int n = recv(sock, mbap, MBAP_LEN, MSG_WAITALL);
        if (n != MBAP_LEN) break;

        uint16_t tid = (mbap[0] << 8) | mbap[1];
        uint16_t pid = (mbap[2] << 8) | mbap[3];
        uint16_t len = (mbap[4] << 8) | mbap[5];
        uint8_t  uid = mbap[6];

        if (pid != 0 || len < 2 || len > MAX_PDU + 1) { gw.err_count++; break; }

        uint16_t pdu_len = len - 1;
        uint8_t  pdu[MAX_PDU];
        n = recv(sock, pdu, pdu_len, MSG_WAITALL);
        if (n != (int)pdu_len) break;

        /* Bridge → RTU */
        rtu_txn_t txn;
        txn.unit_id = uid;
        memcpy(txn.pdu, pdu, pdu_len);
        txn.pdu_len = pdu_len;

        uint8_t out[MBAP_LEN + MAX_ADU];

        if (rs485_transact(&txn)) {
            out[0] = tid >> 8;        out[1] = tid;
            out[2] = 0;              out[3] = 0;
            out[4] = txn.resp_len >> 8;
            out[5] = txn.resp_len;
            memcpy(out + 6, txn.resp, txn.resp_len);
            send(sock, out, 6 + txn.resp_len, 0);
        } else {
            /* Exception 0x0B: Gateway Target Device Failed to Respond */
            out[0] = tid >> 8;  out[1] = tid;
            out[2] = 0;         out[3] = 0;
            out[4] = 0;         out[5] = 3;
            out[6] = uid;
            out[7] = pdu[0] | 0x80;
            out[8] = 0x0B;
            send(sock, out, 9, 0);
        }
    }
}

void tcp_server_task(void *arg)
{
    /* Wait for network */
    while (!gw.wifi_connected && !gw.ap_mode)
        vTaskDelay(pdMS_TO_TICKS(500));

    int srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv < 0) { ESP_LOGE(TAG, "socket()"); vTaskDelete(NULL); return; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(gw.tcp_port),
        .sin_addr.s_addr = INADDR_ANY,
    };
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind()"); close(srv); vTaskDelete(NULL); return;
    }
    listen(srv, 4);
    ESP_LOGI(TAG, "Modbus TCP on port %d", gw.tcp_port);

    while (1) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int client = accept(srv, (struct sockaddr *)&ca, &cl);
        if (client < 0) continue;

        ESP_LOGI(TAG, "+client");
        struct timeval tv = { .tv_sec = 60 };
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (gw.mode == 1)
            handle_raw_client(client);
        else
            handle_modbus_client(client);
        close(client);
        ESP_LOGI(TAG, "-client");
    }
}
