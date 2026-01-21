/**
 * @file provisioning_wifi.c
 * @brief WiFi AP mode and network scanning for provisioning
 *
 * Creates open AP for initial device setup.
 * Provides WiFi scan functionality for network selection.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "prov_wifi";

/* AP Configuration */
#define PROV_AP_SSID        "CWKeyer-Setup"
#define PROV_AP_CHANNEL     1
#define PROV_AP_MAX_CONN    4

/* Scan configuration */
#define PROV_SCAN_MAX_AP    20

/* State */
static esp_netif_t *s_ap_netif = NULL;
static bool s_initialized = false;

/* Scan results storage */
static wifi_ap_record_t s_scan_results[PROV_SCAN_MAX_AP];
static uint16_t s_scan_count = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "Station " MACSTR " connected", MAC2STR(event->mac));
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG, "Station " MACSTR " disconnected", MAC2STR(event->mac));
                break;
            }
            default:
                break;
        }
    }
}

void prov_wifi_start_ap(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return;
    }

    ESP_LOGI(TAG, "Initializing WiFi AP mode...");

    /* Initialize TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Create AP netif */
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return;
    }

    /* Initialize WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register event handler */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler,
                                                         NULL,
                                                         NULL));

    /* Configure AP */
    wifi_config_t ap_config = {
        .ap = {
            .ssid = PROV_AP_SSID,
            .ssid_len = (uint8_t)strlen(PROV_AP_SSID),
            .channel = PROV_AP_CHANNEL,
            .max_connection = PROV_AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));  /* AP+STA for scanning */
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_initialized = true;

    ESP_LOGI(TAG, "AP started: SSID=%s (open), IP=192.168.4.1", PROV_AP_SSID);
}

void prov_wifi_stop(void)
{
    if (!s_initialized) {
        return;
    }

    esp_wifi_stop();
    esp_wifi_deinit();

    if (s_ap_netif != NULL) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "WiFi stopped");
}

int prov_wifi_scan(char *json_buf, size_t buf_size)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return -1;
    }

    ESP_LOGI(TAG, "Starting WiFi scan...");

    /* Configure scan */
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,
                .max = 300,
            },
        },
    };

    /* Start scan (blocking) */
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
        snprintf(json_buf, buf_size, "[]");
        return 0;
    }

    /* Get results */
    s_scan_count = PROV_SCAN_MAX_AP;
    err = esp_wifi_scan_get_ap_records(&s_scan_count, s_scan_results);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(err));
        snprintf(json_buf, buf_size, "[]");
        return 0;
    }

    ESP_LOGI(TAG, "Scan found %d networks", s_scan_count);

    /* Build JSON array */
    size_t offset = 0;
    offset += (size_t)snprintf(json_buf + offset, buf_size - offset, "[");

    for (uint16_t i = 0; i < s_scan_count && offset < buf_size - 100; i++) {
        if (i > 0) {
            offset += (size_t)snprintf(json_buf + offset, buf_size - offset, ",");
        }

        /* Escape SSID for JSON (simple escape for quotes) */
        char escaped_ssid[65] = {0};
        const char *src = (const char *)s_scan_results[i].ssid;
        size_t j = 0;
        for (size_t k = 0; src[k] != '\0' && j < sizeof(escaped_ssid) - 2; k++) {
            if (src[k] == '"' || src[k] == '\\') {
                escaped_ssid[j++] = '\\';
            }
            escaped_ssid[j++] = src[k];
        }
        escaped_ssid[j] = '\0';

        /* Auth mode: 0=open, others=secured */
        int auth = (s_scan_results[i].authmode == WIFI_AUTH_OPEN) ? 0 : 1;

        offset += (size_t)snprintf(json_buf + offset, buf_size - offset,
                                    "{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%d}",
                                    escaped_ssid,
                                    s_scan_results[i].rssi,
                                    auth);
    }

    offset += (size_t)snprintf(json_buf + offset, buf_size - offset, "]");

    return (int)s_scan_count;
}
