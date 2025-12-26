/**
 * @file config_console.c
 * @brief Console command parameter registry implementation
 *
 * Parameter descriptors are generated from parameters.yaml,
 * but get/set string functions are hand-written defensive code.
 */

#include "config_console.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* Family registry */
const family_descriptor_t CONSOLE_FAMILIES[FAMILY_COUNT] = {
    { "keyer", "k", "Keying behavior and timing", 1 },
    { "audio", "a,snd", "Sidetone and audio output", 2 },
    { "hardware", "hw,gpio", "GPIO and pin configuration", 3 },
    { "timing", "t", "RT loop and PTT timing", 4 },
    { "system", "sys", "Debug and system settings", 5 },
    { "leds", "led,l", "RGB LED strip configuration", 6 },
    { "wifi", "w,net", "Wireless network configuration", 7 },
};

/* Get/set function implementations - using nested family paths */

/* keyer family */
static param_value_t get_wpm(void) {
    param_value_t v;
    v.u16 = atomic_load_explicit(&g_config.keyer.wpm, memory_order_relaxed);
    return v;
}
static void set_wpm(param_value_t v) {
    atomic_store_explicit(&g_config.keyer.wpm, v.u16, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_iambic_mode(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.keyer.iambic_mode, memory_order_relaxed);
    return v;
}
static void set_iambic_mode(param_value_t v) {
    atomic_store_explicit(&g_config.keyer.iambic_mode, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_memory_mode(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.keyer.memory_mode, memory_order_relaxed);
    return v;
}
static void set_memory_mode(param_value_t v) {
    if (v.u8 <= 3) {  /* 0=NONE, 1=DOT_ONLY, 2=DAH_ONLY, 3=DOT_AND_DAH */
        atomic_store_explicit(&g_config.keyer.memory_mode, v.u8, memory_order_relaxed);
        config_bump_generation(&g_config);
    }
}

static param_value_t get_squeeze_mode(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.keyer.squeeze_mode, memory_order_relaxed);
    return v;
}
static void set_squeeze_mode(param_value_t v) {
    if (v.u8 <= 1) {  /* 0=LATCH_OFF, 1=LATCH_ON */
        atomic_store_explicit(&g_config.keyer.squeeze_mode, v.u8, memory_order_relaxed);
        config_bump_generation(&g_config);
    }
}

static param_value_t get_weight(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.keyer.weight, memory_order_relaxed);
    return v;
}
static void set_weight(param_value_t v) {
    atomic_store_explicit(&g_config.keyer.weight, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_mem_window_start_pct(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.keyer.mem_window_start_pct, memory_order_relaxed);
    return v;
}
static void set_mem_window_start_pct(param_value_t v) {
    if (v.u8 <= 100) {
        atomic_store_explicit(&g_config.keyer.mem_window_start_pct, v.u8, memory_order_relaxed);
        config_bump_generation(&g_config);
    }
}

static param_value_t get_mem_window_end_pct(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.keyer.mem_window_end_pct, memory_order_relaxed);
    return v;
}
static void set_mem_window_end_pct(param_value_t v) {
    if (v.u8 <= 100) {
        atomic_store_explicit(&g_config.keyer.mem_window_end_pct, v.u8, memory_order_relaxed);
        config_bump_generation(&g_config);
    }
}

/* audio family */
static param_value_t get_sidetone_freq_hz(void) {
    param_value_t v;
    v.u16 = atomic_load_explicit(&g_config.audio.sidetone_freq_hz, memory_order_relaxed);
    return v;
}
static void set_sidetone_freq_hz(param_value_t v) {
    atomic_store_explicit(&g_config.audio.sidetone_freq_hz, v.u16, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_sidetone_volume(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.audio.sidetone_volume, memory_order_relaxed);
    return v;
}
static void set_sidetone_volume(param_value_t v) {
    atomic_store_explicit(&g_config.audio.sidetone_volume, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_fade_duration_ms(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.audio.fade_duration_ms, memory_order_relaxed);
    return v;
}
static void set_fade_duration_ms(param_value_t v) {
    atomic_store_explicit(&g_config.audio.fade_duration_ms, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

/* hardware family */
static param_value_t get_gpio_dit(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.hardware.gpio_dit, memory_order_relaxed);
    return v;
}
static void set_gpio_dit(param_value_t v) {
    atomic_store_explicit(&g_config.hardware.gpio_dit, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_gpio_dah(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.hardware.gpio_dah, memory_order_relaxed);
    return v;
}
static void set_gpio_dah(param_value_t v) {
    atomic_store_explicit(&g_config.hardware.gpio_dah, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_gpio_tx(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.hardware.gpio_tx, memory_order_relaxed);
    return v;
}
static void set_gpio_tx(param_value_t v) {
    atomic_store_explicit(&g_config.hardware.gpio_tx, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

/* timing family */
static param_value_t get_ptt_tail_ms(void) {
    param_value_t v;
    v.u32 = atomic_load_explicit(&g_config.timing.ptt_tail_ms, memory_order_relaxed);
    return v;
}
static void set_ptt_tail_ms(param_value_t v) {
    atomic_store_explicit(&g_config.timing.ptt_tail_ms, v.u32, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_tick_rate_hz(void) {
    param_value_t v;
    v.u32 = atomic_load_explicit(&g_config.timing.tick_rate_hz, memory_order_relaxed);
    return v;
}
static void set_tick_rate_hz(param_value_t v) {
    atomic_store_explicit(&g_config.timing.tick_rate_hz, v.u32, memory_order_relaxed);
    config_bump_generation(&g_config);
}

/* system family */
static param_value_t get_debug_logging(void) {
    param_value_t v;
    v.b = atomic_load_explicit(&g_config.system.debug_logging, memory_order_relaxed);
    return v;
}
static void set_debug_logging(param_value_t v) {
    atomic_store_explicit(&g_config.system.debug_logging, v.b, memory_order_relaxed);
    config_bump_generation(&g_config);
}

/* leds family */
static param_value_t get_led_gpio_data(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.leds.gpio_data, memory_order_relaxed);
    return v;
}
static void set_led_gpio_data(param_value_t v) {
    atomic_store_explicit(&g_config.leds.gpio_data, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_led_count(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.leds.count, memory_order_relaxed);
    return v;
}
static void set_led_count(param_value_t v) {
    atomic_store_explicit(&g_config.leds.count, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_led_brightness(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.leds.brightness, memory_order_relaxed);
    return v;
}
static void set_led_brightness(param_value_t v) {
    atomic_store_explicit(&g_config.leds.brightness, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_led_brightness_dim(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.leds.brightness_dim, memory_order_relaxed);
    return v;
}
static void set_led_brightness_dim(param_value_t v) {
    atomic_store_explicit(&g_config.leds.brightness_dim, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

/* wifi family */
static param_value_t get_wifi_enabled(void) {
    param_value_t v;
    v.b = atomic_load_explicit(&g_config.wifi.enabled, memory_order_relaxed);
    return v;
}
static void set_wifi_enabled(param_value_t v) {
    atomic_store_explicit(&g_config.wifi.enabled, v.b, memory_order_relaxed);
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

/* Parameter registry with full_path for dot notation */
const param_descriptor_t CONSOLE_PARAMS[CONSOLE_PARAM_COUNT] = {
    /* keyer family */
    { "wpm", "keyer", "keyer.wpm", PARAM_TYPE_U16, 5, 100, get_wpm, set_wpm },
    { "iambic_mode", "keyer", "keyer.iambic_mode", PARAM_TYPE_ENUM, 0, 1, get_iambic_mode, set_iambic_mode },
    { "memory_mode", "keyer", "keyer.memory_mode", PARAM_TYPE_ENUM, 0, 3, get_memory_mode, set_memory_mode },
    { "squeeze_mode", "keyer", "keyer.squeeze_mode", PARAM_TYPE_ENUM, 0, 1, get_squeeze_mode, set_squeeze_mode },
    { "weight", "keyer", "keyer.weight", PARAM_TYPE_U8, 33, 67, get_weight, set_weight },
    { "mem_window_start_pct", "keyer", "keyer.mem_window_start_pct", PARAM_TYPE_U8, 0, 100, get_mem_window_start_pct, set_mem_window_start_pct },
    { "mem_window_end_pct", "keyer", "keyer.mem_window_end_pct", PARAM_TYPE_U8, 0, 100, get_mem_window_end_pct, set_mem_window_end_pct },
    /* audio family */
    { "sidetone_freq_hz", "audio", "audio.sidetone_freq_hz", PARAM_TYPE_U16, 400, 800, get_sidetone_freq_hz, set_sidetone_freq_hz },
    { "sidetone_volume", "audio", "audio.sidetone_volume", PARAM_TYPE_U8, 1, 100, get_sidetone_volume, set_sidetone_volume },
    { "fade_duration_ms", "audio", "audio.fade_duration_ms", PARAM_TYPE_U8, 1, 10, get_fade_duration_ms, set_fade_duration_ms },
    /* hardware family */
    { "gpio_dit", "hardware", "hardware.gpio_dit", PARAM_TYPE_U8, 0, 45, get_gpio_dit, set_gpio_dit },
    { "gpio_dah", "hardware", "hardware.gpio_dah", PARAM_TYPE_U8, 0, 45, get_gpio_dah, set_gpio_dah },
    { "gpio_tx", "hardware", "hardware.gpio_tx", PARAM_TYPE_U8, 0, 45, get_gpio_tx, set_gpio_tx },
    /* timing family */
    { "ptt_tail_ms", "timing", "timing.ptt_tail_ms", PARAM_TYPE_U32, 50, 500, get_ptt_tail_ms, set_ptt_tail_ms },
    { "tick_rate_hz", "timing", "timing.tick_rate_hz", PARAM_TYPE_U32, 1000, 10000, get_tick_rate_hz, set_tick_rate_hz },
    /* system family */
    { "debug_logging", "system", "system.debug_logging", PARAM_TYPE_BOOL, 0, 1, get_debug_logging, set_debug_logging },
    /* leds family */
    { "gpio_data", "leds", "leds.gpio_data", PARAM_TYPE_U8, 0, 48, get_led_gpio_data, set_led_gpio_data },
    { "count", "leds", "leds.count", PARAM_TYPE_U8, 0, 32, get_led_count, set_led_count },
    { "brightness", "leds", "leds.brightness", PARAM_TYPE_U8, 0, 100, get_led_brightness, set_led_brightness },
    { "brightness_dim", "leds", "leds.brightness_dim", PARAM_TYPE_U8, 0, 50, get_led_brightness_dim, set_led_brightness_dim },
    /* wifi family (non-string params only - strings handled separately) */
    { "enabled", "wifi", "wifi.enabled", PARAM_TYPE_BOOL, 0, 1, get_wifi_enabled, set_wifi_enabled },
    { "timeout_sec", "wifi", "wifi.timeout_sec", PARAM_TYPE_U16, 5, 120, get_wifi_timeout_sec, set_wifi_timeout_sec },
    { "use_static_ip", "wifi", "wifi.use_static_ip", PARAM_TYPE_BOOL, 0, 1, get_wifi_use_static_ip, set_wifi_use_static_ip },
};

/* String parameter getters - direct return of internal buffer pointer */
const char *config_get_wifi_ssid(void) {
    return g_config.wifi.ssid;
}

const char *config_get_wifi_password(void) {
    return g_config.wifi.password;
}

const char *config_get_wifi_ip_address(void) {
    return g_config.wifi.ip_address;
}

const char *config_get_wifi_netmask(void) {
    return g_config.wifi.netmask;
}

const char *config_get_wifi_gateway(void) {
    return g_config.wifi.gateway;
}

const char *config_get_wifi_dns(void) {
    return g_config.wifi.dns;
}

/* String parameter setters - safe strncpy with null termination */
void config_set_wifi_ssid(const char *value) {
    if (value != NULL) {
        strncpy(g_config.wifi.ssid, value, 32);
        g_config.wifi.ssid[32] = '\0';
        config_bump_generation(&g_config);
    }
}

void config_set_wifi_password(const char *value) {
    if (value != NULL) {
        strncpy(g_config.wifi.password, value, 64);
        g_config.wifi.password[64] = '\0';
        config_bump_generation(&g_config);
    }
}

void config_set_wifi_ip_address(const char *value) {
    if (value != NULL) {
        strncpy(g_config.wifi.ip_address, value, 16);
        g_config.wifi.ip_address[16] = '\0';
        config_bump_generation(&g_config);
    }
}

void config_set_wifi_netmask(const char *value) {
    if (value != NULL) {
        strncpy(g_config.wifi.netmask, value, 16);
        g_config.wifi.netmask[16] = '\0';
        config_bump_generation(&g_config);
    }
}

void config_set_wifi_gateway(const char *value) {
    if (value != NULL) {
        strncpy(g_config.wifi.gateway, value, 16);
        g_config.wifi.gateway[16] = '\0';
        config_bump_generation(&g_config);
    }
}

void config_set_wifi_dns(const char *value) {
    if (value != NULL) {
        strncpy(g_config.wifi.dns, value, 16);
        g_config.wifi.dns[16] = '\0';
        config_bump_generation(&g_config);
    }
}

const family_descriptor_t *config_find_family(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < FAMILY_COUNT; i++) {
        const family_descriptor_t *f = &CONSOLE_FAMILIES[i];
        /* Match by name */
        if (strcmp(f->name, name) == 0) {
            return f;
        }
        /* Match by alias */
        const char *aliases = f->aliases;
        while (*aliases) {
            const char *end = aliases;
            while (*end && *end != ',') end++;
            size_t len = (size_t)(end - aliases);
            if (strncmp(aliases, name, len) == 0 && name[len] == '\0') {
                return f;
            }
            aliases = (*end == ',') ? end + 1 : end;
        }
    }
    return NULL;
}

const param_descriptor_t *config_find_param(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < CONSOLE_PARAM_COUNT; i++) {
        /* Match by full path (keyer.wpm) */
        if (strcmp(CONSOLE_PARAMS[i].full_path, name) == 0) {
            return &CONSOLE_PARAMS[i];
        }
        /* Also match by short name for backwards compat */
        if (strcmp(CONSOLE_PARAMS[i].name, name) == 0) {
            return &CONSOLE_PARAMS[i];
        }
    }
    return NULL;
}

/**
 * @brief Check if parameter is a string type (handled separately)
 */
static int handle_string_param_get(const char *name, char *buf, size_t len) {
    if (strcmp(name, "wifi.ssid") == 0 || strcmp(name, "ssid") == 0) {
        strncpy(buf, config_get_wifi_ssid(), len);
        buf[len - 1] = '\0';
        return 0;
    }
    if (strcmp(name, "wifi.password") == 0 || strcmp(name, "password") == 0) {
        /* Mask password for security */
        strncpy(buf, "********", len);
        buf[len - 1] = '\0';
        return 0;
    }
    if (strcmp(name, "wifi.ip_address") == 0 || strcmp(name, "ip_address") == 0) {
        strncpy(buf, config_get_wifi_ip_address(), len);
        buf[len - 1] = '\0';
        return 0;
    }
    if (strcmp(name, "wifi.netmask") == 0 || strcmp(name, "netmask") == 0) {
        strncpy(buf, config_get_wifi_netmask(), len);
        buf[len - 1] = '\0';
        return 0;
    }
    if (strcmp(name, "wifi.gateway") == 0 || strcmp(name, "gateway") == 0) {
        strncpy(buf, config_get_wifi_gateway(), len);
        buf[len - 1] = '\0';
        return 0;
    }
    if (strcmp(name, "wifi.dns") == 0 || strcmp(name, "dns") == 0) {
        strncpy(buf, config_get_wifi_dns(), len);
        buf[len - 1] = '\0';
        return 0;
    }
    return -1;  /* Not a string param */
}

static int handle_string_param_set(const char *name, const char *value) {
    if (strcmp(name, "wifi.ssid") == 0 || strcmp(name, "ssid") == 0) {
        config_set_wifi_ssid(value);
        return 0;
    }
    if (strcmp(name, "wifi.password") == 0 || strcmp(name, "password") == 0) {
        config_set_wifi_password(value);
        return 0;
    }
    if (strcmp(name, "wifi.ip_address") == 0 || strcmp(name, "ip_address") == 0) {
        config_set_wifi_ip_address(value);
        return 0;
    }
    if (strcmp(name, "wifi.netmask") == 0 || strcmp(name, "netmask") == 0) {
        config_set_wifi_netmask(value);
        return 0;
    }
    if (strcmp(name, "wifi.gateway") == 0 || strcmp(name, "gateway") == 0) {
        config_set_wifi_gateway(value);
        return 0;
    }
    if (strcmp(name, "wifi.dns") == 0 || strcmp(name, "dns") == 0) {
        config_set_wifi_dns(value);
        return 0;
    }
    return -1;  /* Not a string param */
}

int config_get_param_str(const char *name, char *buf, size_t len) {
    /* Issue 1 fix: Validate buffer pointer and length */
    if (buf == NULL || len == 0) {
        return -3;  /* Invalid buffer */
    }

    /* Try string params first */
    if (handle_string_param_get(name, buf, len) == 0) {
        return 0;
    }

    const param_descriptor_t *p = config_find_param(name);
    if (p == NULL) {
        return -1;  /* Unknown parameter */
    }

    param_value_t v = p->get_fn();

    switch (p->type) {
        case PARAM_TYPE_U8:
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
        case PARAM_TYPE_ENUM:
            snprintf(buf, len, "%u", v.u8);
            break;
        case PARAM_TYPE_STRING:
            /* Handled above */
            break;
    }
    return 0;
}

int config_set_param_str(const char *name, const char *value) {
    /* Issue 2 fix: Validate value pointer */
    if (value == NULL) {
        return -2;  /* Invalid value */
    }

    /* Try string params first */
    if (handle_string_param_set(name, value) == 0) {
        return 0;
    }

    const param_descriptor_t *p = config_find_param(name);
    if (p == NULL) {
        return -1;  /* Unknown parameter */
    }

    param_value_t v;
    char *endptr;
    unsigned long ul;

    switch (p->type) {
        case PARAM_TYPE_U8:
        case PARAM_TYPE_ENUM:
            /* Issue 3 fix: Detect integer overflow */
            errno = 0;
            ul = strtoul(value, &endptr, 10);
            if (errno == ERANGE) {
                return -2;  /* Invalid value (overflow) */
            }
            if (*endptr != '\0') {
                return -2;  /* Invalid value */
            }
            if (ul < p->min || ul > p->max) {
                return -4;  /* Out of range */
            }
            v.u8 = (uint8_t)ul;
            break;

        case PARAM_TYPE_U16:
            /* Issue 3 fix: Detect integer overflow */
            errno = 0;
            ul = strtoul(value, &endptr, 10);
            if (errno == ERANGE) {
                return -2;  /* Invalid value (overflow) */
            }
            if (*endptr != '\0') {
                return -2;
            }
            if (ul < p->min || ul > p->max) {
                return -4;
            }
            v.u16 = (uint16_t)ul;
            break;

        case PARAM_TYPE_U32:
            /* Issue 3 fix: Detect integer overflow */
            errno = 0;
            ul = strtoul(value, &endptr, 10);
            if (errno == ERANGE) {
                return -2;  /* Invalid value (overflow) */
            }
            if (*endptr != '\0') {
                return -2;
            }
            if (ul < p->min || ul > p->max) {
                return -4;
            }
            v.u32 = (uint32_t)ul;
            break;

        case PARAM_TYPE_BOOL:
            if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                v.b = true;
            } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
                v.b = false;
            } else {
                return -2;  /* Invalid value */
            }
            break;

        case PARAM_TYPE_STRING:
            /* Handled above */
            return -2;
    }

    p->set_fn(v);
    return 0;
}

/**
 * @brief Check if path matches pattern
 *
 * Patterns:
 * - "keyer.wpm"     exact match
 * - "keyer.*"       all direct params in keyer
 * - "keyer.**"      all params in keyer and subfamilies
 * - "**"            all params
 */
static bool path_matches(const char *path, const char *pattern) {
    size_t pat_len = strlen(pattern);

    /* "**" matches everything */
    if (strcmp(pattern, "**") == 0) {
        return true;
    }

    /* Check for ** (recursive) */
    if (pat_len >= 2 && strcmp(pattern + pat_len - 2, "**") == 0) {
        /* Match family prefix */
        return strncmp(path, pattern, pat_len - 2) == 0;
    }

    /* Check for * (single level) */
    if (pat_len >= 1 && pattern[pat_len - 1] == '*') {
        /* Match family.* */
        size_t prefix_len = pat_len - 1;
        if (strncmp(path, pattern, prefix_len) != 0) {
            return false;
        }
        /* Ensure no more dots after prefix (single level only) */
        const char *rest = path + prefix_len;
        return strchr(rest, '.') == NULL;
    }

    /* Exact match */
    return strcmp(path, pattern) == 0;
}

/**
 * @brief Expand alias in pattern
 *
 * "hw.*" -> "hardware.*"
 */
static void expand_pattern(const char *pattern, char *expanded, size_t len) {
    /* Find dot position */
    const char *dot = strchr(pattern, '.');
    if (dot == NULL) {
        strncpy(expanded, pattern, len);
        expanded[len - 1] = '\0';
        return;
    }

    /* Extract family part */
    size_t family_len = (size_t)(dot - pattern);
    char family_buf[32];
    if (family_len >= sizeof(family_buf)) {
        family_len = sizeof(family_buf) - 1;
    }
    memcpy(family_buf, pattern, family_len);
    family_buf[family_len] = '\0';

    /* Try to expand alias */
    const family_descriptor_t *f = config_find_family(family_buf);
    if (f != NULL) {
        snprintf(expanded, len, "%s%s", f->name, dot);
    } else {
        strncpy(expanded, pattern, len);
        expanded[len - 1] = '\0';
    }
}

void config_foreach_matching(const char *pattern, param_visitor_fn visitor, void *ctx) {
    if (pattern == NULL || visitor == NULL) {
        return;
    }

    /* Expand aliases */
    char expanded[64];
    expand_pattern(pattern, expanded, sizeof(expanded));

    /* Visit matching params */
    for (size_t i = 0; i < CONSOLE_PARAM_COUNT; i++) {
        const param_descriptor_t *p = &CONSOLE_PARAMS[i];
        if (path_matches(p->full_path, expanded)) {
            visitor(p, ctx);
        }
    }
}
