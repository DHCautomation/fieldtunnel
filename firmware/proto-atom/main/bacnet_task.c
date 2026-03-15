#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include "gateway.h"

static const char *TAG = "bacnet";

void bacnet_task(void *arg)
{
    ESP_LOGI(TAG, "BACnet task started (stub — MS/TP stack pending)");

    while (1) {
        /* Sleep until BACnet mode is selected */
        if (gw.mode != 2) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(TAG, "BACnet MS/TP mode active");
        ESP_LOGI(TAG, "MAC=%d MaxMaster=%d Port=%d Baud=%lu",
                 gw.bacnet_mac, gw.bacnet_max_master,
                 gw.bacnet_port, (unsigned long)gw.baud);

        /* Open UDP socket for BACnet/IP */
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) {
            ESP_LOGE(TAG, "socket() failed");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(gw.bacnet_port),
            .sin_addr.s_addr = INADDR_ANY,
        };

        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            ESP_LOGE(TAG, "bind(%d) failed", gw.bacnet_port);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        ESP_LOGI(TAG, "BACnet/IP listening on UDP %d", gw.bacnet_port);
        ESP_LOGI(TAG, "MS/TP token passing: stub — full stack coming soon");

        struct timeval tv = { .tv_sec = 1 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        uint8_t buf[512];
        struct sockaddr_in client;
        socklen_t cl = sizeof(client);

        while (gw.mode == 2) {
            int n = recvfrom(sock, buf, sizeof(buf), 0,
                             (struct sockaddr *)&client, &cl);
            if (n > 0) {
                ESP_LOGI(TAG, "BACnet/IP rx %d bytes", n);
                gw.rx_count++;

                /* Stub: echo back a simple BACnet Reject for now */
                /* Full MS/TP bridge will translate to RS485 frames */
            }
        }

        close(sock);
        ESP_LOGI(TAG, "BACnet mode deactivated");
    }
}
