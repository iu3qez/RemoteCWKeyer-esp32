#include "sse.h"
#include "esp_log.h"
#include <string.h>
#include <sys/socket.h>
#include <errno.h>

static const char *TAG = "sse";

typedef struct {
    int fd;
    bool active;
    sse_stream_t stream;
    int slot;
} sse_client_t;

static sse_client_t s_clients[SSE_STREAM_COUNT][SSE_MAX_CLIENTS];
static httpd_handle_t s_httpd_handle = NULL;

/**
 * @brief Session close callback - cleans up SSE client when connection closes
 *
 * Note: httpd_free_ctx_fn_t signature is void (*)(void *), no sockfd param.
 * The client struct contains the fd for identification.
 */
static void sse_session_close_cb(void *ctx) {
    if (ctx == NULL) {
        return;
    }

    sse_client_t *client = (sse_client_t *)ctx;
    if (client->active) {
        ESP_LOGI(TAG, "SSE session closed: stream=%d slot=%d fd=%d",
                 client->stream, client->slot, client->fd);
        client->active = false;
        client->fd = -1;
    }
}

void sse_init(void) {
    memset(s_clients, 0, sizeof(s_clients));
    for (int s = 0; s < SSE_STREAM_COUNT; s++) {
        for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
            s_clients[s][i].fd = -1;
            s_clients[s][i].stream = (sse_stream_t)s;
            s_clients[s][i].slot = i;
        }
    }
    ESP_LOGI(TAG, "SSE initialized (max %d clients per stream)", SSE_MAX_CLIENTS);
}

void sse_set_httpd_handle(httpd_handle_t handle) {
    s_httpd_handle = handle;
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

    /* Get socket fd */
    int fd = httpd_req_to_sockfd(req);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to get socket fd");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Socket error");
        return ESP_FAIL;
    }

    /* Start async handling - this prevents httpd from closing the connection */
    httpd_req_t *async_req = NULL;
    esp_err_t ret = httpd_req_async_handler_begin(req, &async_req);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start async handler: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Async error");
        return ESP_FAIL;
    }

    /* Register session context for cleanup on disconnect */
    sse_client_t *client = &s_clients[stream][slot];
    client->fd = fd;
    client->active = true;

    /* Set session context with close callback */
    httpd_sess_set_ctx(req->handle, fd, client, sse_session_close_cb);

    /* Send SSE headers using httpd API */
    httpd_resp_set_type(async_req, "text/event-stream");
    httpd_resp_set_hdr(async_req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(async_req, "Connection", "keep-alive");
    httpd_resp_set_hdr(async_req, "Access-Control-Allow-Origin", "*");

    /* Send initial SSE comment - use chunked encoding to keep connection open */
    const char *init = ": SSE stream connected\n\n";
    httpd_resp_send_chunk(async_req, init, (ssize_t)strlen(init));

    ESP_LOGI(TAG, "SSE client registered: stream=%d slot=%d fd=%d", stream, slot, fd);

    /* NOTE: We intentionally do NOT call httpd_req_async_handler_complete()
     * This keeps the connection open for SSE streaming.
     * The connection will be cleaned up when the client disconnects
     * (detected by send() failure in sse_broadcast). */

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
