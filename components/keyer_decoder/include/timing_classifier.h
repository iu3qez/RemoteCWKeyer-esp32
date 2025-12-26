/**
 * @file timing_classifier.h
 * @brief Adaptive timing classifier for CW decoding
 *
 * Uses EMA (Exponential Moving Average) to learn operator's speed.
 * Classifies marks (dit/dah) and spaces (intra/char/word gap).
 */

#ifndef KEYER_TIMING_CLASSIFIER_H
#define KEYER_TIMING_CLASSIFIER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Key Events
 * ============================================================================ */

/**
 * @brief Classified key event types
 */
typedef enum {
    KEY_EVENT_DIT       = 0,    /**< Short mark (.) */
    KEY_EVENT_DAH       = 1,    /**< Long mark (-) */
    KEY_EVENT_INTRA_GAP = 2,    /**< Intra-character space (~1 dit) */
    KEY_EVENT_CHAR_GAP  = 3,    /**< Inter-character space (~3 dit) */
    KEY_EVENT_WORD_GAP  = 4,    /**< Inter-word space (~7 dit) */
    KEY_EVENT_UNKNOWN   = 255,  /**< Warm-up period, not classifiable */
} key_event_t;

/* ============================================================================
 * Timing Classifier State
 * ============================================================================ */

/**
 * @brief Timing classifier state
 *
 * Tracks adaptive timing using EMA for dit and dah averages.
 * Default tolerance is 25% for classification thresholds.
 */
typedef struct {
    int64_t dit_avg_us;     /**< Dit average in microseconds */
    int64_t dah_avg_us;     /**< Dah average in microseconds */
    uint32_t dit_count;     /**< Number of dits seen */
    uint32_t dah_count;     /**< Number of dahs seen */
    uint32_t warmup_count;  /**< Events until calibrated */
    float tolerance_pct;    /**< Tolerance +/- % (default 25) */
    float ema_alpha;        /**< EMA smoothing factor (default 0.3) */
} timing_classifier_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Initialize timing classifier
 *
 * @param tc Classifier to initialize
 * @param initial_wpm Initial WPM estimate (used during warmup)
 *
 * Uses PARIS standard: dit = 1200ms / WPM
 */
void timing_classifier_init(timing_classifier_t *tc, float initial_wpm);

/**
 * @brief Reset classifier to initial state
 *
 * @param tc Classifier to reset
 * @param initial_wpm Initial WPM estimate
 */
void timing_classifier_reset(timing_classifier_t *tc, float initial_wpm);

/**
 * @brief Classify a duration
 *
 * @param tc Classifier state (updated with EMA)
 * @param duration_us Duration in microseconds
 * @param is_mark true = key-on (mark), false = key-off (space)
 * @return Classified event type
 *
 * For marks: DIT or DAH (updates dit_avg or dah_avg)
 * For spaces: INTRA_GAP, CHAR_GAP, or WORD_GAP
 */
key_event_t timing_classifier_classify(timing_classifier_t *tc,
                                        int64_t duration_us,
                                        bool is_mark);

/**
 * @brief Get current detected WPM
 *
 * @param tc Classifier state
 * @return WPM (0 if not yet calibrated)
 *
 * Uses PARIS standard: WPM = 1,200,000 / dit_avg_us
 */
uint32_t timing_classifier_get_wpm(const timing_classifier_t *tc);

/**
 * @brief Check if classifier is calibrated
 *
 * @param tc Classifier state
 * @return true if warmup complete and timing learned
 */
bool timing_classifier_is_calibrated(const timing_classifier_t *tc);

/**
 * @brief Set tolerance percentage
 *
 * @param tc Classifier state
 * @param tolerance_pct Tolerance (default 25.0)
 */
void timing_classifier_set_tolerance(timing_classifier_t *tc, float tolerance_pct);

/**
 * @brief Get dit/dah ratio
 *
 * @param tc Classifier state
 * @return Ratio (ideal is 3.0)
 */
float timing_classifier_get_ratio(const timing_classifier_t *tc);

/**
 * @brief Get string name for event type
 *
 * @param event Event type
 * @return Static string name
 */
const char *key_event_str(key_event_t event);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_TIMING_CLASSIFIER_H */
