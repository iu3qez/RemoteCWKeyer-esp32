/**
 * @file timing_classifier.c
 * @brief Adaptive timing classifier for CW decoding
 */

#include "timing_classifier.h"
#include <stddef.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

/** Number of events before classifier is considered calibrated */
#define WARMUP_EVENTS 3

/** Default EMA alpha (0.3 = 30% new value, 70% old average) */
#define DEFAULT_EMA_ALPHA 0.3f

/** Default tolerance percentage */
#define DEFAULT_TOLERANCE_PCT 25.0f

/** Minimum valid duration to classify (debounce, 5ms) */
#define MIN_DURATION_US 5000

/** Maximum valid duration (5 seconds - likely idle) */
#define MAX_DURATION_US 5000000

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Convert WPM to dit duration in microseconds
 * PARIS standard: "PARIS" = 50 dit units
 * 1 WPM = 50 dit units per minute = 50/60 dit units per second
 * dit duration = 60 / (50 * WPM) seconds = 1.2 / WPM seconds
 * dit duration = 1,200,000 / WPM microseconds
 */
static int64_t wpm_to_dit_us(float wpm) {
    if (wpm < 1.0f) {
        wpm = 1.0f;
    }
    return (int64_t)(1200000.0f / wpm);
}

/**
 * Convert dit duration to WPM
 */
static uint32_t dit_us_to_wpm(int64_t dit_us) {
    if (dit_us <= 0) {
        return 0;
    }
    return (uint32_t)(1200000 / dit_us);
}

/**
 * Update EMA average
 */
static int64_t ema_update(int64_t old_avg, int64_t new_value, float alpha) {
    float new_avg = alpha * (float)new_value + (1.0f - alpha) * (float)old_avg;
    return (int64_t)new_avg;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

void timing_classifier_init(timing_classifier_t *tc, float initial_wpm) {
    if (tc == NULL) {
        return;
    }

    int64_t dit_us = wpm_to_dit_us(initial_wpm);

    tc->dit_avg_us = dit_us;
    tc->dah_avg_us = dit_us * 3;  /* Ideal dah = 3 * dit */
    tc->dit_count = 0;
    tc->dah_count = 0;
    tc->warmup_count = WARMUP_EVENTS;
    tc->tolerance_pct = DEFAULT_TOLERANCE_PCT;
    tc->ema_alpha = DEFAULT_EMA_ALPHA;
}

void timing_classifier_reset(timing_classifier_t *tc, float initial_wpm) {
    timing_classifier_init(tc, initial_wpm);
}

key_event_t timing_classifier_classify(timing_classifier_t *tc,
                                        int64_t duration_us,
                                        bool is_mark) {
    if (tc == NULL) {
        return KEY_EVENT_UNKNOWN;
    }

    /* Ignore very short or very long durations */
    if (duration_us < MIN_DURATION_US || duration_us > MAX_DURATION_US) {
        return KEY_EVENT_UNKNOWN;
    }

    if (is_mark) {
        /* Classify mark as dit or dah */

        /* Threshold: midpoint between dit and dah averages */
        /* With tolerance: dit_avg * 1.5 (midpoint between 1 and 3 dit units) */
        int64_t threshold = (tc->dit_avg_us * 3 + tc->dah_avg_us) / 4;

        /* Apply tolerance */
        float tolerance_factor = 1.0f + tc->tolerance_pct / 100.0f;
        int64_t adjusted_threshold = (int64_t)((float)threshold * tolerance_factor);

        key_event_t event;
        if (duration_us < adjusted_threshold) {
            /* Dit */
            event = KEY_EVENT_DIT;
            tc->dit_avg_us = ema_update(tc->dit_avg_us, duration_us, tc->ema_alpha);
            tc->dit_count++;
        } else {
            /* Dah */
            event = KEY_EVENT_DAH;
            tc->dah_avg_us = ema_update(tc->dah_avg_us, duration_us, tc->ema_alpha);
            tc->dah_count++;
        }

        /* Decrement warmup counter */
        if (tc->warmup_count > 0) {
            tc->warmup_count--;
        }

        return event;
    } else {
        /* Classify space */
        /* Intra-char gap: ~1 dit (< 2 dit)
         * Char gap: ~3 dit (2-5 dit)
         * Word gap: ~7 dit (> 5 dit)
         */
        int64_t dit = tc->dit_avg_us;

        if (duration_us < dit * 2) {
            return KEY_EVENT_INTRA_GAP;
        } else if (duration_us < dit * 5) {
            return KEY_EVENT_CHAR_GAP;
        } else {
            return KEY_EVENT_WORD_GAP;
        }
    }
}

uint32_t timing_classifier_get_wpm(const timing_classifier_t *tc) {
    if (tc == NULL || tc->warmup_count > 0) {
        return 0;
    }
    return dit_us_to_wpm(tc->dit_avg_us);
}

bool timing_classifier_is_calibrated(const timing_classifier_t *tc) {
    if (tc == NULL) {
        return false;
    }
    return tc->warmup_count == 0;
}

void timing_classifier_set_tolerance(timing_classifier_t *tc, float tolerance_pct) {
    if (tc == NULL) {
        return;
    }
    tc->tolerance_pct = tolerance_pct;
}

float timing_classifier_get_ratio(const timing_classifier_t *tc) {
    if (tc == NULL || tc->dit_avg_us <= 0) {
        return 0.0f;
    }
    return (float)tc->dah_avg_us / (float)tc->dit_avg_us;
}

const char *key_event_str(key_event_t event) {
    switch (event) {
        case KEY_EVENT_DIT:       return "DIT";
        case KEY_EVENT_DAH:       return "DAH";
        case KEY_EVENT_INTRA_GAP: return "INTRA_GAP";
        case KEY_EVENT_CHAR_GAP:  return "CHAR_GAP";
        case KEY_EVENT_WORD_GAP:  return "WORD_GAP";
        case KEY_EVENT_UNKNOWN:   return "UNKNOWN";
        default:                  return "???";
    }
}
