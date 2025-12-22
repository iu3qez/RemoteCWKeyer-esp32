/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config_console.c
 * @brief Console command parameter registry implementation
 */

#include "config_console.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Get/set function implementations */
static param_value_t get_wpm(void) {
    param_value_t v;
    v.u16 = atomic_load_explicit(&g_config.wpm, memory_order_relaxed);
    return v;
}
static void set_wpm(param_value_t v) {
    atomic_store_explicit(&g_config.wpm, v.u16, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_iambic_mode(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.iambic_mode, memory_order_relaxed);
    return v;
}
static void set_iambic_mode(param_value_t v) {
    atomic_store_explicit(&g_config.iambic_mode, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_memory_window_us(void) {
    param_value_t v;
    v.u32 = atomic_load_explicit(&g_config.memory_window_us, memory_order_relaxed);
    return v;
}
static void set_memory_window_us(param_value_t v) {
    atomic_store_explicit(&g_config.memory_window_us, v.u32, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_weight(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.weight, memory_order_relaxed);
    return v;
}
static void set_weight(param_value_t v) {
    atomic_store_explicit(&g_config.weight, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_sidetone_freq_hz(void) {
    param_value_t v;
    v.u16 = atomic_load_explicit(&g_config.sidetone_freq_hz, memory_order_relaxed);
    return v;
}
static void set_sidetone_freq_hz(param_value_t v) {
    atomic_store_explicit(&g_config.sidetone_freq_hz, v.u16, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_sidetone_volume(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.sidetone_volume, memory_order_relaxed);
    return v;
}
static void set_sidetone_volume(param_value_t v) {
    atomic_store_explicit(&g_config.sidetone_volume, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_fade_duration_ms(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.fade_duration_ms, memory_order_relaxed);
    return v;
}
static void set_fade_duration_ms(param_value_t v) {
    atomic_store_explicit(&g_config.fade_duration_ms, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_gpio_dit(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.gpio_dit, memory_order_relaxed);
    return v;
}
static void set_gpio_dit(param_value_t v) {
    atomic_store_explicit(&g_config.gpio_dit, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_gpio_dah(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.gpio_dah, memory_order_relaxed);
    return v;
}
static void set_gpio_dah(param_value_t v) {
    atomic_store_explicit(&g_config.gpio_dah, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_gpio_tx(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.gpio_tx, memory_order_relaxed);
    return v;
}
static void set_gpio_tx(param_value_t v) {
    atomic_store_explicit(&g_config.gpio_tx, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_ptt_tail_ms(void) {
    param_value_t v;
    v.u32 = atomic_load_explicit(&g_config.ptt_tail_ms, memory_order_relaxed);
    return v;
}
static void set_ptt_tail_ms(param_value_t v) {
    atomic_store_explicit(&g_config.ptt_tail_ms, v.u32, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_tick_rate_hz(void) {
    param_value_t v;
    v.u32 = atomic_load_explicit(&g_config.tick_rate_hz, memory_order_relaxed);
    return v;
}
static void set_tick_rate_hz(param_value_t v) {
    atomic_store_explicit(&g_config.tick_rate_hz, v.u32, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_debug_logging(void) {
    param_value_t v;
    v.b = atomic_load_explicit(&g_config.debug_logging, memory_order_relaxed);
    return v;
}
static void set_debug_logging(param_value_t v) {
    atomic_store_explicit(&g_config.debug_logging, v.b, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_led_brightness(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.led_brightness, memory_order_relaxed);
    return v;
}
static void set_led_brightness(param_value_t v) {
    atomic_store_explicit(&g_config.led_brightness, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

/* Parameter registry */
const param_descriptor_t CONSOLE_PARAMS[CONSOLE_PARAM_COUNT] = {
    { "wpm", "keyer", PARAM_TYPE_U16, 5, 100, get_wpm, set_wpm },
    { "iambic_mode", "keyer", PARAM_TYPE_ENUM, 0, 1, get_iambic_mode, set_iambic_mode },
    { "memory_window_us", "keyer", PARAM_TYPE_U32, 0, 1000, get_memory_window_us, set_memory_window_us },
    { "weight", "keyer", PARAM_TYPE_U8, 33, 67, get_weight, set_weight },
    { "sidetone_freq_hz", "audio", PARAM_TYPE_U16, 400, 800, get_sidetone_freq_hz, set_sidetone_freq_hz },
    { "sidetone_volume", "audio", PARAM_TYPE_U8, 1, 100, get_sidetone_volume, set_sidetone_volume },
    { "fade_duration_ms", "audio", PARAM_TYPE_U8, 1, 10, get_fade_duration_ms, set_fade_duration_ms },
    { "gpio_dit", "hardware", PARAM_TYPE_U8, 0, 45, get_gpio_dit, set_gpio_dit },
    { "gpio_dah", "hardware", PARAM_TYPE_U8, 0, 45, get_gpio_dah, set_gpio_dah },
    { "gpio_tx", "hardware", PARAM_TYPE_U8, 0, 45, get_gpio_tx, set_gpio_tx },
    { "ptt_tail_ms", "timing", PARAM_TYPE_U32, 50, 500, get_ptt_tail_ms, set_ptt_tail_ms },
    { "tick_rate_hz", "timing", PARAM_TYPE_U32, 1000, 10000, get_tick_rate_hz, set_tick_rate_hz },
    { "debug_logging", "system", PARAM_TYPE_BOOL, 0, 1, get_debug_logging, set_debug_logging },
    { "led_brightness", "system", PARAM_TYPE_U8, 0, 100, get_led_brightness, set_led_brightness },
};

const char *CATEGORIES[CATEGORY_COUNT] = {
    "keyer", "audio", "hardware", "timing", "system"
};

const param_descriptor_t *config_find_param(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < CONSOLE_PARAM_COUNT; i++) {
        if (strcmp(CONSOLE_PARAMS[i].name, name) == 0) {
            return &CONSOLE_PARAMS[i];
        }
    }
    return NULL;
}

int config_get_param_str(const char *name, char *buf, size_t len) {
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
    }
    return 0;
}

int config_set_param_str(const char *name, const char *value) {
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
            ul = strtoul(value, &endptr, 10);
            if (*endptr != '\0') {
                return -2;  /* Invalid value */
            }
            if (ul < p->min || ul > p->max) {
                return -4;  /* Out of range */
            }
            v.u8 = (uint8_t)ul;
            break;

        case PARAM_TYPE_U16:
            ul = strtoul(value, &endptr, 10);
            if (*endptr != '\0') {
                return -2;
            }
            if (ul < p->min || ul > p->max) {
                return -4;
            }
            v.u16 = (uint16_t)ul;
            break;

        case PARAM_TYPE_U32:
            ul = strtoul(value, &endptr, 10);
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
    }

    p->set_fn(v);
    return 0;
}
