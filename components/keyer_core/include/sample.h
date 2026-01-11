/**
 * @file sample.h
 * @brief Stream sample type - the fundamental keying event unit
 *
 * Each sample is 6 bytes packed, containing GPIO state, keying output,
 * audio level, flags, and config generation.
 *
 * ARCHITECTURE.md compliance:
 * - RULE 1.1.1: All keying events flow through KeyingStream
 * - RULE 3.1.1: Only atomic operations for synchronization
 */

#ifndef KEYER_SAMPLE_H
#define KEYER_SAMPLE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * GPIO State
 * ============================================================================ */

/**
 * @brief GPIO paddle state (1 byte)
 */
typedef struct {
    uint8_t bits;
} gpio_state_t;

#define GPIO_DIT_BIT  0x01
#define GPIO_DAH_BIT  0x02

/** Idle state - no paddles pressed */
#define GPIO_IDLE     ((gpio_state_t){.bits = 0})

/** Both paddles pressed */
#define GPIO_BOTH     ((gpio_state_t){.bits = (GPIO_DIT_BIT | GPIO_DAH_BIT)})

/** Check if DIT paddle is pressed */
static inline bool gpio_dit(gpio_state_t gs) {
    return (gs.bits & GPIO_DIT_BIT) != 0;
}

/** Check if DAH paddle is pressed */
static inline bool gpio_dah(gpio_state_t gs) {
    return (gs.bits & GPIO_DAH_BIT) != 0;
}

/** Check if no paddles pressed */
static inline bool gpio_is_idle(gpio_state_t gs) {
    return gs.bits == 0;
}

/** Check if both paddles pressed */
static inline bool gpio_both_pressed(gpio_state_t gs) {
    return (gs.bits & (GPIO_DIT_BIT | GPIO_DAH_BIT)) == (GPIO_DIT_BIT | GPIO_DAH_BIT);
}

/** Create GPIO state from individual paddle states */
static inline gpio_state_t gpio_from_paddles(bool dit, bool dah) {
    gpio_state_t gs = {.bits = 0};
    if (dit) gs.bits |= GPIO_DIT_BIT;
    if (dah) gs.bits |= GPIO_DAH_BIT;
    return gs;
}

/** Set DIT state in GPIO state */
static inline gpio_state_t gpio_with_dit(gpio_state_t gs, bool pressed) {
    if (pressed) {
        gs.bits |= GPIO_DIT_BIT;
    } else {
        gs.bits &= (uint8_t)~GPIO_DIT_BIT;
    }
    return gs;
}

/** Set DAH state in GPIO state */
static inline gpio_state_t gpio_with_dah(gpio_state_t gs, bool pressed) {
    if (pressed) {
        gs.bits |= GPIO_DAH_BIT;
    } else {
        gs.bits &= (uint8_t)~GPIO_DAH_BIT;
    }
    return gs;
}

/* ============================================================================
 * Sample Flags
 * ============================================================================ */

/** GPIO state changed from previous sample */
#define FLAG_GPIO_EDGE      0x01

/** Configuration changed */
#define FLAG_CONFIG_CHANGE  0x02

/** TX transmission started */
#define FLAG_TX_START       0x04

/** Remote CW reception started */
#define FLAG_RX_START       0x08

/** Silence marker (RLE compression) */
#define FLAG_SILENCE        0x10

/** Local key state edge (on/off transition) */
#define FLAG_LOCAL_EDGE     0x20

/* ============================================================================
 * Stream Sample (6 bytes packed)
 * ============================================================================ */

/**
 * @brief Stream sample - the fundamental keying event unit
 *
 * Layout (6 bytes total):
 * - gpio:       1 byte - Physical paddle state
 * - local_key:  1 byte - Iambic keyer output (bool as u8)
 * - audio_level:1 byte - Audio output level (0-255)
 * - flags:      1 byte - Edge flags and markers
 * - config_gen: 2 bytes - Config generation / silence ticks
 */
typedef struct __attribute__((packed)) {
    gpio_state_t gpio;       /**< Physical paddle state */
    uint8_t      local_key;  /**< Keyer output: 1=key down, 0=key up */
    uint8_t      audio_level;/**< Audio output level (0-255) */
    uint8_t      flags;      /**< Edge flags and markers */
    uint16_t     config_gen; /**< Config generation or silence tick count */
} stream_sample_t;

/** Empty sample (all zeros) */
#define STREAM_SAMPLE_EMPTY ((stream_sample_t){ \
    .gpio = GPIO_IDLE, \
    .local_key = 0, \
    .audio_level = 0, \
    .flags = 0, \
    .config_gen = 0 \
})

/* ============================================================================
 * Sample Helper Functions
 * ============================================================================ */

/** Check if this is a silence marker (RLE compressed) */
static inline bool sample_is_silence(const stream_sample_t *s) {
    return (s->flags & FLAG_SILENCE) != 0;
}

/** Create a silence marker with the given tick count */
static inline stream_sample_t sample_silence(uint32_t ticks) {
    stream_sample_t s = STREAM_SAMPLE_EMPTY;
    s.flags = FLAG_SILENCE;
    s.config_gen = (uint16_t)(ticks > UINT16_MAX ? UINT16_MAX : ticks);
    return s;
}

/** Get silence tick count from a silence marker */
static inline uint32_t sample_silence_ticks(const stream_sample_t *s) {
    return s->config_gen;
}

/** Check if sample has a GPIO edge flag */
static inline bool sample_has_gpio_edge(const stream_sample_t *s) {
    return (s->flags & FLAG_GPIO_EDGE) != 0;
}

/** Check if sample has a local key edge flag */
static inline bool sample_has_local_edge(const stream_sample_t *s) {
    return (s->flags & FLAG_LOCAL_EDGE) != 0;
}

/**
 * @brief Check if sample has changed from another
 *
 * Used for silence compression - if no change, increment idle counter.
 */
static inline bool sample_has_change_from(const stream_sample_t *a,
                                          const stream_sample_t *b) {
    return a->gpio.bits != b->gpio.bits ||
           a->local_key != b->local_key ||
           a->audio_level != b->audio_level;
}

/**
 * @brief Create sample with edge flags computed from previous sample
 *
 * @param current Current sample state
 * @param previous Previous sample for edge detection
 * @return Sample with edge flags set
 */
stream_sample_t sample_with_edges_from(stream_sample_t current,
                                       const stream_sample_t *previous);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_SAMPLE_H */
