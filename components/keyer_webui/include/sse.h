#ifndef KEYER_WEBUI_SSE_H
#define KEYER_WEBUI_SSE_H

#include "esp_http_server.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSE_MAX_CLIENTS 4

typedef enum {
    SSE_STREAM_TIMELINE,
    SSE_STREAM_DECODER,
    SSE_STREAM_COUNT
} sse_stream_t;

/**
 * @brief Initialize SSE subsystem
 */
void sse_init(void);

/**
 * @brief Set HTTP server handle for session management
 * @param handle HTTP server handle from httpd_start()
 */
void sse_set_httpd_handle(httpd_handle_t handle);

/**
 * @brief Register new SSE client
 * @param stream Which stream to subscribe to
 * @param req HTTP request (kept open for streaming)
 * @return ESP_OK on success
 */
esp_err_t sse_client_register(sse_stream_t stream, httpd_req_t *req);

/**
 * @brief Send event to all clients on a stream
 * @param stream Target stream
 * @param event_type Event type (e.g., "char", "paddle")
 * @param json_data JSON payload
 */
void sse_broadcast(sse_stream_t stream, const char *event_type, const char *json_data);

/**
 * @brief Send keepalive to all clients
 */
void sse_send_keepalive(void);

/**
 * @brief Get connected client count for a stream
 */
int sse_get_client_count(sse_stream_t stream);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_WEBUI_SSE_H */
