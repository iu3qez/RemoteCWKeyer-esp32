/**
 * @file hal_audio.h
 * @brief Audio HAL - ES8311 codec + TCA9555 PA control
 */

#ifndef KEYER_HAL_AUDIO_H
#define KEYER_HAL_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio HAL configuration
 */
typedef struct {
    /* I2C bus (shared with other peripherals) */
    int i2c_sda_pin;
    int i2c_scl_pin;
    uint32_t i2c_freq_hz;

    /* I2S pins */
    int i2s_mclk_pin;
    int i2s_bclk_pin;
    int i2s_lrck_pin;
    int i2s_dout_pin;

    /* Audio parameters */
    uint32_t sample_rate;    /**< Sample rate in Hz (typically 8000) */
    uint8_t volume_percent;  /**< Initial volume 0-100 */

    /* PA control */
    bool pa_via_io_expander; /**< true = TCA9555, false = direct GPIO */
    int pa_pin;              /**< TCA9555 pin or GPIO number */
    bool pa_active_high;     /**< PA enable polarity */
} hal_audio_config_t;

/**
 * @brief Default audio configuration (matches reference hardware)
 */
#define HAL_AUDIO_CONFIG_DEFAULT { \
    .i2c_sda_pin = 11, \
    .i2c_scl_pin = 10, \
    .i2c_freq_hz = 400000, \
    .i2s_mclk_pin = 12, \
    .i2s_bclk_pin = 13, \
    .i2s_lrck_pin = 14, \
    .i2s_dout_pin = 16, \
    .sample_rate = 8000, \
    .volume_percent = 70, \
    .pa_via_io_expander = true, \
    .pa_pin = 8, \
    .pa_active_high = true, \
}

/**
 * @brief Initialize audio HAL (ES8311 + I2S + TCA9555)
 * @param config Configuration structure
 * @return ESP_OK on success, error code on failure
 * @note Audio failure does not block boot - system degrades gracefully
 */
esp_err_t hal_audio_init(const hal_audio_config_t *config);

/**
 * @brief Write audio samples to I2S buffer
 * @param samples Pointer to sample buffer (mono, 16-bit signed)
 * @param count Number of samples
 * @return Number of samples actually written
 */
size_t hal_audio_write(const int16_t *samples, size_t count);

/**
 * @brief Set codec volume
 * @param volume_percent 0-100
 * @return ESP_OK on success
 * @note NOT RT-safe (I2C transaction)
 */
esp_err_t hal_audio_set_volume(uint8_t volume_percent);

/**
 * @brief Enable/disable PA via TCA9555
 * @param enable true = PA on, false = PA off
 * @return ESP_OK on success
 * @note NOT RT-safe (I2C transaction)
 */
esp_err_t hal_audio_set_pa(bool enable);

/**
 * @brief Start I2S output
 */
void hal_audio_start(void);

/**
 * @brief Stop I2S output
 */
void hal_audio_stop(void);

/**
 * @brief Check if audio HAL is initialized and available
 * @return true if audio is functional
 */
bool hal_audio_is_available(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_HAL_AUDIO_H */
