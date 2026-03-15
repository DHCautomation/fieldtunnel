/*
 * BACnet MS/TP Gateway Task
 * Implements MS/TP datalink on UART1 (G26 TX, G32 RX)
 * Bridges BACnet/IP (UDP 47808) to MS/TP frames
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "gateway.h"

static const char *TAG = "bacnet";

/* ── MS/TP Frame Types ── */
#define MSTP_PREAMBLE1     0x55
#define MSTP_PREAMBLE2     0xFF
#define MSTP_TOKEN         0x00
#define MSTP_POLL_FOR_MASTER 0x01
#define MSTP_REPLY_PFM     0x02
#define MSTP_TEST_REQUEST  0x03
#define MSTP_TEST_RESPONSE 0x04
#define MSTP_BACNET_DATA_NO_REPLY  0x05
#define MSTP_BACNET_DATA_REPLY     0x06
#define MSTP_REPLY_POSTPONED       0x07

/* ── MS/TP CRC ── */
static uint8_t mstp_crc8(uint8_t crc, uint8_t byte)
{
    uint8_t x = crc ^ byte;
    for (int i = 0; i < 8; i++) {
        if (x & 1) x ^= 0x1D;
        x >>= 1;
    }
    return x;
}

static uint16_t mstp_crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        uint8_t b = *buf++;
        uint8_t lsb = (uint8_t)(((crc ^ b) & 0xFF) ^ ((crc ^ b) >> 1));
        if (lsb & 1) lsb ^= 0xA6;
        lsb ^= (uint8_t)((crc & 0xFF) >> 1);
        uint8_t msb = (uint8_t)(crc >> 8);
        msb ^= (uint8_t)((crc ^ b) >> 7);
        if (((crc ^ b) >> 6) & 1) msb ^= 0x01;
        crc = ((uint16_t)msb << 8) | lsb;
    }
    return ~crc;
}

/* ── MS/TP Frame ── */
typedef struct {
    uint8_t  type;
    uint8_t  dst;
    uint8_t  src;
    uint16_t len;
    uint8_t  data[512];
    bool     valid;
} mstp_frame_t;

/* ── Send MS/TP frame on UART ── */
static void mstp_send_frame(uint8_t type, uint8_t dst, uint8_t src,
                             const uint8_t *data, uint16_t len)
{
    uint8_t hdr[8];
    hdr[0] = MSTP_PREAMBLE1;
    hdr[1] = MSTP_PREAMBLE2;
    hdr[2] = type;
    hdr[3] = dst;
    hdr[4] = src;
    hdr[5] = (len >> 8) & 0xFF;
    hdr[6] = len & 0xFF;

    /* Header CRC (over type, dst, src, len_hi, len_lo) */
    uint8_t hcrc = 0xFF;
    for (int i = 2; i < 7; i++)
        hcrc = mstp_crc8(hcrc, hdr[i]);
    hdr[7] = ~hcrc;

    uart_write_bytes(RS485_UART, hdr, 8);

    if (len > 0 && data) {
        uart_write_bytes(RS485_UART, data, len);
        uint16_t dcrc = mstp_crc16(data, len);
        uint8_t crc_bytes[2] = { dcrc & 0xFF, (dcrc >> 8) & 0xFF };
        uart_write_bytes(RS485_UART, crc_bytes, 2);
    }
    uart_wait_tx_done(RS485_UART, pdMS_TO_TICKS(100));
}

/* ── Receive MS/TP frame from UART ── */
static bool mstp_recv_frame(mstp_frame_t *frame, uint32_t timeout_ms)
{
    frame->valid = false;
    uint8_t b;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    /* Find preamble 0x55 0xFF */
    int state = 0;
    while (esp_timer_get_time() < deadline) {
        if (uart_read_bytes(RS485_UART, &b, 1, pdMS_TO_TICKS(5)) != 1)
            continue;
        if (state == 0 && b == MSTP_PREAMBLE1) { state = 1; continue; }
        if (state == 1 && b == MSTP_PREAMBLE2) { state = 2; break; }
        if (state == 1 && b == MSTP_PREAMBLE1) { continue; }
        state = 0;
    }
    if (state != 2) return false;

    /* Read header: type, dst, src, len_hi, len_lo, hcrc */
    uint8_t hdr[6];
    if (uart_read_bytes(RS485_UART, hdr, 6, pdMS_TO_TICKS(50)) != 6)
        return false;

    /* Verify header CRC */
    uint8_t hcrc = 0xFF;
    for (int i = 0; i < 5; i++)
        hcrc = mstp_crc8(hcrc, hdr[i]);
    if ((uint8_t)~hcrc != hdr[5])
        return false;

    frame->type = hdr[0];
    frame->dst  = hdr[1];
    frame->src  = hdr[2];
    frame->len  = ((uint16_t)hdr[3] << 8) | hdr[4];

    /* Read data + CRC16 if present */
    if (frame->len > 0) {
        if (frame->len > sizeof(frame->data)) return false;
        int total = frame->len + 2;  /* data + 2-byte CRC */
        uint8_t tmp[514];
        if (uart_read_bytes(RS485_UART, tmp, total, pdMS_TO_TICKS(200)) != total)
            return false;
        memcpy(frame->data, tmp, frame->len);
        /* Verify data CRC */
        uint16_t dcrc = mstp_crc16(tmp, frame->len);
        uint16_t rcrc = tmp[frame->len] | ((uint16_t)tmp[frame->len + 1] << 8);
        if (dcrc != rcrc) return false;
    }

    frame->valid = true;
    return true;
}

/* ── BACnet Who-Is / I-Am helpers ── */

/* Build I-Am NPDU+APDU for broadcast */
static uint16_t build_iam(uint8_t *buf, uint32_t device_id,
                           uint16_t vendor_id)
{
    int p = 0;
    /* NPDU — BACnet network layer */
    buf[p++] = 0x01;  /* version */
    buf[p++] = 0x20;  /* control: DNET present */
    buf[p++] = 0xFF;  /* DNET hi (broadcast) */
    buf[p++] = 0xFF;  /* DNET lo */
    buf[p++] = 0x00;  /* DLEN = 0 (broadcast) */
    buf[p++] = 0xFF;  /* hop count */

    /* APDU — Unconfirmed I-Am */
    buf[p++] = 0x10;  /* type=1 (unconfirmed), flags */
    buf[p++] = 0x00;  /* service = I-Am */

    /* I-Am-Request: objectIdentifier, maxAPDU, segmentation, vendorID */
    /* Object Identifier: device,instance */
    buf[p++] = 0xC4;  /* context tag 0, length 4 */
    buf[p++] = 0x02;  /* object type hi (device=8 → 8<<6 = 0x200 → hi=0x02) */
    buf[p++] = 0x00;
    buf[p++] = (device_id >> 8) & 0xFF;
    buf[p++] = device_id & 0xFF;

    /* Max APDU length: 480 */
    buf[p++] = 0x22;  /* context tag 2, length 2 */
    buf[p++] = 0x01;
    buf[p++] = 0xE0;

    /* Segmentation supported: no */
    buf[p++] = 0x91;  /* context tag, length 1 */
    buf[p++] = 0x03;  /* no-segmentation */

    /* Vendor ID */
    buf[p++] = 0x21;  /* context tag, length 1 */
    buf[p++] = vendor_id & 0xFF;

    return p;
}

/* ── Main BACnet task ── */
void bacnet_task(void *arg)
{
    ESP_LOGI(TAG, "BACnet task started");

    while (1) {
        if (gw.mode != 2) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        uint8_t our_mac = gw.bacnet_mac;
        uint8_t max_master = gw.bacnet_max_master;

        ESP_LOGI(TAG, "MS/TP active: MAC=%d MaxMaster=%d Baud=%lu",
                 our_mac, max_master, (unsigned long)gw.baud);

        /* Reconfigure UART for BACnet (typically 9600 for MS/TP) */
        uart_config_t ucfg = {
            .baud_rate  = (int)gw.baud,
            .data_bits  = UART_DATA_8_BITS,
            .parity     = UART_PARITY_DISABLE,
            .stop_bits  = UART_STOP_BITS_1,
            .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        uart_param_config(RS485_UART, &ucfg);
        uart_flush_input(RS485_UART);
        ESP_LOGI(TAG, "UART configured for MS/TP at %lu baud", (unsigned long)gw.baud);

        /* Open UDP socket for BACnet/IP bridge */
        int udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udp_sock < 0) {
            ESP_LOGE(TAG, "UDP socket failed");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        struct sockaddr_in baddr = {
            .sin_family = AF_INET,
            .sin_port = htons(gw.bacnet_port),
            .sin_addr.s_addr = INADDR_ANY,
        };
        if (bind(udp_sock, (struct sockaddr *)&baddr, sizeof(baddr)) < 0) {
            ESP_LOGE(TAG, "UDP bind(%d) failed", gw.bacnet_port);
            close(udp_sock);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 };
        setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        ESP_LOGI(TAG, "BACnet/IP on UDP %d + MS/TP on UART ready", gw.bacnet_port);

        /* ── MS/TP state machine ── */
        bool have_token = false;
        uint8_t poll_station = (our_mac + 1) % (max_master + 1);
        int token_count = 0;
        mstp_frame_t rx;

        while (gw.mode == 2) {
            /* Try to receive a frame */
            if (mstp_recv_frame(&rx, 50)) {
                gw.rx_count++;

                if (rx.type == MSTP_TOKEN && rx.dst == our_mac) {
                    have_token = true;
                    token_count = 0;
                } else if (rx.type == MSTP_POLL_FOR_MASTER && rx.dst == our_mac) {
                    /* Reply to PFM */
                    mstp_send_frame(MSTP_REPLY_PFM, rx.src, our_mac, NULL, 0);
                    gw.tx_count++;
                } else if (rx.type == MSTP_BACNET_DATA_REPLY && rx.dst == our_mac) {
                    /* Forward data to BACnet/IP (bridge) */
                    if (rx.len > 0) {
                        struct sockaddr_in bcast = {
                            .sin_family = AF_INET,
                            .sin_port = htons(gw.bacnet_port),
                            .sin_addr.s_addr = INADDR_BROADCAST,
                        };
                        sendto(udp_sock, rx.data, rx.len, 0,
                               (struct sockaddr *)&bcast, sizeof(bcast));
                    }
                } else if (rx.type == MSTP_BACNET_DATA_NO_REPLY &&
                           (rx.dst == our_mac || rx.dst == 0xFF)) {
                    /* Check for Who-Is in the data */
                    if (rx.len >= 4 && rx.data[0] == 0x01 &&
                        (rx.data[1] & 0x04)) {
                        /* Broadcast — check for Who-Is service (0x08) */
                        /* Respond with I-Am */
                        uint8_t iam[64];
                        uint16_t iam_len = build_iam(iam, BACNET_DEVICE_ID,
                                                     BACNET_VENDOR_ID);
                        mstp_send_frame(MSTP_BACNET_DATA_NO_REPLY,
                                        0xFF, our_mac, iam, iam_len);
                        gw.tx_count++;
                        ESP_LOGI(TAG, "I-Am sent (device %d)", BACNET_DEVICE_ID);
                    }
                }
            }

            /* If we have the token, use it then pass it */
            if (have_token) {
                /* Check for BACnet/IP packets to bridge to MS/TP */
                uint8_t ipbuf[512];
                struct sockaddr_in from;
                socklen_t fl = sizeof(from);
                int n = recvfrom(udp_sock, ipbuf, sizeof(ipbuf), MSG_DONTWAIT,
                                 (struct sockaddr *)&from, &fl);
                if (n > 0) {
                    /* Forward BACnet/IP → MS/TP as broadcast */
                    mstp_send_frame(MSTP_BACNET_DATA_NO_REPLY,
                                    0xFF, our_mac, ipbuf, n);
                    gw.tx_count++;
                }

                /* Pass token to next station */
                uint8_t next = (our_mac + 1) % (max_master + 1);
                if (next == our_mac) next = (next + 1) % (max_master + 1);
                mstp_send_frame(MSTP_TOKEN, next, our_mac, NULL, 0);
                gw.tx_count++;
                have_token = false;
                token_count++;

                /* Periodically poll for new masters */
                if (token_count >= 50) {
                    mstp_send_frame(MSTP_POLL_FOR_MASTER,
                                    poll_station, our_mac, NULL, 0);
                    gw.tx_count++;
                    poll_station = (poll_station + 1) % (max_master + 1);
                    if (poll_station == our_mac)
                        poll_station = (poll_station + 1) % (max_master + 1);
                    token_count = 0;
                }
            }
        }

        close(udp_sock);
        ESP_LOGI(TAG, "BACnet mode deactivated");
    }
}
