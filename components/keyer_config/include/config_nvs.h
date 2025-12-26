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
#define NVS_KEY_WPM "wpm"
#define NVS_KEY_IAMBIC_MODE "mode"
#define NVS_KEY_MEMORY_MODE "mem_mode"
#define NVS_KEY_SQUEEZE_MODE "sqz_mode"
#define NVS_KEY_WEIGHT "weight"
#define NVS_KEY_MEM_WINDOW_START_PCT "mem_start"
#define NVS_KEY_MEM_WINDOW_END_PCT "mem_end"
#define NVS_KEY_SIDETONE_FREQ_HZ "st_freq"
#define NVS_KEY_SIDETONE_VOLUME "st_vol"
#define NVS_KEY_FADE_DURATION_MS "fade_ms"
#define NVS_KEY_GPIO_DIT "gpio_dit"
#define NVS_KEY_GPIO_DAH "gpio_dah"
#define NVS_KEY_GPIO_TX "gpio_tx"
#define NVS_KEY_PTT_TAIL_MS "ptt_tail"
#define NVS_KEY_TICK_RATE_HZ "tick_hz"
#define NVS_KEY_DEBUG_LOGGING "debug_log"
#define NVS_KEY_LED_BRIGHTNESS "led_bright"

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONFIG_NVS_H */
