/**
 * @file hal_audio.h
 * @brief I2S audio output hardware abstraction
 */

#ifndef KEYER_HAL_AUDIO_H
#define KEYER_HAL_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio configuration
 */
typedef struct {
    uint32_t sample_rate;  /**< Sample rate in Hz (typically 8000) */
    uint8_t bits_per_sample; /**< Bits per sample (8 or 16) */
} hal_audio_config_t;

/**
 * @brief Default audio configuration
 */
#define HAL_AUDIO_CONFIG_DEFAULT { \
    .sample_rate = 8000, \
    .bits_per_sample = 16 \
}

/**
 * @brief Initialize I2S audio output
 * @param config Audio configuration
 */
void hal_audio_init(const hal_audio_config_t *config);

/**
 * @brief Write audio samples to I2S
 * @param samples Sample buffer
 * @param count Number of samples
 * @return Number of samples written
 */
size_t hal_audio_write(const int16_t *samples, size_t count);

/**
 * @brief Start audio output
 */
void hal_audio_start(void);

/**
 * @brief Stop audio output
 */
void hal_audio_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_HAL_AUDIO_H */
