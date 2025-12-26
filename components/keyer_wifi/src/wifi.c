/**
 * @file wifi.c
 * @brief WiFi connectivity manager (stub)
 */

#include "wifi.h"

esp_err_t wifi_app_init(const wifi_config_app_t *config)
{
    (void)config;
    return ESP_OK;
}

esp_err_t wifi_app_start(void)
{
    return ESP_OK;
}

void wifi_app_stop(void)
{
}

wifi_state_t wifi_get_state(void)
{
    return WIFI_STATE_DISABLED;
}

bool wifi_get_ip(char *buf, size_t len)
{
    (void)buf;
    (void)len;
    return false;
}

bool wifi_is_connected(void)
{
    return false;
}
