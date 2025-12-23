/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config.h
 * @brief Atomic configuration struct for keyer_c
 *
 * All parameters use atomic types for lock-free access from
 * multiple threads (RT thread, UI thread, WiFi thread, etc.)
 */

#ifndef KEYER_CONFIG_H
#define KEYER_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Global keyer configuration with atomic access
 *
 * Change detection via `generation` counter:
 * - Increment `generation` when any parameter changes
 * - Consumers check `generation` to detect updates
 */
typedef struct {
    atomic_ushort wpm;  /**< Words Per Minute (5-100) */
    atomic_uchar iambic_mode;  /**< Iambic Mode */
    atomic_uchar memory_mode;  /**< Memory Mode */
    atomic_uchar squeeze_mode;  /**< Squeeze Timing */
    atomic_uchar mem_window_start_pct;  /**< Memory Window Start (%) (0-100) */
    atomic_uchar mem_window_end_pct;  /**< Memory Window End (%) (0-100) */
    atomic_uchar weight;  /**< Dit/Dah Weight (33-67) */
    atomic_ushort sidetone_freq_hz;  /**< Sidetone Frequency (Hz) (400-800) */
    atomic_uchar sidetone_volume;  /**< Sidetone Volume (%) (1-100) */
    atomic_uchar fade_duration_ms;  /**< Fade Duration (ms) (1-10) */
    atomic_uchar gpio_dit;  /**< DIT Paddle GPIO (0-45) */
    atomic_uchar gpio_dah;  /**< DAH Paddle GPIO (0-45) */
    atomic_uchar gpio_tx;  /**< TX Output GPIO (0-45) */
    atomic_uint ptt_tail_ms;  /**< PTT Tail Timeout (ms) (50-500) */
    atomic_uint tick_rate_hz;  /**< RT Loop Tick Rate (Hz) (1000-10000) */
    atomic_bool debug_logging;  /**< Debug Logging */
    atomic_uchar led_brightness;  /**< LED Brightness (%) (0-100) */
    atomic_ushort generation;  /**< Config generation counter */
} keyer_config_t;

/** Global configuration instance */
extern keyer_config_t g_config;

/**
 * @brief Initialize configuration with default values
 * @param cfg Configuration to initialize
 */
void config_init_defaults(keyer_config_t *cfg);

/**
 * @brief Increment generation counter to signal config change
 * @param cfg Configuration
 */
void config_bump_generation(keyer_config_t *cfg);

/* ============================================================================
 * Parameter Access Macros
 * ============================================================================ */

#define CONFIG_GET_WPM() \
    atomic_load_explicit(&g_config.wpm, memory_order_relaxed)

#define CONFIG_SET_WPM(v) do { \
    atomic_store_explicit(&g_config.wpm, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_IAMBIC_MODE() \
    atomic_load_explicit(&g_config.iambic_mode, memory_order_relaxed)

#define CONFIG_SET_IAMBIC_MODE(v) do { \
    atomic_store_explicit(&g_config.iambic_mode, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_MEMORY_MODE() \
    atomic_load_explicit(&g_config.memory_mode, memory_order_relaxed)

#define CONFIG_SET_MEMORY_MODE(v) do { \
    atomic_store_explicit(&g_config.memory_mode, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_SQUEEZE_MODE() \
    atomic_load_explicit(&g_config.squeeze_mode, memory_order_relaxed)

#define CONFIG_SET_SQUEEZE_MODE(v) do { \
    atomic_store_explicit(&g_config.squeeze_mode, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_MEM_WINDOW_START_PCT() \
    atomic_load_explicit(&g_config.mem_window_start_pct, memory_order_relaxed)

#define CONFIG_SET_MEM_WINDOW_START_PCT(v) do { \
    atomic_store_explicit(&g_config.mem_window_start_pct, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_MEM_WINDOW_END_PCT() \
    atomic_load_explicit(&g_config.mem_window_end_pct, memory_order_relaxed)

#define CONFIG_SET_MEM_WINDOW_END_PCT(v) do { \
    atomic_store_explicit(&g_config.mem_window_end_pct, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_WEIGHT() \
    atomic_load_explicit(&g_config.weight, memory_order_relaxed)

#define CONFIG_SET_WEIGHT(v) do { \
    atomic_store_explicit(&g_config.weight, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_SIDETONE_FREQ_HZ() \
    atomic_load_explicit(&g_config.sidetone_freq_hz, memory_order_relaxed)

#define CONFIG_SET_SIDETONE_FREQ_HZ(v) do { \
    atomic_store_explicit(&g_config.sidetone_freq_hz, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_SIDETONE_VOLUME() \
    atomic_load_explicit(&g_config.sidetone_volume, memory_order_relaxed)

#define CONFIG_SET_SIDETONE_VOLUME(v) do { \
    atomic_store_explicit(&g_config.sidetone_volume, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_FADE_DURATION_MS() \
    atomic_load_explicit(&g_config.fade_duration_ms, memory_order_relaxed)

#define CONFIG_SET_FADE_DURATION_MS(v) do { \
    atomic_store_explicit(&g_config.fade_duration_ms, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_GPIO_DIT() \
    atomic_load_explicit(&g_config.gpio_dit, memory_order_relaxed)

#define CONFIG_SET_GPIO_DIT(v) do { \
    atomic_store_explicit(&g_config.gpio_dit, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_GPIO_DAH() \
    atomic_load_explicit(&g_config.gpio_dah, memory_order_relaxed)

#define CONFIG_SET_GPIO_DAH(v) do { \
    atomic_store_explicit(&g_config.gpio_dah, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_GPIO_TX() \
    atomic_load_explicit(&g_config.gpio_tx, memory_order_relaxed)

#define CONFIG_SET_GPIO_TX(v) do { \
    atomic_store_explicit(&g_config.gpio_tx, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_PTT_TAIL_MS() \
    atomic_load_explicit(&g_config.ptt_tail_ms, memory_order_relaxed)

#define CONFIG_SET_PTT_TAIL_MS(v) do { \
    atomic_store_explicit(&g_config.ptt_tail_ms, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_TICK_RATE_HZ() \
    atomic_load_explicit(&g_config.tick_rate_hz, memory_order_relaxed)

#define CONFIG_SET_TICK_RATE_HZ(v) do { \
    atomic_store_explicit(&g_config.tick_rate_hz, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_DEBUG_LOGGING() \
    atomic_load_explicit(&g_config.debug_logging, memory_order_relaxed)

#define CONFIG_SET_DEBUG_LOGGING(v) do { \
    atomic_store_explicit(&g_config.debug_logging, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_LED_BRIGHTNESS() \
    atomic_load_explicit(&g_config.led_brightness, memory_order_relaxed)

#define CONFIG_SET_LED_BRIGHTNESS(v) do { \
    atomic_store_explicit(&g_config.led_brightness, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_H */
