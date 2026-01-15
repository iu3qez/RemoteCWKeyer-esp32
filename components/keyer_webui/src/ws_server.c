/**
 * @file ws_server.c
 * @brief WebSocket server implementation
 *
 * Handles WebSocket connections and broadcasts events to all clients.
 * Uses httpd_queue_work() for async sending from bg_task context.
 */

#include "ws_server.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ws_server";

/**
 * @brief WebSocket client tracking
 */
typedef struct {
    int fd;      /**< Socket file descriptor (-1 if unused) */
    bool active; /**< Connection active flag */
} ws_client_t;

static ws_client_t s_clients[WS_MAX_CLIENTS];
static httpd_handle_t s_httpd_handle = NULL;

/**
 * @brief Async message structure for httpd_queue_work
 */
typedef struct {
    httpd_handle_t hd;
    int fd;
    char message[256];
} ws_async_msg_t;

/* Pre-allocated message pool to avoid malloc in hot path */
static ws_async_msg_t s_msg_pool[WS_MAX_CLIENTS];
static uint8_t s_msg_pool_idx = 0;

/**
 * @brief Register a new WebSocket client
 */
static void ws_client_register(int fd) {
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (!s_clients[i].active) {
            s_clients[i].fd = fd;
            s_clients[i].active = true;
            ESP_LOGI(TAG, "Client registered: slot=%d fd=%d", i, fd);
            return;
        }
    }
    ESP_LOGW(TAG, "No free slots for client fd=%d", fd);
}

/**
 * @brief Unregister a WebSocket client
 */
static void ws_client_unregister(int fd) {
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            s_clients[i].active = false;
            s_clients[i].fd = -1;
            ESP_LOGI(TAG, "Client unregistered: slot=%d fd=%d", i, fd);
            return;
        }
    }
}

/**
 * @brief Check if a client is already registered
 */
static bool ws_client_is_registered(int fd) {
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Async send worker (runs in httpd context)
 */
static void ws_async_send_worker(void *arg) {
    ws_async_msg_t *msg = (ws_async_msg_t *)arg;

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)msg->message;
    ws_pkt.len = strlen(msg->message);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.final = true;

    esp_err_t ret = httpd_ws_send_frame_async(msg->hd, msg->fd, &ws_pkt);
    if (ret != ESP_OK) {
        /* Client disconnected - mark inactive */
        ESP_LOGD(TAG, "Send failed fd=%d: %s", msg->fd, esp_err_to_name(ret));
        ws_client_unregister(msg->fd);
    }
}

void ws_server_init(void) {
    memset(s_clients, 0, sizeof(s_clients));
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        s_clients[i].fd = -1;
    }
    ESP_LOGI(TAG, "WebSocket server initialized (max %d clients)", WS_MAX_CLIENTS);
}

void ws_server_set_httpd_handle(httpd_handle_t handle) {
    s_httpd_handle = handle;
}

esp_err_t ws_handler(httpd_req_t *req) {
    /* First call with GET method: WebSocket handshake */
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WebSocket handshake fd=%d", fd);
        /* Register client after handshake completes */
        ws_client_register(fd);
        return ESP_OK;
    }

    /* Receive frame */
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    /* First call: get frame info (len=0 just gets size) */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %s", esp_err_to_name(ret));
        return ret;
    }

    int fd = httpd_req_to_sockfd(req);

    /* Handle control frames */
    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "Close frame from fd=%d", fd);
        ws_client_unregister(fd);
        return ESP_OK;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_PING) {
        /* Respond with pong */
        ws_pkt.type = HTTPD_WS_TYPE_PONG;
        return httpd_ws_send_frame(req, &ws_pkt);
    }

    /* Handle text frames (client commands) */
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_pkt.len > 0) {
        uint8_t *buf = malloc(ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "malloc failed for %zu bytes", ws_pkt.len);
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret == ESP_OK) {
            buf[ws_pkt.len] = '\0';
            ESP_LOGD(TAG, "Received from fd=%d: %s", fd, buf);
            /* Future: parse JSON commands here */
        }
        free(buf);
    }

    return ESP_OK;
}

void ws_broadcast(const char *message) {
    if (s_httpd_handle == NULL) {
        return;
    }

    size_t len = strlen(message);
    if (len >= sizeof(s_msg_pool[0].message)) {
        ESP_LOGW(TAG, "Message too long: %zu bytes", len);
        return;
    }

    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd >= 0) {
            /* Get next pool slot (round-robin) */
            uint8_t pool_idx = s_msg_pool_idx;
            s_msg_pool_idx = (uint8_t)((s_msg_pool_idx + 1) % WS_MAX_CLIENTS);

            ws_async_msg_t *msg = &s_msg_pool[pool_idx];
            msg->hd = s_httpd_handle;
            msg->fd = s_clients[i].fd;
            strncpy(msg->message, message, sizeof(msg->message) - 1);
            msg->message[sizeof(msg->message) - 1] = '\0';

            esp_err_t ret = httpd_queue_work(s_httpd_handle, ws_async_send_worker, msg);
            if (ret != ESP_OK) {
                ESP_LOGD(TAG, "Failed to queue work for fd=%d", s_clients[i].fd);
            }
        }
    }
}

void ws_broadcast_decoder_char(char c, uint8_t wpm) {
    char json[64];

    /* Handle special characters that need JSON escaping */
    if (c == '"') {
        snprintf(json, sizeof(json), "{\"type\":\"decoded\",\"char\":\"\\\"\",\"wpm\":%u}", wpm);
    } else if (c == '\\') {
        snprintf(json, sizeof(json), "{\"type\":\"decoded\",\"char\":\"\\\\\",\"wpm\":%u}", wpm);
    } else {
        snprintf(json, sizeof(json), "{\"type\":\"decoded\",\"char\":\"%c\",\"wpm\":%u}", c, wpm);
    }

    ws_broadcast(json);
}

void ws_broadcast_decoder_word(void) {
    ws_broadcast("{\"type\":\"word\"}");
}

void ws_broadcast_timeline(const char *event_type, const char *json_data) {
    char json[256];

    /* Merge event_type into existing JSON object
     * Input:  event_type="paddle", json_data="{\"ts\":123,\"paddle\":0}"
     * Output: "{\"type\":\"paddle\",\"ts\":123,\"paddle\":0}"
     */
    if (json_data != NULL && json_data[0] == '{' && strlen(json_data) > 2) {
        /* Insert type field after opening brace */
        snprintf(json, sizeof(json), "{\"type\":\"%s\",%s",
                 event_type, json_data + 1);
    } else {
        /* Wrap data as-is */
        snprintf(json, sizeof(json), "{\"type\":\"%s\",\"data\":%s}",
                 event_type, json_data ? json_data : "null");
    }

    ws_broadcast(json);
}

int ws_get_client_count(void) {
    int count = 0;
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (s_clients[i].active) {
            count++;
        }
    }
    return count;
}
