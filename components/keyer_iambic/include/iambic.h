/**
 * @file iambic.h
 * @brief Iambic keyer finite state machine
 *
 * Pure logic, no hardware dependencies. Consumes GPIO state,
 * produces keying output. Fully testable on host.
 *
 * Iambic Modes:
 * - Mode A: Stops immediately when paddles released
 * - Mode B: Completes current element + one more when squeeze released
 */

#ifndef KEYER_IAMBIC_H
#define KEYER_IAMBIC_H

#include <stdint.h>
#include <stdbool.h>
#include "sample.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Enums
 * ============================================================================ */

/**
 * @brief Iambic keyer mode
 */
typedef enum {
    IAMBIC_MODE_A = 0,  /**< Mode A: Stop immediately when paddles released */
    IAMBIC_MODE_B = 1,  /**< Mode B: Complete current + bonus element on squeeze release */
} iambic_mode_t;

/**
 * @brief Keying element type
 */
typedef enum {
    ELEMENT_DIT = 0,
    ELEMENT_DAH = 1,
} iambic_element_t;

/* ============================================================================
 * Configuration
 * ============================================================================ */

/**
 * @brief Iambic keyer configuration
 */
typedef struct {
    uint32_t wpm;          /**< Speed in words per minute (PARIS timing) */
    iambic_mode_t mode;    /**< Iambic mode (A or B) */
    bool dit_memory;       /**< Dit memory enable */
    bool dah_memory;       /**< Dah memory enable */
} iambic_config_t;

/**
 * @brief Default iambic configuration
 */
#define IAMBIC_CONFIG_DEFAULT { \
    .wpm = 20, \
    .mode = IAMBIC_MODE_B, \
    .dit_memory = true, \
    .dah_memory = true \
}

/**
 * @brief Calculate dit duration in microseconds
 *
 * PARIS timing: dit = 1.2 / WPM seconds
 *
 * @param config Iambic configuration
 * @return Dit duration in microseconds
 */
static inline int64_t iambic_dit_duration_us(const iambic_config_t *config) {
    return 1200000 / (int64_t)config->wpm;
}

/**
 * @brief Calculate dah duration in microseconds (3x dit)
 *
 * @param config Iambic configuration
 * @return Dah duration in microseconds
 */
static inline int64_t iambic_dah_duration_us(const iambic_config_t *config) {
    return iambic_dit_duration_us(config) * 3;
}

/**
 * @brief Calculate inter-element gap in microseconds (1x dit)
 *
 * @param config Iambic configuration
 * @return Gap duration in microseconds
 */
static inline int64_t iambic_gap_duration_us(const iambic_config_t *config) {
    return iambic_dit_duration_us(config);
}

/* ============================================================================
 * Processor
 * ============================================================================ */

/**
 * @brief FSM state (internal)
 */
typedef enum {
    IAMBIC_STATE_IDLE = 0,
    IAMBIC_STATE_SEND_DIT = 1,
    IAMBIC_STATE_SEND_DAH = 2,
    IAMBIC_STATE_GAP = 3,
} iambic_state_t;

/**
 * @brief Iambic keyer processor
 *
 * Pure FSM that converts paddle GPIO state into keying output.
 * No hardware dependencies, fully testable on host.
 */
typedef struct {
    iambic_config_t config;    /**< Current configuration */

    /* FSM state */
    iambic_state_t state;      /**< Current FSM state */
    int64_t element_end_us;    /**< Timestamp when current element ends */
    iambic_element_t last_element; /**< Last element sent (for alternation) */

    /* Paddle state */
    bool dit_pressed;          /**< DIT paddle currently pressed */
    bool dah_pressed;          /**< DAH paddle currently pressed */

    /* Memory flags */
    bool dit_memory;           /**< DIT was pressed during element/gap */
    bool dah_memory;           /**< DAH was pressed during element/gap */

    /* Mode B: squeeze tracking */
    bool squeeze_seen;         /**< Squeeze detected during current element */

    /* Output state */
    bool key_down;             /**< Current key output state */
} iambic_processor_t;

/**
 * @brief Initialize iambic processor
 *
 * @param proc Processor to initialize
 * @param config Initial configuration
 */
void iambic_init(iambic_processor_t *proc, const iambic_config_t *config);

/**
 * @brief Update processor configuration
 *
 * @param proc Processor
 * @param config New configuration
 */
void iambic_set_config(iambic_processor_t *proc, const iambic_config_t *config);

/**
 * @brief Tick the FSM and produce output sample
 *
 * @param proc Processor
 * @param now_us Current timestamp in microseconds
 * @param gpio Current paddle GPIO state
 * @return StreamSample with local_key set to current keying state
 */
stream_sample_t iambic_tick(iambic_processor_t *proc, int64_t now_us, gpio_state_t gpio);

/**
 * @brief Check if key output is currently active
 *
 * @param proc Processor
 * @return true if key is down
 */
static inline bool iambic_is_key_down(const iambic_processor_t *proc) {
    return proc->key_down;
}

/**
 * @brief Reset FSM to idle state
 *
 * @param proc Processor
 */
void iambic_reset(iambic_processor_t *proc);

/**
 * @brief Get opposite element
 *
 * @param element Current element
 * @return Opposite element (DIT→DAH, DAH→DIT)
 */
static inline iambic_element_t iambic_element_opposite(iambic_element_t element) {
    return (element == ELEMENT_DIT) ? ELEMENT_DAH : ELEMENT_DIT;
}

#ifdef __cplusplus
}
#endif

#endif /* KEYER_IAMBIC_H */
