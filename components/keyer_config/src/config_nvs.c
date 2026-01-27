/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config_nvs.c
 * @brief NVS persistence implementation
 */

#include "config_nvs.h"
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

int config_load_from_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0;  /* Namespace doesn't exist yet, use defaults */
    }
    if (err != ESP_OK) {
        return -1;
    }

    int loaded = 0;
    uint8_t u8_val;
    uint16_t u16_val;
    uint32_t u32_val;
    size_t str_len;

    /* Load keyer.wpm */
    if (nvs_get_u16(handle, NVS_KEYER_WPM, &u16_val) == ESP_OK) {
        atomic_store_explicit(&g_config.keyer.wpm, u16_val, memory_order_relaxed);
        loaded++;
    }

    /* Load keyer.iambic_mode */
    if (nvs_get_u8(handle, NVS_KEYER_IAMBIC_MODE, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.keyer.iambic_mode, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load keyer.memory_mode */
    if (nvs_get_u8(handle, NVS_KEYER_MEMORY_MODE, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.keyer.memory_mode, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load keyer.squeeze_mode */
    if (nvs_get_u8(handle, NVS_KEYER_SQUEEZE_MODE, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.keyer.squeeze_mode, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load keyer.weight */
    if (nvs_get_u8(handle, NVS_KEYER_WEIGHT, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.keyer.weight, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load keyer.mem_window_start_pct */
    if (nvs_get_u8(handle, NVS_KEYER_MEM_WINDOW_START_PCT, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.keyer.mem_window_start_pct, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load keyer.mem_window_end_pct */
    if (nvs_get_u8(handle, NVS_KEYER_MEM_WINDOW_END_PCT, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.keyer.mem_window_end_pct, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load audio.sidetone_freq_hz */
    if (nvs_get_u16(handle, NVS_AUDIO_SIDETONE_FREQ_HZ, &u16_val) == ESP_OK) {
        atomic_store_explicit(&g_config.audio.sidetone_freq_hz, u16_val, memory_order_relaxed);
        loaded++;
    }

    /* Load audio.sidetone_volume */
    if (nvs_get_u8(handle, NVS_AUDIO_SIDETONE_VOLUME, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.audio.sidetone_volume, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load audio.fade_duration_ms */
    if (nvs_get_u8(handle, NVS_AUDIO_FADE_DURATION_MS, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.audio.fade_duration_ms, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load hardware.gpio_dit */
    if (nvs_get_u8(handle, NVS_HARDWARE_GPIO_DIT, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.hardware.gpio_dit, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load hardware.gpio_dah */
    if (nvs_get_u8(handle, NVS_HARDWARE_GPIO_DAH, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.hardware.gpio_dah, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load hardware.gpio_tx */
    if (nvs_get_u8(handle, NVS_HARDWARE_GPIO_TX, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.hardware.gpio_tx, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load timing.ptt_tail_ms */
    if (nvs_get_u32(handle, NVS_TIMING_PTT_TAIL_MS, &u32_val) == ESP_OK) {
        atomic_store_explicit(&g_config.timing.ptt_tail_ms, u32_val, memory_order_relaxed);
        loaded++;
    }

    /* Load timing.tick_rate_hz */
    if (nvs_get_u32(handle, NVS_TIMING_TICK_RATE_HZ, &u32_val) == ESP_OK) {
        atomic_store_explicit(&g_config.timing.tick_rate_hz, u32_val, memory_order_relaxed);
        loaded++;
    }

    /* Load system.debug_logging */
    if (nvs_get_u8(handle, NVS_SYSTEM_DEBUG_LOGGING, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.system.debug_logging, u8_val != 0, memory_order_relaxed);
        loaded++;
    }

    /* Load system.callsign */
    str_len = sizeof(g_config.system.callsign);
    if (nvs_get_str(handle, NVS_SYSTEM_CALLSIGN, g_config.system.callsign, &str_len) == ESP_OK) {
        loaded++;
    }

    /* Load system.ui_theme */
    if (nvs_get_u8(handle, NVS_SYSTEM_UI_THEME, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.system.ui_theme, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load leds.gpio_data */
    if (nvs_get_u8(handle, NVS_LEDS_GPIO_DATA, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.leds.gpio_data, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load leds.count */
    if (nvs_get_u8(handle, NVS_LEDS_COUNT, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.leds.count, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load leds.brightness */
    if (nvs_get_u8(handle, NVS_LEDS_BRIGHTNESS, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.leds.brightness, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load leds.brightness_dim */
    if (nvs_get_u8(handle, NVS_LEDS_BRIGHTNESS_DIM, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.leds.brightness_dim, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load wifi.enabled */
    if (nvs_get_u8(handle, NVS_WIFI_ENABLED, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.wifi.enabled, u8_val != 0, memory_order_relaxed);
        loaded++;
    }

    /* Load wifi.ssid */
    str_len = sizeof(g_config.wifi.ssid);
    if (nvs_get_str(handle, NVS_WIFI_SSID, g_config.wifi.ssid, &str_len) == ESP_OK) {
        loaded++;
    }

    /* Load wifi.password */
    str_len = sizeof(g_config.wifi.password);
    if (nvs_get_str(handle, NVS_WIFI_PASSWORD, g_config.wifi.password, &str_len) == ESP_OK) {
        loaded++;
    }

    /* Load wifi.timeout_sec */
    if (nvs_get_u16(handle, NVS_WIFI_TIMEOUT_SEC, &u16_val) == ESP_OK) {
        atomic_store_explicit(&g_config.wifi.timeout_sec, u16_val, memory_order_relaxed);
        loaded++;
    }

    /* Load wifi.use_static_ip */
    if (nvs_get_u8(handle, NVS_WIFI_USE_STATIC_IP, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.wifi.use_static_ip, u8_val != 0, memory_order_relaxed);
        loaded++;
    }

    /* Load wifi.ip_address */
    str_len = sizeof(g_config.wifi.ip_address);
    if (nvs_get_str(handle, NVS_WIFI_IP_ADDRESS, g_config.wifi.ip_address, &str_len) == ESP_OK) {
        loaded++;
    }

    /* Load wifi.netmask */
    str_len = sizeof(g_config.wifi.netmask);
    if (nvs_get_str(handle, NVS_WIFI_NETMASK, g_config.wifi.netmask, &str_len) == ESP_OK) {
        loaded++;
    }

    /* Load wifi.gateway */
    str_len = sizeof(g_config.wifi.gateway);
    if (nvs_get_str(handle, NVS_WIFI_GATEWAY, g_config.wifi.gateway, &str_len) == ESP_OK) {
        loaded++;
    }

    /* Load wifi.dns */
    str_len = sizeof(g_config.wifi.dns);
    if (nvs_get_str(handle, NVS_WIFI_DNS, g_config.wifi.dns, &str_len) == ESP_OK) {
        loaded++;
    }

    /* Load vpn.enabled */
    if (nvs_get_u8(handle, NVS_VPN_ENABLED, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.vpn.enabled, u8_val != 0, memory_order_relaxed);
        loaded++;
    }

    /* Load vpn.server_endpoint */
    str_len = sizeof(g_config.vpn.server_endpoint);
    if (nvs_get_str(handle, NVS_VPN_SERVER_ENDPOINT, g_config.vpn.server_endpoint, &str_len) == ESP_OK) {
        loaded++;
    }

    /* Load vpn.server_port */
    if (nvs_get_u16(handle, NVS_VPN_SERVER_PORT, &u16_val) == ESP_OK) {
        atomic_store_explicit(&g_config.vpn.server_port, u16_val, memory_order_relaxed);
        loaded++;
    }

    /* Load vpn.server_public_key */
    str_len = sizeof(g_config.vpn.server_public_key);
    if (nvs_get_str(handle, NVS_VPN_SERVER_PUBLIC_KEY, g_config.vpn.server_public_key, &str_len) == ESP_OK) {
        loaded++;
    }

    /* Load vpn.client_private_key */
    str_len = sizeof(g_config.vpn.client_private_key);
    if (nvs_get_str(handle, NVS_VPN_CLIENT_PRIVATE_KEY, g_config.vpn.client_private_key, &str_len) == ESP_OK) {
        loaded++;
    }

    /* Load vpn.client_address */
    str_len = sizeof(g_config.vpn.client_address);
    if (nvs_get_str(handle, NVS_VPN_CLIENT_ADDRESS, g_config.vpn.client_address, &str_len) == ESP_OK) {
        loaded++;
    }

    /* Load vpn.allowed_ips */
    str_len = sizeof(g_config.vpn.allowed_ips);
    if (nvs_get_str(handle, NVS_VPN_ALLOWED_IPS, g_config.vpn.allowed_ips, &str_len) == ESP_OK) {
        loaded++;
    }

    /* Load vpn.persistent_keepalive */
    if (nvs_get_u16(handle, NVS_VPN_PERSISTENT_KEEPALIVE, &u16_val) == ESP_OK) {
        atomic_store_explicit(&g_config.vpn.persistent_keepalive, u16_val, memory_order_relaxed);
        loaded++;
    }

    /* Load remote.cwnet_enabled */
    if (nvs_get_u8(handle, NVS_REMOTE_CWNET_ENABLED, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.remote.cwnet_enabled, u8_val != 0, memory_order_relaxed);
        loaded++;
    }

    /* Load remote.server_host */
    str_len = sizeof(g_config.remote.server_host);
    if (nvs_get_str(handle, NVS_REMOTE_SERVER_HOST, g_config.remote.server_host, &str_len) == ESP_OK) {
        loaded++;
    }

    /* Load remote.server_port */
    if (nvs_get_u16(handle, NVS_REMOTE_SERVER_PORT, &u16_val) == ESP_OK) {
        atomic_store_explicit(&g_config.remote.server_port, u16_val, memory_order_relaxed);
        loaded++;
    }

    /* Load remote.username */
    str_len = sizeof(g_config.remote.username);
    if (nvs_get_str(handle, NVS_REMOTE_USERNAME, g_config.remote.username, &str_len) == ESP_OK) {
        loaded++;
    }

    nvs_close(handle);
    return loaded;
}

int config_save_to_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return -1;
    }

    int saved = 0;

    /* Save keyer.wpm */
    if (nvs_set_u16(handle, NVS_KEYER_WPM,
            atomic_load_explicit(&g_config.keyer.wpm, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save keyer.iambic_mode */
    if (nvs_set_u8(handle, NVS_KEYER_IAMBIC_MODE,
            atomic_load_explicit(&g_config.keyer.iambic_mode, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save keyer.memory_mode */
    if (nvs_set_u8(handle, NVS_KEYER_MEMORY_MODE,
            atomic_load_explicit(&g_config.keyer.memory_mode, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save keyer.squeeze_mode */
    if (nvs_set_u8(handle, NVS_KEYER_SQUEEZE_MODE,
            atomic_load_explicit(&g_config.keyer.squeeze_mode, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save keyer.weight */
    if (nvs_set_u8(handle, NVS_KEYER_WEIGHT,
            atomic_load_explicit(&g_config.keyer.weight, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save keyer.mem_window_start_pct */
    if (nvs_set_u8(handle, NVS_KEYER_MEM_WINDOW_START_PCT,
            atomic_load_explicit(&g_config.keyer.mem_window_start_pct, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save keyer.mem_window_end_pct */
    if (nvs_set_u8(handle, NVS_KEYER_MEM_WINDOW_END_PCT,
            atomic_load_explicit(&g_config.keyer.mem_window_end_pct, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save audio.sidetone_freq_hz */
    if (nvs_set_u16(handle, NVS_AUDIO_SIDETONE_FREQ_HZ,
            atomic_load_explicit(&g_config.audio.sidetone_freq_hz, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save audio.sidetone_volume */
    if (nvs_set_u8(handle, NVS_AUDIO_SIDETONE_VOLUME,
            atomic_load_explicit(&g_config.audio.sidetone_volume, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save audio.fade_duration_ms */
    if (nvs_set_u8(handle, NVS_AUDIO_FADE_DURATION_MS,
            atomic_load_explicit(&g_config.audio.fade_duration_ms, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save hardware.gpio_dit */
    if (nvs_set_u8(handle, NVS_HARDWARE_GPIO_DIT,
            atomic_load_explicit(&g_config.hardware.gpio_dit, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save hardware.gpio_dah */
    if (nvs_set_u8(handle, NVS_HARDWARE_GPIO_DAH,
            atomic_load_explicit(&g_config.hardware.gpio_dah, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save hardware.gpio_tx */
    if (nvs_set_u8(handle, NVS_HARDWARE_GPIO_TX,
            atomic_load_explicit(&g_config.hardware.gpio_tx, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save timing.ptt_tail_ms */
    if (nvs_set_u32(handle, NVS_TIMING_PTT_TAIL_MS,
            atomic_load_explicit(&g_config.timing.ptt_tail_ms, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save timing.tick_rate_hz */
    if (nvs_set_u32(handle, NVS_TIMING_TICK_RATE_HZ,
            atomic_load_explicit(&g_config.timing.tick_rate_hz, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save system.debug_logging */
    if (nvs_set_u8(handle, NVS_SYSTEM_DEBUG_LOGGING,
            atomic_load_explicit(&g_config.system.debug_logging, memory_order_relaxed) ? 1 : 0) == ESP_OK) {
        saved++;
    }

    /* Save system.callsign */
    if (nvs_set_str(handle, NVS_SYSTEM_CALLSIGN, g_config.system.callsign) == ESP_OK) {
        saved++;
    }

    /* Save system.ui_theme */
    if (nvs_set_u8(handle, NVS_SYSTEM_UI_THEME,
            atomic_load_explicit(&g_config.system.ui_theme, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save leds.gpio_data */
    if (nvs_set_u8(handle, NVS_LEDS_GPIO_DATA,
            atomic_load_explicit(&g_config.leds.gpio_data, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save leds.count */
    if (nvs_set_u8(handle, NVS_LEDS_COUNT,
            atomic_load_explicit(&g_config.leds.count, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save leds.brightness */
    if (nvs_set_u8(handle, NVS_LEDS_BRIGHTNESS,
            atomic_load_explicit(&g_config.leds.brightness, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save leds.brightness_dim */
    if (nvs_set_u8(handle, NVS_LEDS_BRIGHTNESS_DIM,
            atomic_load_explicit(&g_config.leds.brightness_dim, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save wifi.enabled */
    if (nvs_set_u8(handle, NVS_WIFI_ENABLED,
            atomic_load_explicit(&g_config.wifi.enabled, memory_order_relaxed) ? 1 : 0) == ESP_OK) {
        saved++;
    }

    /* Save wifi.ssid */
    if (nvs_set_str(handle, NVS_WIFI_SSID, g_config.wifi.ssid) == ESP_OK) {
        saved++;
    }

    /* Save wifi.password */
    if (nvs_set_str(handle, NVS_WIFI_PASSWORD, g_config.wifi.password) == ESP_OK) {
        saved++;
    }

    /* Save wifi.timeout_sec */
    if (nvs_set_u16(handle, NVS_WIFI_TIMEOUT_SEC,
            atomic_load_explicit(&g_config.wifi.timeout_sec, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save wifi.use_static_ip */
    if (nvs_set_u8(handle, NVS_WIFI_USE_STATIC_IP,
            atomic_load_explicit(&g_config.wifi.use_static_ip, memory_order_relaxed) ? 1 : 0) == ESP_OK) {
        saved++;
    }

    /* Save wifi.ip_address */
    if (nvs_set_str(handle, NVS_WIFI_IP_ADDRESS, g_config.wifi.ip_address) == ESP_OK) {
        saved++;
    }

    /* Save wifi.netmask */
    if (nvs_set_str(handle, NVS_WIFI_NETMASK, g_config.wifi.netmask) == ESP_OK) {
        saved++;
    }

    /* Save wifi.gateway */
    if (nvs_set_str(handle, NVS_WIFI_GATEWAY, g_config.wifi.gateway) == ESP_OK) {
        saved++;
    }

    /* Save wifi.dns */
    if (nvs_set_str(handle, NVS_WIFI_DNS, g_config.wifi.dns) == ESP_OK) {
        saved++;
    }

    /* Save vpn.enabled */
    if (nvs_set_u8(handle, NVS_VPN_ENABLED,
            atomic_load_explicit(&g_config.vpn.enabled, memory_order_relaxed) ? 1 : 0) == ESP_OK) {
        saved++;
    }

    /* Save vpn.server_endpoint */
    if (nvs_set_str(handle, NVS_VPN_SERVER_ENDPOINT, g_config.vpn.server_endpoint) == ESP_OK) {
        saved++;
    }

    /* Save vpn.server_port */
    if (nvs_set_u16(handle, NVS_VPN_SERVER_PORT,
            atomic_load_explicit(&g_config.vpn.server_port, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save vpn.server_public_key */
    if (nvs_set_str(handle, NVS_VPN_SERVER_PUBLIC_KEY, g_config.vpn.server_public_key) == ESP_OK) {
        saved++;
    }

    /* Save vpn.client_private_key */
    if (nvs_set_str(handle, NVS_VPN_CLIENT_PRIVATE_KEY, g_config.vpn.client_private_key) == ESP_OK) {
        saved++;
    }

    /* Save vpn.client_address */
    if (nvs_set_str(handle, NVS_VPN_CLIENT_ADDRESS, g_config.vpn.client_address) == ESP_OK) {
        saved++;
    }

    /* Save vpn.allowed_ips */
    if (nvs_set_str(handle, NVS_VPN_ALLOWED_IPS, g_config.vpn.allowed_ips) == ESP_OK) {
        saved++;
    }

    /* Save vpn.persistent_keepalive */
    if (nvs_set_u16(handle, NVS_VPN_PERSISTENT_KEEPALIVE,
            atomic_load_explicit(&g_config.vpn.persistent_keepalive, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save remote.cwnet_enabled */
    if (nvs_set_u8(handle, NVS_REMOTE_CWNET_ENABLED,
            atomic_load_explicit(&g_config.remote.cwnet_enabled, memory_order_relaxed) ? 1 : 0) == ESP_OK) {
        saved++;
    }

    /* Save remote.server_host */
    if (nvs_set_str(handle, NVS_REMOTE_SERVER_HOST, g_config.remote.server_host) == ESP_OK) {
        saved++;
    }

    /* Save remote.server_port */
    if (nvs_set_u16(handle, NVS_REMOTE_SERVER_PORT,
            atomic_load_explicit(&g_config.remote.server_port, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save remote.username */
    if (nvs_set_str(handle, NVS_REMOTE_USERNAME, g_config.remote.username) == ESP_OK) {
        saved++;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    return (err == ESP_OK) ? saved : -1;
}

esp_err_t config_load_param(const char *name) {
    /* TODO: Implement single parameter load */
    (void)name;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t config_save_param(const char *name) {
    /* TODO: Implement single parameter save */
    (void)name;
    return ESP_ERR_NOT_SUPPORTED;
}
