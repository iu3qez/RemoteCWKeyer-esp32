/* Auto-generated from parameters.yaml - DO NOT EDIT MANUALLY */
/**
 * @file config_nvs.h
 * @brief NVS (Non-Volatile Storage) persistence for configuration
 */

#ifndef KEYER_CONFIG_NVS_H
#define KEYER_CONFIG_NVS_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** NVS namespace for keyer configuration */
#define CONFIG_NVS_NAMESPACE "keyer_cfg"

/**
 * @brief Load all parameters from NVS
 * @return Number of parameters loaded, or negative on error
 */
int config_load_from_nvs(void);

/**
 * @brief Save all parameters to NVS
 * @return Number of parameters saved, or negative on error
 */
int config_save_to_nvs(void);

/**
 * @brief Load single parameter by name
 * @param name Parameter name
 * @return ESP_OK on success
 */
esp_err_t config_load_param(const char *name);

/**
 * @brief Save single parameter by name
 * @param name Parameter name
 * @return ESP_OK on success
 */
esp_err_t config_save_param(const char *name);

/* NVS key definitions */
#define NVS_KEYER_WPM "wpm"
#define NVS_KEYER_IAMBIC_MODE "mode"
#define NVS_KEYER_MEMORY_MODE "mem_mode"
#define NVS_KEYER_SQUEEZE_MODE "sqz_mode"
#define NVS_KEYER_WEIGHT "weight"
#define NVS_KEYER_MEM_WINDOW_START_PCT "mem_start"
#define NVS_KEYER_MEM_WINDOW_END_PCT "mem_end"
#define NVS_AUDIO_SIDETONE_FREQ_HZ "st_freq"
#define NVS_AUDIO_SIDETONE_VOLUME "st_vol"
#define NVS_AUDIO_FADE_DURATION_MS "fade_ms"
#define NVS_HARDWARE_GPIO_DIT "gpio_dit"
#define NVS_HARDWARE_GPIO_DAH "gpio_dah"
#define NVS_HARDWARE_GPIO_TX "gpio_tx"
#define NVS_TIMING_PTT_TAIL_MS "ptt_tail"
#define NVS_TIMING_TICK_RATE_HZ "tick_hz"
#define NVS_SYSTEM_DEBUG_LOGGING "debug_log"
#define NVS_SYSTEM_CALLSIGN "callsign"
#define NVS_LEDS_GPIO_DATA "led_gpio"
#define NVS_LEDS_COUNT "led_count"
#define NVS_LEDS_BRIGHTNESS "led_bright"
#define NVS_LEDS_BRIGHTNESS_DIM "led_dim"
#define NVS_WIFI_ENABLED "wifi_en"
#define NVS_WIFI_SSID "wifi_ssid"
#define NVS_WIFI_PASSWORD "wifi_pass"
#define NVS_WIFI_TIMEOUT_SEC "wifi_tout"
#define NVS_WIFI_USE_STATIC_IP "wifi_static"
#define NVS_WIFI_IP_ADDRESS "wifi_ip"
#define NVS_WIFI_NETMASK "wifi_mask"
#define NVS_WIFI_GATEWAY "wifi_gw"
#define NVS_WIFI_DNS "wifi_dns"

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_NVS_H */
