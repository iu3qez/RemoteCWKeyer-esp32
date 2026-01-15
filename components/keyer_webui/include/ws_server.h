/**
 * @file ws_server.h
 * @brief WebSocket server for real-time streaming
 *
 * Provides a single /ws endpoint for all real-time data:
 * - Decoder events (decoded chars, word separators)
 * - Timeline events (paddle, keying, gaps)
 *
 * Replaces SSE implementation for better connection management.
 */

#ifndef KEYER_WEBUI_WS_SERVER_H
#define KEYER_WEBUI_WS_SERVER_H

#include "esp_http_server.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WS_MAX_CLIENTS 8

/**
 * @brief Initialize WebSocket server subsystem
 */
void ws_server_init(void);

/**
 * @brief Set HTTP server handle for WebSocket management
 * @param handle HTTP server handle from httpd_start()
 */
void ws_server_set_httpd_handle(httpd_handle_t handle);

/**
 * @brief WebSocket URI handler (called by httpd)
 * @param req HTTP request
 * @return ESP_OK on success
 */
esp_err_t ws_handler(httpd_req_t *req);

/**
 * @brief Broadcast raw JSON message to all connected WebSocket clients
 * @param message JSON message string (null-terminated)
 */
void ws_broadcast(const char *message);

/**
 * @brief Broadcast decoder character event
 * @param c Decoded character
 * @param wpm Current WPM
 */
void ws_broadcast_decoder_char(char c, uint8_t wpm);

/**
 * @brief Broadcast decoder word separator event
 */
void ws_broadcast_decoder_word(void);

/**
 * @brief Broadcast decoder pattern update
 * @param pattern Current pattern string (e.g., ".-")
 */
void ws_broadcast_decoder_pattern(const char *pattern);

/**
 * @brief Broadcast timeline event
 * @param event_type Event type string (paddle, keying, decoded, gap)
 * @param json_data JSON payload (will be merged with type)
 */
void ws_broadcast_timeline(const char *event_type, const char *json_data);

/**
 * @brief Get connected WebSocket client count
 * @return Number of active clients
 */
int ws_get_client_count(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_WEBUI_WS_SERVER_H */
