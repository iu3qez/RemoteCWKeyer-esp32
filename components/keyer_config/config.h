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
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Keyer configuration */
typedef struct {
    atomic_ushort wpm;  /**< Words Per Minute (5-100) */
    atomic_uchar iambic_mode;  /**< Iambic Mode */
    atomic_uchar memory_mode;  /**< Memory Mode */
    atomic_uchar squeeze_mode;  /**< Squeeze Timing */
    atomic_uchar weight;  /**< Dit/Dah Weight (33-67) */
    atomic_uchar mem_window_start_pct;  /**< Memory Window Start (%) (0-100) */
    atomic_uchar mem_window_end_pct;  /**< Memory Window End (%) (0-100) */
} config_keyer_t;

/** @brief Audio configuration */
typedef struct {
    atomic_ushort sidetone_freq_hz;  /**< Sidetone Frequency (Hz) (400-800) */
    atomic_uchar sidetone_volume;  /**< Sidetone Volume (%) (1-100) */
    atomic_uchar fade_duration_ms;  /**< Fade Duration (ms) (1-10) */
} config_audio_t;

/** @brief Hardware configuration */
typedef struct {
    atomic_uchar gpio_dit;  /**< DIT Paddle GPIO (0-45) */
    atomic_uchar gpio_dah;  /**< DAH Paddle GPIO (0-45) */
    atomic_uchar gpio_tx;  /**< TX Output GPIO (0-45) */
} config_hardware_t;

/** @brief Timing configuration */
typedef struct {
    atomic_uint ptt_tail_ms;  /**< PTT Tail Timeout (ms) (50-500) */
    atomic_uint tick_rate_hz;  /**< RT Loop Tick Rate (Hz) (1000-10000) */
} config_timing_t;

/** @brief System configuration */
typedef struct {
    atomic_bool debug_logging;  /**< Debug Logging */
    char callsign[13];  /**< Callsign */
} config_system_t;

/** @brief Leds configuration */
typedef struct {
    atomic_uchar gpio_data;  /**< LED Data GPIO (0-48) */
    atomic_uchar count;  /**< Number of LEDs (0-32) */
    atomic_uchar brightness;  /**< LED Brightness (%) (0-100) */
    atomic_uchar brightness_dim;  /**< Idle Brightness (%) (0-50) */
} config_leds_t;

/** @brief Wifi configuration */
typedef struct {
    atomic_bool enabled;  /**< WiFi Enabled */
    char ssid[33];  /**< Network Name */
    char password[65];  /**< Network Password */
    atomic_ushort timeout_sec;  /**< Connection Timeout (s) (5-120) */
    atomic_bool use_static_ip;  /**< Use Static IP */
    char ip_address[17];  /**< Static IP Address */
    char netmask[17];  /**< Subnet Mask */
    char gateway[17];  /**< Default Gateway */
    char dns[17];  /**< DNS Server */
} config_wifi_t;

/** @brief Complete keyer configuration */
typedef struct {
    config_keyer_t keyer;
    config_audio_t audio;
    config_hardware_t hardware;
    config_timing_t timing;
    config_system_t system;
    config_leds_t leds;
    config_wifi_t wifi;
    atomic_ushort generation;  /**< Config change counter */
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
    atomic_load_explicit(&g_config.keyer.wpm, memory_order_relaxed)

#define CONFIG_SET_WPM(v) do { \
    atomic_store_explicit(&g_config.keyer.wpm, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_IAMBIC_MODE() \
    atomic_load_explicit(&g_config.keyer.iambic_mode, memory_order_relaxed)

#define CONFIG_SET_IAMBIC_MODE(v) do { \
    atomic_store_explicit(&g_config.keyer.iambic_mode, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_MEMORY_MODE() \
    atomic_load_explicit(&g_config.keyer.memory_mode, memory_order_relaxed)

#define CONFIG_SET_MEMORY_MODE(v) do { \
    atomic_store_explicit(&g_config.keyer.memory_mode, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_SQUEEZE_MODE() \
    atomic_load_explicit(&g_config.keyer.squeeze_mode, memory_order_relaxed)

#define CONFIG_SET_SQUEEZE_MODE(v) do { \
    atomic_store_explicit(&g_config.keyer.squeeze_mode, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_WEIGHT() \
    atomic_load_explicit(&g_config.keyer.weight, memory_order_relaxed)

#define CONFIG_SET_WEIGHT(v) do { \
    atomic_store_explicit(&g_config.keyer.weight, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_MEM_WINDOW_START_PCT() \
    atomic_load_explicit(&g_config.keyer.mem_window_start_pct, memory_order_relaxed)

#define CONFIG_SET_MEM_WINDOW_START_PCT(v) do { \
    atomic_store_explicit(&g_config.keyer.mem_window_start_pct, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_MEM_WINDOW_END_PCT() \
    atomic_load_explicit(&g_config.keyer.mem_window_end_pct, memory_order_relaxed)

#define CONFIG_SET_MEM_WINDOW_END_PCT(v) do { \
    atomic_store_explicit(&g_config.keyer.mem_window_end_pct, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_SIDETONE_FREQ_HZ() \
    atomic_load_explicit(&g_config.audio.sidetone_freq_hz, memory_order_relaxed)

#define CONFIG_SET_SIDETONE_FREQ_HZ(v) do { \
    atomic_store_explicit(&g_config.audio.sidetone_freq_hz, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_SIDETONE_VOLUME() \
    atomic_load_explicit(&g_config.audio.sidetone_volume, memory_order_relaxed)

#define CONFIG_SET_SIDETONE_VOLUME(v) do { \
    atomic_store_explicit(&g_config.audio.sidetone_volume, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_FADE_DURATION_MS() \
    atomic_load_explicit(&g_config.audio.fade_duration_ms, memory_order_relaxed)

#define CONFIG_SET_FADE_DURATION_MS(v) do { \
    atomic_store_explicit(&g_config.audio.fade_duration_ms, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_GPIO_DIT() \
    atomic_load_explicit(&g_config.hardware.gpio_dit, memory_order_relaxed)

#define CONFIG_SET_GPIO_DIT(v) do { \
    atomic_store_explicit(&g_config.hardware.gpio_dit, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_GPIO_DAH() \
    atomic_load_explicit(&g_config.hardware.gpio_dah, memory_order_relaxed)

#define CONFIG_SET_GPIO_DAH(v) do { \
    atomic_store_explicit(&g_config.hardware.gpio_dah, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_GPIO_TX() \
    atomic_load_explicit(&g_config.hardware.gpio_tx, memory_order_relaxed)

#define CONFIG_SET_GPIO_TX(v) do { \
    atomic_store_explicit(&g_config.hardware.gpio_tx, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_PTT_TAIL_MS() \
    atomic_load_explicit(&g_config.timing.ptt_tail_ms, memory_order_relaxed)

#define CONFIG_SET_PTT_TAIL_MS(v) do { \
    atomic_store_explicit(&g_config.timing.ptt_tail_ms, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_TICK_RATE_HZ() \
    atomic_load_explicit(&g_config.timing.tick_rate_hz, memory_order_relaxed)

#define CONFIG_SET_TICK_RATE_HZ(v) do { \
    atomic_store_explicit(&g_config.timing.tick_rate_hz, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_DEBUG_LOGGING() \
    atomic_load_explicit(&g_config.system.debug_logging, memory_order_relaxed)

#define CONFIG_SET_DEBUG_LOGGING(v) do { \
    atomic_store_explicit(&g_config.system.debug_logging, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_CALLSIGN() \
    (g_config.system.callsign)

#define CONFIG_SET_CALLSIGN(v) do { \
    strncpy(g_config.system.callsign, (v), 12); \
    g_config.system.callsign[12] = '\0'; \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_GPIO_DATA() \
    atomic_load_explicit(&g_config.leds.gpio_data, memory_order_relaxed)

#define CONFIG_SET_GPIO_DATA(v) do { \
    atomic_store_explicit(&g_config.leds.gpio_data, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_COUNT() \
    atomic_load_explicit(&g_config.leds.count, memory_order_relaxed)

#define CONFIG_SET_COUNT(v) do { \
    atomic_store_explicit(&g_config.leds.count, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_BRIGHTNESS() \
    atomic_load_explicit(&g_config.leds.brightness, memory_order_relaxed)

#define CONFIG_SET_BRIGHTNESS(v) do { \
    atomic_store_explicit(&g_config.leds.brightness, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_BRIGHTNESS_DIM() \
    atomic_load_explicit(&g_config.leds.brightness_dim, memory_order_relaxed)

#define CONFIG_SET_BRIGHTNESS_DIM(v) do { \
    atomic_store_explicit(&g_config.leds.brightness_dim, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_ENABLED() \
    atomic_load_explicit(&g_config.wifi.enabled, memory_order_relaxed)

#define CONFIG_SET_ENABLED(v) do { \
    atomic_store_explicit(&g_config.wifi.enabled, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_SSID() \
    (g_config.wifi.ssid)

#define CONFIG_SET_SSID(v) do { \
    strncpy(g_config.wifi.ssid, (v), 32); \
    g_config.wifi.ssid[32] = '\0'; \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_PASSWORD() \
    (g_config.wifi.password)

#define CONFIG_SET_PASSWORD(v) do { \
    strncpy(g_config.wifi.password, (v), 64); \
    g_config.wifi.password[64] = '\0'; \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_TIMEOUT_SEC() \
    atomic_load_explicit(&g_config.wifi.timeout_sec, memory_order_relaxed)

#define CONFIG_SET_TIMEOUT_SEC(v) do { \
    atomic_store_explicit(&g_config.wifi.timeout_sec, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_USE_STATIC_IP() \
    atomic_load_explicit(&g_config.wifi.use_static_ip, memory_order_relaxed)

#define CONFIG_SET_USE_STATIC_IP(v) do { \
    atomic_store_explicit(&g_config.wifi.use_static_ip, (v), memory_order_relaxed); \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_IP_ADDRESS() \
    (g_config.wifi.ip_address)

#define CONFIG_SET_IP_ADDRESS(v) do { \
    strncpy(g_config.wifi.ip_address, (v), 16); \
    g_config.wifi.ip_address[16] = '\0'; \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_NETMASK() \
    (g_config.wifi.netmask)

#define CONFIG_SET_NETMASK(v) do { \
    strncpy(g_config.wifi.netmask, (v), 16); \
    g_config.wifi.netmask[16] = '\0'; \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_GATEWAY() \
    (g_config.wifi.gateway)

#define CONFIG_SET_GATEWAY(v) do { \
    strncpy(g_config.wifi.gateway, (v), 16); \
    g_config.wifi.gateway[16] = '\0'; \
    config_bump_generation(&g_config); \
} while(0)

#define CONFIG_GET_DNS() \
    (g_config.wifi.dns)

#define CONFIG_SET_DNS(v) do { \
    strncpy(g_config.wifi.dns, (v), 16); \
    g_config.wifi.dns[16] = '\0'; \
    config_bump_generation(&g_config); \
} while(0)

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_H */
