/**
 * @file sidetone.h
 * @brief Sidetone generator with phase accumulator and fade envelope
 *
 * Uses 256-entry sine lookup table for efficient tone generation.
 * Implements digital fade envelope to eliminate key clicks.
 */

#ifndef KEYER_SIDETONE_H
#define KEYER_SIDETONE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Sine Lookup Table
 * ============================================================================ */

/** LUT size (must be power of 2) */
#define SINE_LUT_SIZE 256

/** Pre-computed 256-entry sine LUT (signed 16-bit, full scale) */
extern const int16_t SINE_LUT[SINE_LUT_SIZE];

/* ============================================================================
 * Fade Envelope
 * ============================================================================ */

/**
 * @brief Fade envelope state
 */
typedef enum {
    FADE_SILENT = 0,   /**< No output */
    FADE_IN = 1,       /**< Ramping up */
    FADE_SUSTAIN = 2,  /**< Full output */
    FADE_OUT = 3,      /**< Ramping down */
} fade_state_t;

/* ============================================================================
 * Sidetone Generator
 * ============================================================================ */

/**
 * @brief Sidetone generator
 *
 * Uses phase accumulator with 256-entry sine LUT.
 * Digital fade envelope eliminates key clicks.
 */
typedef struct {
    uint32_t phase;        /**< Current phase (32-bit fixed point) */
    uint32_t phase_inc;    /**< Phase increment per sample (frequency control) */
    fade_state_t fade_state; /**< Current fade envelope state */
    uint16_t fade_pos;     /**< Current position in fade ramp */
    uint16_t fade_len;     /**< Fade ramp length in samples */
    uint32_t sample_rate;  /**< Sample rate in Hz */
} sidetone_gen_t;

/**
 * @brief Initialize sidetone generator
 *
 * @param gen Generator to initialize
 * @param freq_hz Tone frequency in Hz
 * @param sample_rate Sample rate in Hz (typically 8000)
 * @param fade_samples Fade ramp length in samples
 */
void sidetone_init(sidetone_gen_t *gen, uint32_t freq_hz, uint32_t sample_rate,
                   uint16_t fade_samples);

/**
 * @brief Generate next audio sample
 *
 * @param gen Generator
 * @param key_down true if key is pressed
 * @return Signed 16-bit audio sample
 */
int16_t sidetone_next_sample(sidetone_gen_t *gen, bool key_down);

/**
 * @brief Set tone frequency
 *
 * @param gen Generator
 * @param freq_hz New frequency in Hz
 */
void sidetone_set_frequency(sidetone_gen_t *gen, uint32_t freq_hz);

/**
 * @brief Reset generator to silent state
 *
 * @param gen Generator
 */
void sidetone_reset(sidetone_gen_t *gen);

/**
 * @brief Check if generator is currently producing output
 *
 * @param gen Generator
 * @return true if not in FADE_SILENT state
 */
static inline bool sidetone_is_active(const sidetone_gen_t *gen) {
    return gen->fade_state != FADE_SILENT;
}

#ifdef __cplusplus
}
#endif

#endif /* KEYER_SIDETONE_H */
