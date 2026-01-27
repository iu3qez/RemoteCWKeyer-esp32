/**
 * @file cwnet_socket.h
 * @brief CWNet TCP socket integration for ESP-IDF
 *
 * Combines the cwnet_client state machine with BSD socket operations.
 * Handles connection, reconnection, and data transfer.
 *
 * Usage:
 *   1. cwnet_socket_init() - once at startup
 *   2. cwnet_socket_process() - call periodically from bg_task (100Hz)
 *   3. cwnet_socket_send_key_event() - send CW events
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "cwnet_client.h"

/**
 * @brief Socket connection state
 */
typedef enum {
    CWNET_SOCK_DISABLED = 0,    /**< CWNet disabled in config */
    CWNET_SOCK_DISCONNECTED,    /**< Not connected, will attempt */
    CWNET_SOCK_RESOLVING,       /**< DNS resolution in progress */
    CWNET_SOCK_CONNECTING,      /**< TCP connect in progress */
    CWNET_SOCK_CONNECTED,       /**< TCP connected, protocol handshake */
    CWNET_SOCK_READY,           /**< Fully connected and ready */
    CWNET_SOCK_ERROR            /**< Error state, will retry */
} cwnet_socket_state_t;

/**
 * @brief Initialize CWNet socket layer
 *
 * Reads configuration from NVS (remote family).
 * Does nothing if remote.enabled is false.
 */
void cwnet_socket_init(void);

/**
 * @brief Process CWNet socket
 *
 * Call this periodically (e.g., 100Hz from bg_task).
 * Handles connection, reconnection, sending/receiving.
 */
void cwnet_socket_process(void);

/**
 * @brief Send CW key event
 *
 * @param key_down true for key down, false for key up
 * @return true if sent successfully
 */
bool cwnet_socket_send_key_event(bool key_down);

/**
 * @brief Get current socket state
 */
cwnet_socket_state_t cwnet_socket_get_state(void);

/**
 * @brief Check if CWNet is enabled and ready
 */
bool cwnet_socket_is_ready(void);

/**
 * @brief Get last measured latency
 *
 * @return RTT in ms, or -1 if unknown
 */
int32_t cwnet_socket_get_latency_ms(void);

/**
 * @brief Get state as string (for logging)
 */
const char *cwnet_socket_state_str(cwnet_socket_state_t state);
