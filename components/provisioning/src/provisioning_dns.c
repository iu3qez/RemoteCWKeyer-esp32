/**
 * @file provisioning_dns.c
 * @brief Captive portal DNS server for provisioning mode
 *
 * Responds to all DNS A-type queries with 192.168.4.1 (AP IP).
 * This triggers captive portal detection on phones/laptops.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_log.h"

static const char *TAG = "prov_dns";

#define DNS_PORT       53
#define DNS_MAX_LEN    256
#define AP_IP_ADDR     0x0104A8C0  /* 192.168.4.1 in network byte order */
#define ANS_TTL_SEC    60

#define QR_FLAG        (1 << 7)
#define OPCODE_MASK    0x7800
#define QD_TYPE_A      0x0001

typedef struct __attribute__((__packed__)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

typedef struct __attribute__((__packed__)) {
    uint16_t ptr_offset;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t addr_len;
    uint32_t ip_addr;
} dns_answer_t;

static char *skip_dns_name(char *ptr)
{
    while (*ptr != 0) {
        ptr += (unsigned char)(*ptr) + 1;
    }
    return ptr + 1;
}

static int build_dns_response(char *req, int req_len, char *reply, int reply_max)
{
    if (req_len < (int)sizeof(dns_header_t) || req_len > reply_max) {
        return -1;
    }

    memset(reply, 0, (size_t)reply_max);
    memcpy(reply, req, (size_t)req_len);

    dns_header_t *header = (dns_header_t *)reply;

    if ((header->flags & OPCODE_MASK) != 0) {
        return 0;
    }

    header->flags = (uint16_t)(header->flags | QR_FLAG);

    uint16_t qd_count = ntohs(header->qd_count);
    header->an_count = htons(qd_count);

    int reply_len = req_len + (int)qd_count * (int)sizeof(dns_answer_t);
    if (reply_len > reply_max) {
        return -1;
    }

    char *qd_ptr = reply + sizeof(dns_header_t);
    char *ans_ptr = reply + req_len;

    for (uint16_t i = 0; i < qd_count; i++) {
        char *name_start = qd_ptr;
        qd_ptr = skip_dns_name(qd_ptr);

        uint16_t qtype = ntohs(*(uint16_t *)qd_ptr);
        uint16_t qclass = ntohs(*(uint16_t *)(qd_ptr + 2));
        qd_ptr += 4;

        if (qtype == QD_TYPE_A) {
            dns_answer_t *answer = (dns_answer_t *)ans_ptr;
            answer->ptr_offset = htons((uint16_t)(0xC000U | (uint16_t)(name_start - reply)));
            answer->type = htons(qtype);
            answer->class = htons(qclass);
            answer->ttl = htonl(ANS_TTL_SEC);
            answer->addr_len = htons(4);
            answer->ip_addr = AP_IP_ADDR;
            ans_ptr += sizeof(dns_answer_t);
        }
    }

    return reply_len;
}

static void dns_task(void *arg)
{
    (void)arg;
    char rx_buf[512];
    char reply[DNS_MAX_LEN];

    struct sockaddr_in dest = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
    };

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Socket create failed: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    if (bind(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        ESP_LOGE(TAG, "Socket bind failed: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server listening on port %d", DNS_PORT);

    while (1) {
        struct sockaddr_in source;
        socklen_t socklen = sizeof(source);
        int len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0,
                           (struct sockaddr *)&source, &socklen);
        if (len < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int reply_len = build_dns_response(rx_buf, len, reply, sizeof(reply));
        if (reply_len > 0) {
            sendto(sock, reply, (size_t)reply_len, 0,
                   (struct sockaddr *)&source, sizeof(source));
        }
    }
}

void prov_dns_start(void)
{
    BaseType_t ret = xTaskCreate(dns_task, "prov_dns", 4096, NULL, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS task");
    }
}
