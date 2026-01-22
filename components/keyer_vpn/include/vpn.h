/**
 * @file vpn.h
 * @brief WireGuard VPN tunnel manager
 *
 * Manages WireGuard VPN tunnel:
 * - Waits for WiFi connectivity
 * - Syncs time via NTP (required for WireGuard handshake)
 * - Establishes encrypted tunnel to server
 * - Reports state via atomic for monitoring
 *
 * Runs entirely on Core 1 (best-effort). Zero impact on RT path.
 */

#ifndef KEYER_VPN_H
#define KEYER_VPN_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief VPN connection state
 */
typedef enum {
    VPN_STATE_DISABLED = 0,    /**< VPN disabled in config */
    VPN_STATE_WAITING_WIFI,    /**< Waiting for WiFi connection */
    VPN_STATE_WAITING_TIME,    /**< Syncing time via NTP */
    VPN_STATE_CONNECTING,      /**< WireGuard handshake in progress */
    VPN_STATE_CONNECTED,       /**< Tunnel established */
    VPN_STATE_FAILED,          /**< Connection failed */
} vpn_state_t;

/**
 * @brief VPN configuration
 */
typedef struct {
    bool vpn_enabled;                    /**< VPN master enable */
    char server_endpoint[65];        /**< Server hostname or IP */
    uint16_t server_port;            /**< Server UDP port (default 51820) */
    char server_public_key[49];      /**< Server's WireGuard public key (Base64) */
    char client_private_key[49];     /**< This device's private key (Base64) */
    char client_address[19];         /**< Tunnel IP with CIDR (e.g., 10.0.0.2/24) */
    char allowed_ips[65];            /**< Traffic to route (CIDR) */
    uint16_t persistent_keepalive;   /**< Keepalive interval (0=off, 25 for NAT) */
} vpn_config_app_t;

/**
 * @brief Default VPN configuration
 */
#define VPN_CONFIG_DEFAULT { \
    .vpn_enabled = false, \
    .server_endpoint = "", \
    .server_port = 51820, \
    .server_public_key = "", \
    .client_private_key = "", \
    .client_address = "10.0.0.2/24", \
    .allowed_ips = "0.0.0.0/0", \
    .persistent_keepalive = 25 \
}

/**
 * @brief VPN tunnel statistics
 */
typedef struct {
    uint64_t tx_bytes;          /**< Bytes sent through tunnel */
    uint64_t rx_bytes;          /**< Bytes received through tunnel */
    uint32_t handshakes;        /**< Number of successful handshakes */
    int64_t last_handshake_us;  /**< Timestamp of last handshake (us) */
} vpn_stats_t;

/**
 * @brief Initialize VPN subsystem
 *
 * @param config VPN configuration
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if config is NULL
 */
esp_err_t vpn_app_init(const vpn_config_app_t *config);

/**
 * @brief Start VPN connection
 *
 * Non-blocking. Spawns task on Core 1 that:
 * 1. Waits for WiFi
 * 2. Syncs time via NTP
 * 3. Initiates WireGuard handshake
 *
 * Call vpn_get_state() to monitor progress.
 *
 * @return ESP_OK if started, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t vpn_app_start(void);

/**
 * @brief Stop VPN connection
 *
 * Tears down tunnel and stops VPN task.
 */
void vpn_app_stop(void);

/**
 * @brief Get current VPN state
 *
 * Thread-safe (atomic).
 *
 * @return Current state
 */
vpn_state_t vpn_get_state(void);

/**
 * @brief Check if VPN tunnel is established
 *
 * @return true if connected
 */
bool vpn_is_connected(void);

/**
 * @brief Get tunnel statistics
 *
 * @param stats Output buffer for statistics
 * @return true if stats available (connected or was connected)
 */
bool vpn_get_stats(vpn_stats_t *stats);

/**
 * @brief Get state as human-readable string
 *
 * @param state VPN state
 * @return Static string (never NULL)
 */
const char *vpn_state_str(vpn_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_VPN_H */
