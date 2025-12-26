/**
 * @file wifi.c
 * @brief WiFi connectivity manager
 *
 * Implements WiFi STA connection with AP fallback:
 * - Attempts STA connection with retry logic
 * - Falls back to open AP if connection fails
 * - Reports state via atomic for LED integration
 */

#include "wifi.h"
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_log.h"

#define TAG "wifi"

/* Hardcoded AP settings */
#define AP_SSID_PREFIX   "CWKeyer-"
#define AP_MAX_CONN      4
#define AP_CHANNEL       1

/* Event group bits */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

/* Retry configuration */
#define MAX_RETRY_COUNT     3
#define FAILED_STATE_DELAY_MS 600

/* WiFi state */
static struct {
    bool initialized;
    wifi_config_app_t config;
    esp_netif_t *sta_netif;
    esp_netif_t *ap_netif;
    EventGroupHandle_t event_group;
    _Atomic wifi_state_t state;
    int retry_count;
    esp_ip4_addr_t ip_addr;
} s_wifi;

/* Forward declarations */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data);
static void connection_task(void *pvParameters);
static esp_err_t start_ap_mode(void);
static esp_err_t configure_static_ip(void);

esp_err_t wifi_app_init(const wifi_config_app_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_wifi.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    /* Save configuration */
    memcpy(&s_wifi.config, config, sizeof(wifi_config_app_t));

    /* Initialize TCP/IP stack */
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init netif: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Create default event loop */
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Create event group */
    s_wifi.event_group = xEventGroupCreate();
    if (s_wifi.event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    /* Initialize WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi: %s", esp_err_to_name(ret));
        vEventGroupDelete(s_wifi.event_group);
        return ret;
    }

    /* Register event handlers */
    ret = esp_event_handler_instance_register(WIFI_EVENT,
                                                ESP_EVENT_ANY_ID,
                                                &wifi_event_handler,
                                                NULL,
                                                NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        vEventGroupDelete(s_wifi.event_group);
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT,
                                                IP_EVENT_STA_GOT_IP,
                                                &wifi_event_handler,
                                                NULL,
                                                NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        esp_wifi_deinit();
        vEventGroupDelete(s_wifi.event_group);
        return ret;
    }

    /* Set initial state */
    atomic_store_explicit(&s_wifi.state, WIFI_STATE_DISABLED, memory_order_relaxed);
    s_wifi.retry_count = 0;
    s_wifi.ip_addr.addr = 0;
    s_wifi.initialized = true;

    ESP_LOGI(TAG, "WiFi initialized");
    return ESP_OK;
}

esp_err_t wifi_app_start(void)
{
    if (!s_wifi.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Check if WiFi is enabled */
    if (!s_wifi.config.enabled) {
        ESP_LOGI(TAG, "WiFi disabled in config");
        atomic_store_explicit(&s_wifi.state, WIFI_STATE_DISABLED, memory_order_relaxed);
        return ESP_OK;
    }

    /* Check if SSID is configured */
    if (strlen(s_wifi.config.ssid) == 0) {
        ESP_LOGI(TAG, "No SSID configured, WiFi disabled");
        atomic_store_explicit(&s_wifi.state, WIFI_STATE_DISABLED, memory_order_relaxed);
        return ESP_OK;
    }

    /* Create connection task */
    BaseType_t task_ret = xTaskCreate(connection_task,
                                       "wifi_conn",
                                       4096,
                                       NULL,
                                       5,
                                       NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create connection task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "WiFi start initiated");
    return ESP_OK;
}

void wifi_app_stop(void)
{
    if (!s_wifi.initialized) {
        return;
    }

    esp_wifi_stop();
    atomic_store_explicit(&s_wifi.state, WIFI_STATE_DISABLED, memory_order_relaxed);
    s_wifi.retry_count = 0;
    s_wifi.ip_addr.addr = 0;

    ESP_LOGI(TAG, "WiFi stopped");
}

wifi_state_t wifi_get_state(void)
{
    return atomic_load_explicit(&s_wifi.state, memory_order_relaxed);
}

bool wifi_get_ip(char *buf, size_t len)
{
    if (buf == NULL || len < 16) {
        return false;
    }

    if (s_wifi.ip_addr.addr == 0) {
        return false;
    }

    snprintf(buf, len, IPSTR, IP2STR(&s_wifi.ip_addr));
    return true;
}

bool wifi_is_connected(void)
{
    wifi_state_t state = atomic_load_explicit(&s_wifi.state, memory_order_relaxed);
    return (state == WIFI_STATE_CONNECTED);
}

/* WiFi event handler */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started, connecting...");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                s_wifi.retry_count++;
                ESP_LOGW(TAG, "STA disconnected (retry %d/%d)", s_wifi.retry_count, MAX_RETRY_COUNT);

                if (s_wifi.retry_count < MAX_RETRY_COUNT) {
                    esp_wifi_connect();
                } else {
                    ESP_LOGE(TAG, "Max retries reached");
                    xEventGroupSetBits(s_wifi.event_group, WIFI_FAIL_BIT);
                }
                break;

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "Station " MACSTR " joined AP", MAC2STR(event->mac));
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG, "Station " MACSTR " left AP", MAC2STR(event->mac));
                break;
            }

            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            s_wifi.ip_addr = event->ip_info.ip;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&s_wifi.ip_addr));
            s_wifi.retry_count = 0;
            xEventGroupSetBits(s_wifi.event_group, WIFI_CONNECTED_BIT);
        }
    }
}

/* Configure static IP if requested */
static esp_err_t configure_static_ip(void)
{
    if (!s_wifi.config.use_static_ip) {
        return ESP_OK;
    }

    /* Check if IP address is configured (not 0.0.0.0) */
    if (strcmp(s_wifi.config.ip_address, "0.0.0.0") == 0) {
        ESP_LOGI(TAG, "Static IP requested but address is 0.0.0.0, using DHCP");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Configuring static IP: %s", s_wifi.config.ip_address);

    /* Stop DHCP client */
    esp_err_t ret = esp_netif_dhcpc_stop(s_wifi.sta_netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGE(TAG, "Failed to stop DHCP client: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure IP info */
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));

    ip_info.ip.addr = esp_ip4addr_aton(s_wifi.config.ip_address);
    ip_info.netmask.addr = esp_ip4addr_aton(s_wifi.config.netmask);
    ip_info.gw.addr = esp_ip4addr_aton(s_wifi.config.gateway);

    ret = esp_netif_set_ip_info(s_wifi.sta_netif, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set IP info: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure DNS if provided */
    if (strcmp(s_wifi.config.dns, "0.0.0.0") != 0) {
        esp_netif_dns_info_t dns_info;
        dns_info.ip.u_addr.ip4.addr = esp_ip4addr_aton(s_wifi.config.dns);
        dns_info.ip.type = ESP_IPADDR_TYPE_V4;

        ret = esp_netif_set_dns_info(s_wifi.sta_netif, ESP_NETIF_DNS_MAIN, &dns_info);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set DNS: %s", esp_err_to_name(ret));
            /* Non-fatal, continue */
        } else {
            ESP_LOGI(TAG, "DNS configured: %s", s_wifi.config.dns);
        }
    }

    return ESP_OK;
}

/* Start AP mode with MAC-based SSID */
static esp_err_t start_ap_mode(void)
{
    ESP_LOGI(TAG, "Starting AP mode");

    /* Create AP netif if needed */
    if (s_wifi.ap_netif == NULL) {
        s_wifi.ap_netif = esp_netif_create_default_wifi_ap();
        if (s_wifi.ap_netif == NULL) {
            ESP_LOGE(TAG, "Failed to create AP netif");
            return ESP_FAIL;
        }
    }

    /* Get MAC address for SSID suffix */
    uint8_t mac[6];
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_AP, mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Build SSID: CWKeyer-XXXX (last 4 hex digits of MAC) */
    char ap_ssid[33];
    snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X",
             AP_SSID_PREFIX, mac[4], mac[5]);

    /* Configure AP */
    wifi_config_t ap_config = {
        .ap = {
            .channel = AP_CHANNEL,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    /* Copy SSID */
    size_t ssid_len = strlen(ap_ssid);
    if (ssid_len >= sizeof(ap_config.ap.ssid)) {
        ssid_len = sizeof(ap_config.ap.ssid) - 1U;
    }
    memcpy(ap_config.ap.ssid, ap_ssid, ssid_len);
    ap_config.ap.ssid[ssid_len] = '\0';
    ap_config.ap.ssid_len = (uint8_t)ssid_len;

    /* Set WiFi mode to AP */
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start AP: %s", esp_err_to_name(ret));
        return ret;
    }

    atomic_store_explicit(&s_wifi.state, WIFI_STATE_AP_MODE, memory_order_relaxed);
    ESP_LOGI(TAG, "AP mode started: %s (open)", ap_ssid);

    return ESP_OK;
}

/* Connection task */
static void connection_task(void *pvParameters)
{
    (void)pvParameters;

    ESP_LOGI(TAG, "Connection task started");
    atomic_store_explicit(&s_wifi.state, WIFI_STATE_CONNECTING, memory_order_relaxed);

    /* Create STA netif */
    s_wifi.sta_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi.sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create STA netif");
        atomic_store_explicit(&s_wifi.state, WIFI_STATE_FAILED, memory_order_relaxed);
        vTaskDelete(NULL);
        return;
    }

    /* Configure static IP if requested */
    esp_err_t ret = configure_static_ip();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Static IP configuration failed, continuing with DHCP");
    }

    /* Configure STA */
    wifi_config_t sta_config = {0};
    size_t ssid_len = strlen(s_wifi.config.ssid);
    if (ssid_len >= sizeof(sta_config.sta.ssid)) {
        ssid_len = sizeof(sta_config.sta.ssid) - 1U;
    }
    memcpy(sta_config.sta.ssid, s_wifi.config.ssid, ssid_len);
    sta_config.sta.ssid[ssid_len] = '\0';

    size_t pass_len = strlen(s_wifi.config.password);
    if (pass_len >= sizeof(sta_config.sta.password)) {
        pass_len = sizeof(sta_config.sta.password) - 1U;
    }
    memcpy(sta_config.sta.password, s_wifi.config.password, pass_len);
    sta_config.sta.password[pass_len] = '\0';

    /* Set WiFi mode to STA */
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA mode: %s", esp_err_to_name(ret));
        atomic_store_explicit(&s_wifi.state, WIFI_STATE_FAILED, memory_order_relaxed);
        vTaskDelete(NULL);
        return;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(ret));
        atomic_store_explicit(&s_wifi.state, WIFI_STATE_FAILED, memory_order_relaxed);
        vTaskDelete(NULL);
        return;
    }

    /* Clear event bits */
    xEventGroupClearBits(s_wifi.event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_wifi.retry_count = 0;

    /* Start WiFi (triggers WIFI_EVENT_STA_START -> esp_wifi_connect) */
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        atomic_store_explicit(&s_wifi.state, WIFI_STATE_FAILED, memory_order_relaxed);
        vTaskDelete(NULL);
        return;
    }

    /* Wait for connection or failure */
    TickType_t timeout_ticks = pdMS_TO_TICKS((uint32_t)s_wifi.config.timeout_sec * 1000U);
    EventBits_t bits = xEventGroupWaitBits(s_wifi.event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE,
                                            pdFALSE,
                                            timeout_ticks);

    if (bits & WIFI_CONNECTED_BIT) {
        /* Successfully connected */
        atomic_store_explicit(&s_wifi.state, WIFI_STATE_CONNECTED, memory_order_relaxed);
        ESP_LOGI(TAG, "Connected to SSID: %s", s_wifi.config.ssid);
    } else {
        /* Failed or timeout */
        ESP_LOGW(TAG, "Connection failed or timeout");
        atomic_store_explicit(&s_wifi.state, WIFI_STATE_FAILED, memory_order_relaxed);

        /* Brief delay in FAILED state for LED indication */
        vTaskDelay(pdMS_TO_TICKS(FAILED_STATE_DELAY_MS));

        /* Stop STA mode before switching to AP */
        esp_wifi_stop();
        vTaskDelay(pdMS_TO_TICKS(100));

        /* Fall back to AP mode */
        ret = start_ap_mode();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start AP mode");
            atomic_store_explicit(&s_wifi.state, WIFI_STATE_DISABLED, memory_order_relaxed);
        }
    }

    /* Task complete */
    vTaskDelete(NULL);
}
