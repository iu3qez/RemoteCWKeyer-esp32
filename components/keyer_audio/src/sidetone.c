/**
 * @file sidetone.c
 * @brief Sidetone generator implementation
 *
 * Phase accumulator with 256-entry sine LUT.
 * Digital fade envelope eliminates key clicks.
 */

#include "sidetone.h"
#include <assert.h>
#include <stddef.h>

/* Phase accumulator uses upper 8 bits for LUT index */
#define PHASE_SHIFT 24
#define PHASE_MASK  0xFF000000U

void sidetone_init(sidetone_gen_t *gen, uint32_t freq_hz, uint32_t sample_rate,
                   uint16_t fade_samples) {
    assert(gen != NULL);
    assert(sample_rate > 0);
    assert(fade_samples > 0);

    gen->phase = 0;
    gen->sample_rate = sample_rate;
    gen->fade_state = FADE_SILENT;
    gen->fade_pos = 0;
    gen->fade_len = fade_samples;

    /* Calculate phase increment: (freq * 2^32) / sample_rate */
    gen->phase_inc = (uint32_t)(((uint64_t)freq_hz << 32) / sample_rate);
}

void sidetone_set_frequency(sidetone_gen_t *gen, uint32_t freq_hz) {
    assert(gen != NULL);

    gen->phase_inc = (uint32_t)(((uint64_t)freq_hz << 32) / gen->sample_rate);
}

void sidetone_reset(sidetone_gen_t *gen) {
    assert(gen != NULL);

    gen->phase = 0;
    gen->fade_state = FADE_SILENT;
    gen->fade_pos = 0;
}

int16_t sidetone_next_sample(sidetone_gen_t *gen, bool key_down) {
    assert(gen != NULL);

    /* Update fade state machine */
    switch (gen->fade_state) {
        case FADE_SILENT:
            if (key_down) {
                gen->fade_state = FADE_IN;
                gen->fade_pos = 0;
            }
            break;

        case FADE_IN:
            if (!key_down) {
                gen->fade_state = FADE_OUT;
                /* Invert position for smooth transition */
                gen->fade_pos = gen->fade_len - gen->fade_pos;
            } else if (gen->fade_pos >= gen->fade_len) {
                gen->fade_state = FADE_SUSTAIN;
            } else {
                gen->fade_pos++;
            }
            break;

        case FADE_SUSTAIN:
            if (!key_down) {
                gen->fade_state = FADE_OUT;
                gen->fade_pos = 0;
            }
            break;

        case FADE_OUT:
            if (key_down) {
                gen->fade_state = FADE_IN;
                /* Invert position for smooth transition */
                gen->fade_pos = gen->fade_len - gen->fade_pos;
            } else if (gen->fade_pos >= gen->fade_len) {
                gen->fade_state = FADE_SILENT;
                gen->fade_pos = 0;
                return 0;
            } else {
                gen->fade_pos++;
            }
            break;
    }

    /* If silent, return 0 */
    if (gen->fade_state == FADE_SILENT) {
        return 0;
    }

    /* Get sine sample from LUT */
    uint8_t lut_idx = (uint8_t)(gen->phase >> PHASE_SHIFT);
    int32_t raw_sample = SINE_LUT[lut_idx];

    /* Advance phase */
    gen->phase += gen->phase_inc;

    /* Apply envelope */
    int32_t envelope;
    switch (gen->fade_state) {
        case FADE_IN:
            /* Linear ramp up */
            envelope = ((int32_t)gen->fade_pos * 32767) / gen->fade_len;
            break;

        case FADE_OUT:
            /* Linear ramp down */
            envelope = ((int32_t)(gen->fade_len - gen->fade_pos) * 32767) / gen->fade_len;
            break;

        case FADE_SUSTAIN:
        default:
            envelope = 32767;
            break;
    }

    /* Apply envelope and scale */
    int32_t sample = (raw_sample * envelope) >> 15;

    /* Clamp to int16_t range */
    if (sample > 32767) sample = 32767;
    if (sample < -32767) sample = -32767;

    return (int16_t)sample;
}
