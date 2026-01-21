/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config_console.c
 * @brief Console parameter registry implementation
 */

#include "config_console.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Family Registry
 * ============================================================================ */

const family_descriptor_t CONSOLE_FAMILIES[FAMILY_COUNT] = {
    { "keyer", "k", "Keying behavior and timing", 1 },
    { "audio", "a,snd", "Sidetone and audio output", 2 },
    { "hardware", "hw,gpio", "GPIO and pin configuration", 3 },
    { "timing", "t", "RT loop and PTT timing", 4 },
    { "system", "sys", "Debug and system settings", 5 },
    { "leds", "led,l", "RGB LED strip configuration", 6 },
    { "wifi", "w,net", "Wireless network configuration", 7 },
    { "remote", "r,cwnet", "CWNet remote keying configuration", 8 },
};

/* ============================================================================
 * Parameter Accessors
 * ============================================================================ */

static param_value_t get_keyer_wpm(void) {
    param_value_t v;
    v.u16 = atomic_load_explicit(&g_config.keyer.wpm, memory_order_relaxed);
    return v;
}

static void set_keyer_wpm(param_value_t v) {
    atomic_store_explicit(&g_config.keyer.wpm, v.u16, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_keyer_iambic_mode(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.keyer.iambic_mode, memory_order_relaxed);
    return v;
}

static void set_keyer_iambic_mode(param_value_t v) {
    atomic_store_explicit(&g_config.keyer.iambic_mode, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_keyer_memory_mode(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.keyer.memory_mode, memory_order_relaxed);
    return v;
}

static void set_keyer_memory_mode(param_value_t v) {
    atomic_store_explicit(&g_config.keyer.memory_mode, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_keyer_squeeze_mode(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.keyer.squeeze_mode, memory_order_relaxed);
    return v;
}

static void set_keyer_squeeze_mode(param_value_t v) {
    atomic_store_explicit(&g_config.keyer.squeeze_mode, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_keyer_weight(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.keyer.weight, memory_order_relaxed);
    return v;
}

static void set_keyer_weight(param_value_t v) {
    atomic_store_explicit(&g_config.keyer.weight, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_keyer_mem_window_start_pct(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.keyer.mem_window_start_pct, memory_order_relaxed);
    return v;
}

static void set_keyer_mem_window_start_pct(param_value_t v) {
    atomic_store_explicit(&g_config.keyer.mem_window_start_pct, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_keyer_mem_window_end_pct(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.keyer.mem_window_end_pct, memory_order_relaxed);
    return v;
}

static void set_keyer_mem_window_end_pct(param_value_t v) {
    atomic_store_explicit(&g_config.keyer.mem_window_end_pct, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_audio_sidetone_freq_hz(void) {
    param_value_t v;
    v.u16 = atomic_load_explicit(&g_config.audio.sidetone_freq_hz, memory_order_relaxed);
    return v;
}

static void set_audio_sidetone_freq_hz(param_value_t v) {
    atomic_store_explicit(&g_config.audio.sidetone_freq_hz, v.u16, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_audio_sidetone_volume(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.audio.sidetone_volume, memory_order_relaxed);
    return v;
}

static void set_audio_sidetone_volume(param_value_t v) {
    atomic_store_explicit(&g_config.audio.sidetone_volume, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_audio_fade_duration_ms(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.audio.fade_duration_ms, memory_order_relaxed);
    return v;
}

static void set_audio_fade_duration_ms(param_value_t v) {
    atomic_store_explicit(&g_config.audio.fade_duration_ms, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_hardware_gpio_dit(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.hardware.gpio_dit, memory_order_relaxed);
    return v;
}

static void set_hardware_gpio_dit(param_value_t v) {
    atomic_store_explicit(&g_config.hardware.gpio_dit, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_hardware_gpio_dah(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.hardware.gpio_dah, memory_order_relaxed);
    return v;
}

static void set_hardware_gpio_dah(param_value_t v) {
    atomic_store_explicit(&g_config.hardware.gpio_dah, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_hardware_gpio_tx(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.hardware.gpio_tx, memory_order_relaxed);
    return v;
}

static void set_hardware_gpio_tx(param_value_t v) {
    atomic_store_explicit(&g_config.hardware.gpio_tx, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_timing_ptt_tail_ms(void) {
    param_value_t v;
    v.u32 = atomic_load_explicit(&g_config.timing.ptt_tail_ms, memory_order_relaxed);
    return v;
}

static void set_timing_ptt_tail_ms(param_value_t v) {
    atomic_store_explicit(&g_config.timing.ptt_tail_ms, v.u32, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_timing_tick_rate_hz(void) {
    param_value_t v;
    v.u32 = atomic_load_explicit(&g_config.timing.tick_rate_hz, memory_order_relaxed);
    return v;
}

static void set_timing_tick_rate_hz(param_value_t v) {
    atomic_store_explicit(&g_config.timing.tick_rate_hz, v.u32, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_system_debug_logging(void) {
    param_value_t v;
    v.b = atomic_load_explicit(&g_config.system.debug_logging, memory_order_relaxed);
    return v;
}

static void set_system_debug_logging(param_value_t v) {
    atomic_store_explicit(&g_config.system.debug_logging, v.b, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_system_callsign(void) {
    param_value_t v;
    v.str = g_config.system.callsign;
    return v;
}

static void set_system_callsign(param_value_t v) {
    strncpy(g_config.system.callsign, v.str, 12);
    g_config.system.callsign[12] = '\0';
    config_bump_generation(&g_config);
}

static param_value_t get_system_ui_theme(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.system.ui_theme, memory_order_relaxed);
    return v;
}

static void set_system_ui_theme(param_value_t v) {
    atomic_store_explicit(&g_config.system.ui_theme, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_leds_gpio_data(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.leds.gpio_data, memory_order_relaxed);
    return v;
}

static void set_leds_gpio_data(param_value_t v) {
    atomic_store_explicit(&g_config.leds.gpio_data, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_leds_count(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.leds.count, memory_order_relaxed);
    return v;
}

static void set_leds_count(param_value_t v) {
    atomic_store_explicit(&g_config.leds.count, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_leds_brightness(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.leds.brightness, memory_order_relaxed);
    return v;
}

static void set_leds_brightness(param_value_t v) {
    atomic_store_explicit(&g_config.leds.brightness, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_leds_brightness_dim(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.leds.brightness_dim, memory_order_relaxed);
    return v;
}

static void set_leds_brightness_dim(param_value_t v) {
    atomic_store_explicit(&g_config.leds.brightness_dim, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_wifi_enabled(void) {
    param_value_t v;
    v.b = atomic_load_explicit(&g_config.wifi.enabled, memory_order_relaxed);
    return v;
}

static void set_wifi_enabled(param_value_t v) {
    atomic_store_explicit(&g_config.wifi.enabled, v.b, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_wifi_ssid(void) {
    param_value_t v;
    v.str = g_config.wifi.ssid;
    return v;
}

static void set_wifi_ssid(param_value_t v) {
    strncpy(g_config.wifi.ssid, v.str, 32);
    g_config.wifi.ssid[32] = '\0';
    config_bump_generation(&g_config);
}

static param_value_t get_wifi_password(void) {
    param_value_t v;
    v.str = g_config.wifi.password;
    return v;
}

static void set_wifi_password(param_value_t v) {
    strncpy(g_config.wifi.password, v.str, 64);
    g_config.wifi.password[64] = '\0';
    config_bump_generation(&g_config);
}

static param_value_t get_wifi_timeout_sec(void) {
    param_value_t v;
    v.u16 = atomic_load_explicit(&g_config.wifi.timeout_sec, memory_order_relaxed);
    return v;
}

static void set_wifi_timeout_sec(param_value_t v) {
    atomic_store_explicit(&g_config.wifi.timeout_sec, v.u16, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_wifi_use_static_ip(void) {
    param_value_t v;
    v.b = atomic_load_explicit(&g_config.wifi.use_static_ip, memory_order_relaxed);
    return v;
}

static void set_wifi_use_static_ip(param_value_t v) {
    atomic_store_explicit(&g_config.wifi.use_static_ip, v.b, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_wifi_ip_address(void) {
    param_value_t v;
    v.str = g_config.wifi.ip_address;
    return v;
}

static void set_wifi_ip_address(param_value_t v) {
    strncpy(g_config.wifi.ip_address, v.str, 16);
    g_config.wifi.ip_address[16] = '\0';
    config_bump_generation(&g_config);
}

static param_value_t get_wifi_netmask(void) {
    param_value_t v;
    v.str = g_config.wifi.netmask;
    return v;
}

static void set_wifi_netmask(param_value_t v) {
    strncpy(g_config.wifi.netmask, v.str, 16);
    g_config.wifi.netmask[16] = '\0';
    config_bump_generation(&g_config);
}

static param_value_t get_wifi_gateway(void) {
    param_value_t v;
    v.str = g_config.wifi.gateway;
    return v;
}

static void set_wifi_gateway(param_value_t v) {
    strncpy(g_config.wifi.gateway, v.str, 16);
    g_config.wifi.gateway[16] = '\0';
    config_bump_generation(&g_config);
}

static param_value_t get_wifi_dns(void) {
    param_value_t v;
    v.str = g_config.wifi.dns;
    return v;
}

static void set_wifi_dns(param_value_t v) {
    strncpy(g_config.wifi.dns, v.str, 16);
    g_config.wifi.dns[16] = '\0';
    config_bump_generation(&g_config);
}

static param_value_t get_remote_cwnet_enabled(void) {
    param_value_t v;
    v.b = atomic_load_explicit(&g_config.remote.cwnet_enabled, memory_order_relaxed);
    return v;
}

static void set_remote_cwnet_enabled(param_value_t v) {
    atomic_store_explicit(&g_config.remote.cwnet_enabled, v.b, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_remote_server_host(void) {
    param_value_t v;
    v.str = g_config.remote.server_host;
    return v;
}

static void set_remote_server_host(param_value_t v) {
    strncpy(g_config.remote.server_host, v.str, 64);
    g_config.remote.server_host[64] = '\0';
    config_bump_generation(&g_config);
}

static param_value_t get_remote_server_port(void) {
    param_value_t v;
    v.u16 = atomic_load_explicit(&g_config.remote.server_port, memory_order_relaxed);
    return v;
}

static void set_remote_server_port(param_value_t v) {
    atomic_store_explicit(&g_config.remote.server_port, v.u16, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_remote_username(void) {
    param_value_t v;
    v.str = g_config.remote.username;
    return v;
}

static void set_remote_username(param_value_t v) {
    strncpy(g_config.remote.username, v.str, 32);
    g_config.remote.username[32] = '\0';
    config_bump_generation(&g_config);
}

/* ============================================================================
 * Parameter Registry
 * ============================================================================ */

const param_descriptor_t CONSOLE_PARAMS[CONSOLE_PARAM_COUNT] = {
    { "wpm", "keyer", "keyer.wpm", PARAM_TYPE_U16, 5, 100, get_keyer_wpm, set_keyer_wpm },
    { "iambic_mode", "keyer", "keyer.iambic_mode", PARAM_TYPE_ENUM, 0, 1, get_keyer_iambic_mode, set_keyer_iambic_mode },
    { "memory_mode", "keyer", "keyer.memory_mode", PARAM_TYPE_ENUM, 0, 3, get_keyer_memory_mode, set_keyer_memory_mode },
    { "squeeze_mode", "keyer", "keyer.squeeze_mode", PARAM_TYPE_ENUM, 0, 1, get_keyer_squeeze_mode, set_keyer_squeeze_mode },
    { "weight", "keyer", "keyer.weight", PARAM_TYPE_U8, 33, 67, get_keyer_weight, set_keyer_weight },
    { "mem_window_start_pct", "keyer", "keyer.mem_window_start_pct", PARAM_TYPE_U8, 0, 100, get_keyer_mem_window_start_pct, set_keyer_mem_window_start_pct },
    { "mem_window_end_pct", "keyer", "keyer.mem_window_end_pct", PARAM_TYPE_U8, 0, 100, get_keyer_mem_window_end_pct, set_keyer_mem_window_end_pct },
    { "sidetone_freq_hz", "audio", "audio.sidetone_freq_hz", PARAM_TYPE_U16, 400, 800, get_audio_sidetone_freq_hz, set_audio_sidetone_freq_hz },
    { "sidetone_volume", "audio", "audio.sidetone_volume", PARAM_TYPE_U8, 1, 100, get_audio_sidetone_volume, set_audio_sidetone_volume },
    { "fade_duration_ms", "audio", "audio.fade_duration_ms", PARAM_TYPE_U8, 1, 10, get_audio_fade_duration_ms, set_audio_fade_duration_ms },
    { "gpio_dit", "hardware", "hardware.gpio_dit", PARAM_TYPE_U8, 0, 45, get_hardware_gpio_dit, set_hardware_gpio_dit },
    { "gpio_dah", "hardware", "hardware.gpio_dah", PARAM_TYPE_U8, 0, 45, get_hardware_gpio_dah, set_hardware_gpio_dah },
    { "gpio_tx", "hardware", "hardware.gpio_tx", PARAM_TYPE_U8, 0, 45, get_hardware_gpio_tx, set_hardware_gpio_tx },
    { "ptt_tail_ms", "timing", "timing.ptt_tail_ms", PARAM_TYPE_U32, 50, 500, get_timing_ptt_tail_ms, set_timing_ptt_tail_ms },
    { "tick_rate_hz", "timing", "timing.tick_rate_hz", PARAM_TYPE_U32, 1000, 10000, get_timing_tick_rate_hz, set_timing_tick_rate_hz },
    { "debug_logging", "system", "system.debug_logging", PARAM_TYPE_BOOL, 0, 1, get_system_debug_logging, set_system_debug_logging },
    { "callsign", "system", "system.callsign", PARAM_TYPE_STRING, 0, 12, get_system_callsign, set_system_callsign },
    { "ui_theme", "system", "system.ui_theme", PARAM_TYPE_ENUM, 0, 3, get_system_ui_theme, set_system_ui_theme },
    { "gpio_data", "leds", "leds.gpio_data", PARAM_TYPE_U8, 0, 48, get_leds_gpio_data, set_leds_gpio_data },
    { "count", "leds", "leds.count", PARAM_TYPE_U8, 0, 32, get_leds_count, set_leds_count },
    { "brightness", "leds", "leds.brightness", PARAM_TYPE_U8, 0, 100, get_leds_brightness, set_leds_brightness },
    { "brightness_dim", "leds", "leds.brightness_dim", PARAM_TYPE_U8, 0, 50, get_leds_brightness_dim, set_leds_brightness_dim },
    { "enabled", "wifi", "wifi.enabled", PARAM_TYPE_BOOL, 0, 1, get_wifi_enabled, set_wifi_enabled },
    { "ssid", "wifi", "wifi.ssid", PARAM_TYPE_STRING, 0, 32, get_wifi_ssid, set_wifi_ssid },
    { "password", "wifi", "wifi.password", PARAM_TYPE_STRING, 0, 64, get_wifi_password, set_wifi_password },
    { "timeout_sec", "wifi", "wifi.timeout_sec", PARAM_TYPE_U16, 5, 120, get_wifi_timeout_sec, set_wifi_timeout_sec },
    { "use_static_ip", "wifi", "wifi.use_static_ip", PARAM_TYPE_BOOL, 0, 1, get_wifi_use_static_ip, set_wifi_use_static_ip },
    { "ip_address", "wifi", "wifi.ip_address", PARAM_TYPE_STRING, 0, 16, get_wifi_ip_address, set_wifi_ip_address },
    { "netmask", "wifi", "wifi.netmask", PARAM_TYPE_STRING, 0, 16, get_wifi_netmask, set_wifi_netmask },
    { "gateway", "wifi", "wifi.gateway", PARAM_TYPE_STRING, 0, 16, get_wifi_gateway, set_wifi_gateway },
    { "dns", "wifi", "wifi.dns", PARAM_TYPE_STRING, 0, 16, get_wifi_dns, set_wifi_dns },
    { "cwnet_enabled", "remote", "remote.cwnet_enabled", PARAM_TYPE_BOOL, 0, 1, get_remote_cwnet_enabled, set_remote_cwnet_enabled },
    { "server_host", "remote", "remote.server_host", PARAM_TYPE_STRING, 0, 64, get_remote_server_host, set_remote_server_host },
    { "server_port", "remote", "remote.server_port", PARAM_TYPE_U16, 1, 65535, get_remote_server_port, set_remote_server_port },
    { "username", "remote", "remote.username", PARAM_TYPE_STRING, 0, 32, get_remote_username, set_remote_username },
};

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

const family_descriptor_t *config_find_family(const char *name) {
    if (name == NULL) return NULL;

    for (int i = 0; i < FAMILY_COUNT; i++) {
        const family_descriptor_t *f = &CONSOLE_FAMILIES[i];
        /* Check exact name match */
        if (strcmp(f->name, name) == 0) {
            return f;
        }
        /* Check aliases */
        if (f->aliases && f->aliases[0] != '\0') {
            char aliases_copy[64];
            strncpy(aliases_copy, f->aliases, sizeof(aliases_copy) - 1);
            aliases_copy[sizeof(aliases_copy) - 1] = '\0';
            char *alias = strtok(aliases_copy, ",");
            while (alias) {
                if (strcmp(alias, name) == 0) {
                    return f;
                }
                alias = strtok(NULL, ",");
            }
        }
    }
    return NULL;
}

const param_descriptor_t *config_find_param(const char *name) {
    if (name == NULL) return NULL;

    for (int i = 0; i < CONSOLE_PARAM_COUNT; i++) {
        const param_descriptor_t *p = &CONSOLE_PARAMS[i];
        /* Check full path (e.g., "keyer.wpm") */
        if (strcmp(p->full_path, name) == 0) {
            return p;
        }
        /* Check short name (e.g., "wpm") */
        if (strcmp(p->name, name) == 0) {
            return p;
        }
    }
    return NULL;
}

int config_get_param_str(const char *name, char *buf, size_t len) {
    const param_descriptor_t *p = config_find_param(name);
    if (p == NULL || buf == NULL || len == 0) {
        return -1;
    }

    param_value_t v = p->get_fn();

    switch (p->type) {
        case PARAM_TYPE_U8:
        case PARAM_TYPE_ENUM:
            snprintf(buf, len, "%u", v.u8);
            break;
        case PARAM_TYPE_U16:
            snprintf(buf, len, "%u", v.u16);
            break;
        case PARAM_TYPE_U32:
            snprintf(buf, len, "%lu", (unsigned long)v.u32);
            break;
        case PARAM_TYPE_BOOL:
            snprintf(buf, len, "%s", v.b ? "true" : "false");
            break;
        case PARAM_TYPE_STRING:
            snprintf(buf, len, "%s", v.str ? v.str : "");
            break;
        default:
            return -1;
    }
    return 0;
}

int config_set_param_str(const char *name, const char *value) {
    const param_descriptor_t *p = config_find_param(name);
    if (p == NULL || value == NULL) {
        return -1;
    }

    param_value_t v;
    unsigned long parsed;

    switch (p->type) {
        case PARAM_TYPE_U8:
        case PARAM_TYPE_ENUM:
            parsed = strtoul(value, NULL, 0);
            if (parsed < p->min || parsed > p->max) {
                return -2;  /* Out of range */
            }
            v.u8 = (uint8_t)parsed;
            break;
        case PARAM_TYPE_U16:
            parsed = strtoul(value, NULL, 0);
            if (parsed < p->min || parsed > p->max) {
                return -2;
            }
            v.u16 = (uint16_t)parsed;
            break;
        case PARAM_TYPE_U32:
            parsed = strtoul(value, NULL, 0);
            if (parsed < p->min || parsed > p->max) {
                return -2;
            }
            v.u32 = (uint32_t)parsed;
            break;
        case PARAM_TYPE_BOOL:
            if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
                strcmp(value, "on") == 0 || strcmp(value, "yes") == 0) {
                v.b = true;
            } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 ||
                       strcmp(value, "off") == 0 || strcmp(value, "no") == 0) {
                v.b = false;
            } else {
                return -3;  /* Invalid boolean */
            }
            break;
        case PARAM_TYPE_STRING:
            v.str = value;
            break;
        default:
            return -1;
    }

    p->set_fn(v);
    return 0;
}

void config_foreach_matching(const char *pattern, param_visitor_fn visitor, void *ctx) {
    if (pattern == NULL || visitor == NULL) return;

    /* Check for wildcard patterns */
    const char *star = strchr(pattern, '*');

    if (star == NULL) {
        /* Exact match */
        const param_descriptor_t *p = config_find_param(pattern);
        if (p != NULL) {
            visitor(p, ctx);
        }
        return;
    }

    /* Get prefix (everything before the wildcard) */
    size_t prefix_len = (size_t)(star - pattern);
    char prefix[64];
    if (prefix_len >= sizeof(prefix)) {
        prefix_len = sizeof(prefix) - 1;
    }
    strncpy(prefix, pattern, prefix_len);
    prefix[prefix_len] = '\0';

    /* Remove trailing dot from prefix if present */
    if (prefix_len > 0 && prefix[prefix_len - 1] == '.') {
        prefix[prefix_len - 1] = '\0';
        prefix_len--;
    }

    /* Check if double-star (recursive) */
    bool recursive = (star[1] == '*');

    /* Iterate all params */
    for (int i = 0; i < CONSOLE_PARAM_COUNT; i++) {
        const param_descriptor_t *p = &CONSOLE_PARAMS[i];

        /* Check if family matches prefix */
        if (prefix_len == 0) {
            /* Empty prefix matches all */
            visitor(p, ctx);
        } else if (strncmp(p->full_path, prefix, prefix_len) == 0) {
            /* Prefix matches */
            if (recursive) {
                /* ** matches all under this family */
                visitor(p, ctx);
            } else {
                /* * only matches direct children (no more dots after prefix) */
                const char *rest = p->full_path + prefix_len;
                if (rest[0] == '.') rest++;  /* Skip the dot */
                if (strchr(rest, '.') == NULL) {
                    visitor(p, ctx);
                }
            }
        }
    }
}
