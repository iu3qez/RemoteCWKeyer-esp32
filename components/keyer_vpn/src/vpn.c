/**
 * @file vpn.c
 * @brief WireGuard VPN tunnel manager
 *
 * Implements WireGuard VPN tunnel:
 * - Waits for WiFi connectivity
 * - Syncs time via NTP (required for WireGuard handshake)
 * - Establishes encrypted tunnel to server
 * - Reports state via atomic for monitoring
 *
 * Runs entirely on Core 1 (best-effort). Zero impact on RT path.
 */

#include "vpn.h"
#include "wifi.h"
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "esp_wireguard.h"

#define TAG "vpn"

/* NTP timeout in milliseconds */
#define NTP_TIMEOUT_MS      15000

/* Task stack size (WireGuard crypto needs room) */
#define VPN_TASK_STACK_SIZE 6144

/* Keepalive check interval */
#define MONITOR_INTERVAL_MS 5000

/* VPN state */
static struct {
    bool initialized;
    vpn_config_app_t config;
    wireguard_config_t wg_config;
    wireguard_ctx_t wg_ctx;
    TaskHandle_t task_handle;
    _Atomic vpn_state_t state;
    vpn_stats_t stats;
    bool time_synced;
} s_vpn;

/* Forward declarations */
static void vpn_task(void *pvParameters);
static bool wait_for_time_sync(uint32_t timeout_ms);
static bool wait_for_wifi(uint32_t check_interval_ms);
static esp_err_t setup_wireguard(void);

/* State names for logging */
static const char *s_state_names[] = {
    [VPN_STATE_DISABLED]     = "DISABLED",
    [VPN_STATE_WAITING_WIFI] = "WAITING_WIFI",
    [VPN_STATE_WAITING_TIME] = "WAITING_TIME",
    [VPN_STATE_CONNECTING]   = "CONNECTING",
    [VPN_STATE_CONNECTED]    = "CONNECTED",
    [VPN_STATE_FAILED]       = "FAILED",
};

esp_err_t vpn_app_init(const vpn_config_app_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_vpn.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    /* Save configuration */
    memcpy(&s_vpn.config, config, sizeof(vpn_config_app_t));

    /* Initialize state */
    atomic_store_explicit(&s_vpn.state, VPN_STATE_DISABLED, memory_order_relaxed);
    memset(&s_vpn.stats, 0, sizeof(vpn_stats_t));
    s_vpn.task_handle = NULL;
    s_vpn.time_synced = false;
    s_vpn.initialized = true;

    ESP_LOGI(TAG, "VPN initialized");
    return ESP_OK;
}

esp_err_t vpn_app_start(void)
{
    if (!s_vpn.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Check if already running */
    if (s_vpn.task_handle != NULL) {
        ESP_LOGW(TAG, "VPN task already running");
        return ESP_OK;
    }

    /* Check if VPN is enabled */
    if (!s_vpn.config.vpn_enabled) {
        ESP_LOGI(TAG, "VPN disabled in config");
        atomic_store_explicit(&s_vpn.state, VPN_STATE_DISABLED, memory_order_relaxed);
        return ESP_OK;
    }

    /* Validate configuration */
    if (strlen(s_vpn.config.server_endpoint) == 0) {
        ESP_LOGE(TAG, "No server endpoint configured");
        atomic_store_explicit(&s_vpn.state, VPN_STATE_FAILED, memory_order_relaxed);
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(s_vpn.config.server_public_key) == 0) {
        ESP_LOGE(TAG, "No server public key configured");
        atomic_store_explicit(&s_vpn.state, VPN_STATE_FAILED, memory_order_relaxed);
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(s_vpn.config.client_private_key) == 0) {
        ESP_LOGE(TAG, "No client private key configured");
        atomic_store_explicit(&s_vpn.state, VPN_STATE_FAILED, memory_order_relaxed);
        return ESP_ERR_INVALID_ARG;
    }

    /* Create VPN task on Core 1 */
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        vpn_task,
        "vpn_task",
        VPN_TASK_STACK_SIZE,
        NULL,
        5,                  /* Same priority as WiFi task */
        &s_vpn.task_handle,
        1                   /* Core 1 only - keep RT path on Core 0 clean */
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create VPN task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "VPN start initiated");
    return ESP_OK;
}

void vpn_app_stop(void)
{
    if (!s_vpn.initialized) {
        return;
    }

    /* Disconnect WireGuard */
    vpn_state_t current_state = atomic_load_explicit(&s_vpn.state, memory_order_relaxed);
    if (current_state == VPN_STATE_CONNECTED || current_state == VPN_STATE_CONNECTING) {
        ESP_LOGI(TAG, "Disconnecting WireGuard tunnel");
        esp_wireguard_disconnect(&s_vpn.wg_ctx);
    }

    /* Stop task if running */
    if (s_vpn.task_handle != NULL) {
        /* Signal task to stop by setting state */
        atomic_store_explicit(&s_vpn.state, VPN_STATE_DISABLED, memory_order_release);

        /* Give task time to notice and exit */
        vTaskDelay(pdMS_TO_TICKS(100));

        /* If task still running, delete it */
        if (eTaskGetState(s_vpn.task_handle) != eDeleted) {
            vTaskDelete(s_vpn.task_handle);
        }
        s_vpn.task_handle = NULL;
    }

    atomic_store_explicit(&s_vpn.state, VPN_STATE_DISABLED, memory_order_relaxed);
    ESP_LOGI(TAG, "VPN stopped");
}

vpn_state_t vpn_get_state(void)
{
    return atomic_load_explicit(&s_vpn.state, memory_order_relaxed);
}

bool vpn_is_connected(void)
{
    return (vpn_get_state() == VPN_STATE_CONNECTED);
}

bool vpn_get_stats(vpn_stats_t *stats)
{
    if (stats == NULL) {
        return false;
    }

    memcpy(stats, &s_vpn.stats, sizeof(vpn_stats_t));
    return true;
}

const char *vpn_state_str(vpn_state_t state)
{
    if (state < sizeof(s_state_names) / sizeof(s_state_names[0])) {
        return s_state_names[state];
    }
    return "UNKNOWN";
}

/* Wait for WiFi connection */
static bool wait_for_wifi(uint32_t check_interval_ms)
{
    ESP_LOGI(TAG, "Waiting for WiFi...");
    atomic_store_explicit(&s_vpn.state, VPN_STATE_WAITING_WIFI, memory_order_release);

    while (!wifi_is_connected()) {
        /* Check if we should stop */
        if (atomic_load_explicit(&s_vpn.state, memory_order_acquire) == VPN_STATE_DISABLED) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
    }

    ESP_LOGI(TAG, "WiFi connected");
    return true;
}

/* NTP time sync callback */
static void time_sync_notification_cb(struct timeval *tv)
{
    (void)tv;
    ESP_LOGI(TAG, "NTP time synchronized");
    s_vpn.time_synced = true;
}

/* Wait for NTP time sync */
static bool wait_for_time_sync(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Starting NTP time sync...");
    atomic_store_explicit(&s_vpn.state, VPN_STATE_WAITING_TIME, memory_order_release);

    /* Check if time is already valid (from previous sync or RTC) */
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year >= (2020 - 1900)) {
        ESP_LOGI(TAG, "System time already valid: %d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return true;
    }

    /* Initialize SNTP */
    s_vpn.time_synced = false;

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    /* Wait for time sync or timeout */
    uint32_t elapsed = 0;
    const uint32_t poll_interval = 100;

    while (elapsed < timeout_ms) {
        /* Check if we should stop */
        if (atomic_load_explicit(&s_vpn.state, memory_order_acquire) == VPN_STATE_DISABLED) {
            esp_sntp_stop();
            return false;
        }

        /* Check if callback fired */
        if (s_vpn.time_synced) {
            break;
        }

        /* Check sync status */
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(poll_interval));
        elapsed += poll_interval;
    }

    esp_sntp_stop();

    /* Verify time is now valid */
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year >= (2020 - 1900)) {
        ESP_LOGI(TAG, "Time synced: %d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return true;
    }

    ESP_LOGW(TAG, "Time sync failed after %lu ms", (unsigned long)elapsed);
    return false;
}

/* Setup WireGuard configuration */
static esp_err_t setup_wireguard(void)
{
    memset(&s_vpn.wg_config, 0, sizeof(wireguard_config_t));

    /* Parse client address for IP and netmask */
    char ip_str[16] = {0};
    char mask_str[16] = "255.255.255.0";  /* Default /24 */

    /* Find CIDR separator */
    const char *slash = strchr(s_vpn.config.client_address, '/');
    if (slash != NULL) {
        size_t ip_len = (size_t)(slash - s_vpn.config.client_address);
        if (ip_len >= sizeof(ip_str)) {
            ip_len = sizeof(ip_str) - 1;
        }
        memcpy(ip_str, s_vpn.config.client_address, ip_len);
        ip_str[ip_len] = '\0';

        /* Convert CIDR to netmask */
        int cidr = atoi(slash + 1);
        if (cidr >= 0 && cidr <= 32) {
            uint32_t mask_val = (cidr == 0) ? 0 : (0xFFFFFFFFU << (32 - cidr));
            snprintf(mask_str, sizeof(mask_str), "%u.%u.%u.%u",
                     (mask_val >> 24) & 0xFF,
                     (mask_val >> 16) & 0xFF,
                     (mask_val >> 8) & 0xFF,
                     mask_val & 0xFF);
        }
    } else {
        strncpy(ip_str, s_vpn.config.client_address, sizeof(ip_str) - 1);
    }

    ESP_LOGI(TAG, "Client IP: %s, Netmask: %s", ip_str, mask_str);

    /* Configure WireGuard */
    s_vpn.wg_config.private_key = s_vpn.config.client_private_key;
    s_vpn.wg_config.listen_port = 0;  /* Random local port */
    s_vpn.wg_config.public_key = s_vpn.config.server_public_key;
    s_vpn.wg_config.allowed_ip = ip_str;
    s_vpn.wg_config.allowed_ip_mask = mask_str;
    s_vpn.wg_config.endpoint = s_vpn.config.server_endpoint;
    s_vpn.wg_config.port = s_vpn.config.server_port;
    s_vpn.wg_config.persistent_keepalive = s_vpn.config.persistent_keepalive;

    ESP_LOGI(TAG, "WireGuard config: endpoint=%s:%u, keepalive=%u",
             s_vpn.wg_config.endpoint,
             s_vpn.wg_config.port,
             s_vpn.wg_config.persistent_keepalive);

    /* Initialize WireGuard */
    esp_err_t ret = esp_wireguard_init(&s_vpn.wg_config, &s_vpn.wg_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wireguard_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

/* VPN task */
static void vpn_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "VPN task started on Core %d", xPortGetCoreID());

    /* Step 1: Wait for WiFi */
    if (!wait_for_wifi(500)) {
        ESP_LOGW(TAG, "VPN task stopping (WiFi wait aborted)");
        s_vpn.task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Step 2: Sync time via NTP */
    if (!wait_for_time_sync(NTP_TIMEOUT_MS)) {
        ESP_LOGE(TAG, "NTP time sync failed - WireGuard requires valid time");
        atomic_store_explicit(&s_vpn.state, VPN_STATE_FAILED, memory_order_release);
        s_vpn.task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Step 3: Setup WireGuard */
    esp_err_t ret = setup_wireguard();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WireGuard setup failed");
        atomic_store_explicit(&s_vpn.state, VPN_STATE_FAILED, memory_order_release);
        s_vpn.task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Step 4: Connect */
    ESP_LOGI(TAG, "Connecting to WireGuard server...");
    atomic_store_explicit(&s_vpn.state, VPN_STATE_CONNECTING, memory_order_release);

    ret = esp_wireguard_connect(&s_vpn.wg_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wireguard_connect failed: %s", esp_err_to_name(ret));
        atomic_store_explicit(&s_vpn.state, VPN_STATE_FAILED, memory_order_release);
        s_vpn.task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* Connected! */
    atomic_store_explicit(&s_vpn.state, VPN_STATE_CONNECTED, memory_order_release);
    s_vpn.stats.handshakes++;
    s_vpn.stats.last_handshake_us = esp_timer_get_time();
    ESP_LOGI(TAG, "VPN tunnel established");

    /* Step 5: Monitor loop */
    while (atomic_load_explicit(&s_vpn.state, memory_order_acquire) != VPN_STATE_DISABLED) {
        /* Check if peer is still up */
        ret = esp_wireguardif_peer_is_up(&s_vpn.wg_ctx);
        if (ret != ESP_OK) {
            /* Tunnel down - try to reconnect */
            ESP_LOGW(TAG, "VPN tunnel down, reconnecting...");
            atomic_store_explicit(&s_vpn.state, VPN_STATE_CONNECTING, memory_order_release);

            /* Re-check WiFi */
            if (!wifi_is_connected()) {
                ESP_LOGW(TAG, "WiFi disconnected, waiting...");
                if (!wait_for_wifi(500)) {
                    break;  /* Stop requested */
                }
            }

            /* Reconnect */
            ret = esp_wireguard_connect(&s_vpn.wg_ctx);
            if (ret == ESP_OK) {
                atomic_store_explicit(&s_vpn.state, VPN_STATE_CONNECTED, memory_order_release);
                s_vpn.stats.handshakes++;
                s_vpn.stats.last_handshake_us = esp_timer_get_time();
                ESP_LOGI(TAG, "VPN tunnel re-established");
            } else {
                ESP_LOGE(TAG, "Reconnect failed: %s", esp_err_to_name(ret));
                atomic_store_explicit(&s_vpn.state, VPN_STATE_FAILED, memory_order_release);
                vTaskDelay(pdMS_TO_TICKS(5000));  /* Back off before retry */
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }

    /* Cleanup */
    ESP_LOGI(TAG, "VPN task stopping");
    esp_wireguard_disconnect(&s_vpn.wg_ctx);
    s_vpn.task_handle = NULL;
    vTaskDelete(NULL);
}
