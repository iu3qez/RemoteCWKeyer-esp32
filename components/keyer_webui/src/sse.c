#include "sse.h"
#include "esp_log.h"
#include <string.h>
#include <sys/socket.h>

static const char *TAG = "sse";

typedef struct {
    int fd;
    bool active;
} sse_client_t;

static sse_client_t s_clients[SSE_STREAM_COUNT][SSE_MAX_CLIENTS];

void sse_init(void) {
    memset(s_clients, 0, sizeof(s_clients));
    ESP_LOGI(TAG, "SSE initialized (max %d clients per stream)", SSE_MAX_CLIENTS);
}

esp_err_t sse_client_register(sse_stream_t stream, httpd_req_t *req) {
    if (stream >= SSE_STREAM_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Find empty slot */
    int slot = -1;
    for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
        if (!s_clients[stream][i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        ESP_LOGW(TAG, "No free SSE slots for stream %d", stream);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Too many SSE clients");
        return ESP_ERR_NO_MEM;
    }

    /* Send SSE headers */
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    /* Send initial comment to establish connection */
    const char *init = ": SSE stream connected\n\n";
    size_t init_len = strlen(init);
    httpd_resp_send_chunk(req, init, (ssize_t)init_len);

    /* Get socket fd for later sends */
    int fd = httpd_req_to_sockfd(req);
    s_clients[stream][slot].fd = fd;
    s_clients[stream][slot].active = true;

    ESP_LOGI(TAG, "SSE client registered: stream=%d slot=%d fd=%d", stream, slot, fd);

    /* Don't close connection - return without sending final chunk */
    return ESP_OK;
}

void sse_broadcast(sse_stream_t stream, const char *event_type, const char *json_data) {
    if (stream >= SSE_STREAM_COUNT) {
        return;
    }

    char buf[256];
    int len = snprintf(buf, sizeof(buf), "event: %s\ndata: %s\n\n", event_type, json_data);
    if (len >= (int)sizeof(buf)) {
        ESP_LOGW(TAG, "SSE event truncated");
        len = (int)sizeof(buf) - 1;
    }

    for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
        if (s_clients[stream][i].active) {
            int fd = s_clients[stream][i].fd;
            int sent = send(fd, buf, (size_t)len, MSG_DONTWAIT);
            if (sent < 0) {
                ESP_LOGD(TAG, "SSE client disconnected: stream=%d slot=%d", stream, i);
                s_clients[stream][i].active = false;
            }
        }
    }
}

void sse_send_keepalive(void) {
    const char *keepalive = ": keepalive\n\n";
    size_t len = strlen(keepalive);

    for (int stream = 0; stream < SSE_STREAM_COUNT; stream++) {
        for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
            if (s_clients[stream][i].active) {
                int fd = s_clients[stream][i].fd;
                int sent = send(fd, keepalive, len, MSG_DONTWAIT);
                if (sent < 0) {
                    s_clients[stream][i].active = false;
                }
            }
        }
    }
}

int sse_get_client_count(sse_stream_t stream) {
    if (stream >= SSE_STREAM_COUNT) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
        if (s_clients[stream][i].active) {
            count++;
        }
    }
    return count;
}
