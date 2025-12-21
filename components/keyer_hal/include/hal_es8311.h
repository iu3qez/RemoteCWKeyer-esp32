/**
 * @file hal_es8311.h
 * @brief ES8311 audio codec driver via I2C
 */

#ifndef KEYER_HAL_ES8311_H
#define KEYER_HAL_ES8311_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ES8311 configuration
 */
typedef struct {
    uint8_t i2c_port;      /**< I2C port number */
    uint8_t i2c_addr;      /**< I2C address (default 0x18) */
    uint8_t sda_pin;       /**< I2C SDA GPIO pin */
    uint8_t scl_pin;       /**< I2C SCL GPIO pin */
} hal_es8311_config_t;

/**
 * @brief Default ES8311 configuration
 */
#define HAL_ES8311_CONFIG_DEFAULT { \
    .i2c_port = 0, \
    .i2c_addr = 0x18, \
    .sda_pin = 21, \
    .scl_pin = 22 \
}

/**
 * @brief Initialize ES8311 codec
 * @param config Codec configuration
 * @return ESP_OK on success
 */
esp_err_t hal_es8311_init(const hal_es8311_config_t *config);

/**
 * @brief Set DAC volume
 * @param volume Volume percentage (0-100)
 * @return ESP_OK on success
 */
esp_err_t hal_es8311_set_volume(uint8_t volume);

/**
 * @brief Mute/unmute DAC
 * @param mute true to mute
 * @return ESP_OK on success
 */
esp_err_t hal_es8311_set_mute(bool mute);

/**
 * @brief Power down codec
 * @return ESP_OK on success
 */
esp_err_t hal_es8311_power_down(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_HAL_ES8311_H */
