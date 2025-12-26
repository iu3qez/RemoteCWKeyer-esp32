/**
 * @file wifi.h
 * @brief WiFi connectivity manager
 *
 * Manages WiFi STA connection with AP fallback:
 * - Attempts STA connection on startup
 * - Falls back to open AP if connection fails
 * - Reports state via atomic for LED integration
 */

#ifndef KEYER_WIFI_H
#define KEYER_WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi state
 */
typedef enum {
    WIFI_STATE_DISABLED = 0,   /**< WiFi disabled */
    WIFI_STATE_CONNECTING,     /**< Attempting STA connection */
    WIFI_STATE_CONNECTED,      /**< STA connected */
    WIFI_STATE_FAILED,         /**< Connection failed (brief state) */
    WIFI_STATE_AP_MODE,        /**< Running as access point */
} wifi_state_t;

/**
 * @brief WiFi configuration
 */
typedef struct {
    bool enabled;              /**< WiFi master enable */
    char ssid[33];             /**< Network SSID (32 + null) */
    char password[65];         /**< Network password (64 + null) */
    uint16_t timeout_sec;      /**< Connection timeout */
    bool use_static_ip;        /**< Use static IP vs DHCP */
    char ip_address[16];       /**< Static IP */
    char netmask[16];          /**< Subnet mask */
    char gateway[16];          /**< Gateway */
    char dns[16];              /**< DNS server */
} wifi_config_app_t;

/**
 * @brief Default WiFi configuration
 */
#define WIFI_CONFIG_DEFAULT { \
    .enabled = false, \
    .ssid = "", \
    .password = "", \
    .timeout_sec = 30, \
    .use_static_ip = false, \
    .ip_address = "0.0.0.0", \
    .netmask = "255.255.255.0", \
    .gateway = "0.0.0.0", \
    .dns = "0.0.0.0" \
}

/**
 * @brief Initialize WiFi subsystem
 *
 * @param config WiFi configuration
 * @return ESP_OK on success
 */
esp_err_t wifi_app_init(const wifi_config_app_t *config);

/**
 * @brief Start WiFi connection
 *
 * Non-blocking. Call wifi_get_state() to monitor progress.
 *
 * @return ESP_OK if started
 */
esp_err_t wifi_app_start(void);

/**
 * @brief Stop WiFi
 */
void wifi_app_stop(void);

/**
 * @brief Get current WiFi state
 *
 * Thread-safe (atomic).
 *
 * @return Current state
 */
wifi_state_t wifi_get_state(void);

/**
 * @brief Get current IP address
 *
 * @param buf Buffer for IP string (at least 16 bytes)
 * @param len Buffer length
 * @return true if connected and IP available
 */
bool wifi_get_ip(char *buf, size_t len);

/**
 * @brief Check if WiFi is connected
 *
 * @return true if STA connected
 */
bool wifi_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_WIFI_H */
