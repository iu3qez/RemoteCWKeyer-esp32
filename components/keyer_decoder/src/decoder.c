/**
 * @file decoder.c
 * @brief CW Morse decoder implementation
 *
 * Consumes keying_stream_t as best-effort consumer.
 * Detects key transitions, classifies timing, decodes to text.
 */

#include "decoder.h"
#include "morse_table.h"
#include "timing_classifier.h"
#include "consumer.h"
#include "sample.h"

#include <string.h>
#include <stdatomic.h>

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "esp_log.h"
static const char *TAG = "decoder";
#else
/* Host stub for esp_timer_get_time */
static int64_t s_mock_time_us = 0;
static inline int64_t esp_timer_get_time(void) { return s_mock_time_us; }
void decoder_set_mock_time(int64_t time_us) { s_mock_time_us = time_us; }
#endif

/* ============================================================================
 * Configuration
 * ============================================================================ */

/** Decoded text buffer size */
#define DECODER_BUFFER_SIZE 128

/** Maximum pattern length (ITU max is 6 for some punctuation) */
#define MAX_PATTERN_LEN 8

/** Default initial WPM for timing classifier */
#define DEFAULT_INITIAL_WPM 20.0f

/** Inactivity timeout: force finalization after N dit units of silence */
#define INACTIVITY_DIT_UNITS 7

/* ============================================================================
 * External references
 * ============================================================================ */

#ifdef ESP_PLATFORM
extern keying_stream_t g_keying_stream;
#else
/* Host test: provide stream pointer */
static keying_stream_t *s_test_stream = NULL;
void decoder_set_test_stream(keying_stream_t *stream) { s_test_stream = stream; }
#endif

/* ============================================================================
 * Static state
 * ============================================================================ */

/** Decoded text circular buffer */
static decoded_char_t s_decoded_buffer[DECODER_BUFFER_SIZE];
static size_t s_buffer_head = 0;   /* Next write position */
static size_t s_buffer_count = 0;  /* Characters in buffer */
static size_t s_buffer_read = 0;   /* Next read position (for pop) */

/** Current pattern being accumulated */
static char s_pattern[MAX_PATTERN_LEN + 1];
static size_t s_pattern_len = 0;

/** Decoder state */
static decoder_state_t s_state = DECODER_STATE_IDLE;

/** Timing classifier */
static timing_classifier_t s_timing;

/** Stream consumer */
static best_effort_consumer_t s_consumer;
static bool s_consumer_initialized = false;

/** Enable flag */
static atomic_bool s_enabled = false;

/** Edge tracking */
static int64_t s_last_edge_us = 0;
static bool s_last_was_mark = false;

/** Statistics */
static decoder_stats_t s_stats;

/** Last event timestamp in sample time */
static int64_t s_last_event_us = 0;

/** Last event timestamp in wall clock (for inactivity detection) */
static int64_t s_last_event_wall_us = 0;

/** Sample-based time tracking (1 sample = 1ms = 1000us) */
static int64_t s_sample_time_us = 0;

/* ============================================================================
 * Internal helpers
 * ============================================================================ */

/**
 * @brief Add character to decoded buffer
 */
static void buffer_push(char c, int64_t timestamp_us) {
    s_decoded_buffer[s_buffer_head].character = c;
    s_decoded_buffer[s_buffer_head].timestamp_us = timestamp_us;

    s_buffer_head = (s_buffer_head + 1) % DECODER_BUFFER_SIZE;
    if (s_buffer_count < DECODER_BUFFER_SIZE) {
        s_buffer_count++;
    }
}

/**
 * @brief Finalize current pattern and decode
 */
static void finalize_pattern(int64_t timestamp_us) {
    if (s_pattern_len == 0) {
        return;
    }

    s_pattern[s_pattern_len] = '\0';
    char decoded = morse_table_lookup(s_pattern);

    if (decoded != '\0') {
        buffer_push(decoded, timestamp_us);
        s_stats.chars_decoded++;
#ifdef ESP_PLATFORM
        ESP_LOGD(TAG, "Decoded: '%s' -> '%c'", s_pattern, decoded);
#endif
    } else {
        /* Unknown pattern */
        s_stats.errors++;
#ifdef ESP_PLATFORM
        ESP_LOGD(TAG, "Unknown pattern: '%s'", s_pattern);
#endif
    }

    /* Reset pattern */
    s_pattern_len = 0;
    s_state = DECODER_STATE_IDLE;
}

/**
 * @brief Check for inactivity timeout (uses wall clock)
 */
static void check_inactivity(void) {
    if (s_state != DECODER_STATE_RECEIVING || s_last_event_wall_us == 0) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_us = now_us - s_last_event_wall_us;
    int64_t timeout_us = s_timing.dit_avg_us * INACTIVITY_DIT_UNITS;

    if (elapsed_us > timeout_us) {
        /* Force finalization */
        finalize_pattern(s_sample_time_us);
        /* No word space - just character gap */
    }
}

/* ============================================================================
 * Public API
 * ============================================================================ */

void decoder_init(void) {
#ifdef ESP_PLATFORM
    /* Enable DEBUG level for decoder tag */
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    /* Initialize timing classifier with default WPM */
    timing_classifier_init(&s_timing, DEFAULT_INITIAL_WPM);

    /* Reset state */
    s_buffer_head = 0;
    s_buffer_count = 0;
    s_buffer_read = 0;
    s_pattern_len = 0;
    s_state = DECODER_STATE_IDLE;
    s_last_edge_us = 0;
    s_last_was_mark = false;
    s_last_event_us = 0;

    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_decoded_buffer, 0, sizeof(s_decoded_buffer));
    memset(s_pattern, 0, sizeof(s_pattern));

    /* Initialize consumer */
#ifdef ESP_PLATFORM
    best_effort_consumer_init(&s_consumer, &g_keying_stream, 100);
    s_consumer_initialized = true;
#else
    if (s_test_stream != NULL) {
        best_effort_consumer_init(&s_consumer, s_test_stream, 100);
        s_consumer_initialized = true;
    }
#endif

    atomic_store(&s_enabled, true);
}

void decoder_process(void) {
    if (!atomic_load(&s_enabled) || !s_consumer_initialized) {
        return;
    }

    /* Process all available samples */
    stream_sample_t sample;
    while (best_effort_consumer_tick(&s_consumer, &sample)) {
        s_stats.samples_processed++;

        /* Advance sample time based on sample type:
         * - Regular sample: 1ms (1000us)
         * - Silence marker: config_gen ms (ticks * 1000us)
         */
        if (sample_is_silence(&sample)) {
            s_sample_time_us += (int64_t)sample_silence_ticks(&sample) * 1000;
            continue;  /* Silence doesn't change key state */
        }
        s_sample_time_us += 1000;  /* 1 sample = 1ms */

        /* We're interested in local_key transitions (mark/space) */
        bool is_mark = (sample.local_key != 0);

        /* Detect edge */
        if (is_mark != s_last_was_mark) {
            if (s_last_edge_us > 0) {
                /* Edge detected - classify the duration */
                int64_t duration_us = s_sample_time_us - s_last_edge_us;

                /* s_last_was_mark tells us what just ended:
                 * - true: a mark just ended (key went up) → classify as dit/dah
                 * - false: a space just ended (key went down) → classify as gap
                 */
                key_event_t event = timing_classifier_classify(
                    &s_timing, duration_us, s_last_was_mark);

#ifdef ESP_PLATFORM
                ESP_LOGD(TAG, "Edge: %s->%s dur=%lldus event=%d dit_avg=%lld",
                         s_last_was_mark ? "MARK" : "SPACE",
                         is_mark ? "MARK" : "SPACE",
                         (long long)duration_us, (int)event,
                         (long long)s_timing.dit_avg_us);
#endif

                decoder_handle_event(event, s_sample_time_us);
                s_last_event_us = s_sample_time_us;
                s_last_event_wall_us = esp_timer_get_time();
            }

            /* Update edge tracking only on transitions */
            s_last_edge_us = s_sample_time_us;
            s_last_was_mark = is_mark;
        }
    }

    /* Update dropped count */
    s_stats.samples_dropped = (uint32_t)best_effort_consumer_dropped(&s_consumer);

    /* Check for inactivity timeout (uses wall clock) */
    check_inactivity();
}

void decoder_handle_event(key_event_t event, int64_t timestamp_us) {
#ifdef ESP_PLATFORM
    ESP_LOGD(TAG, "Event: %d pattern='%.*s' len=%zu",
             (int)event, (int)s_pattern_len, s_pattern, s_pattern_len);
#endif

    switch (event) {
        case KEY_EVENT_DIT:
            if (s_pattern_len < MAX_PATTERN_LEN) {
                s_pattern[s_pattern_len++] = '.';
#ifdef ESP_PLATFORM
                ESP_LOGD(TAG, "Added DIT, pattern now: '%.*s'",
                         (int)s_pattern_len, s_pattern);
#endif
            }
            s_state = DECODER_STATE_RECEIVING;
            break;

        case KEY_EVENT_DAH:
            if (s_pattern_len < MAX_PATTERN_LEN) {
                s_pattern[s_pattern_len++] = '-';
#ifdef ESP_PLATFORM
                ESP_LOGD(TAG, "Added DAH, pattern now: '%.*s'",
                         (int)s_pattern_len, s_pattern);
#endif
            }
            s_state = DECODER_STATE_RECEIVING;
            break;

        case KEY_EVENT_INTRA_GAP:
            /* Within character - do nothing */
            break;

        case KEY_EVENT_CHAR_GAP:
            /* Character complete */
#ifdef ESP_PLATFORM
            ESP_LOGD(TAG, "CHAR_GAP: finalizing pattern '%.*s'",
                     (int)s_pattern_len, s_pattern);
#endif
            finalize_pattern(timestamp_us);
            break;

        case KEY_EVENT_WORD_GAP:
            /* Word complete */
#ifdef ESP_PLATFORM
            ESP_LOGD(TAG, "WORD_GAP: finalizing pattern '%.*s'",
                     (int)s_pattern_len, s_pattern);
#endif
            finalize_pattern(timestamp_us);
            buffer_push(' ', timestamp_us);
            s_stats.words_decoded++;
            break;

        case KEY_EVENT_UNKNOWN:
        default:
            /* Ignore during warmup */
            break;
    }
}

void decoder_set_enabled(bool enabled) {
    atomic_store(&s_enabled, enabled);
}

bool decoder_is_enabled(void) {
    return atomic_load(&s_enabled);
}

size_t decoder_get_text(char *buf, size_t max_len) {
    if (buf == NULL || max_len == 0) {
        return 0;
    }

    size_t count = (s_buffer_count < max_len - 1) ? s_buffer_count : max_len - 1;
    if (count == 0) {
        buf[0] = '\0';
        return 0;
    }

    /* Calculate start position in circular buffer */
    size_t start;
    if (s_buffer_count >= DECODER_BUFFER_SIZE) {
        start = s_buffer_head;  /* Buffer wrapped, head is oldest */
    } else {
        start = 0;
    }

    /* Copy characters */
    for (size_t i = 0; i < count; i++) {
        size_t idx = (start + s_buffer_count - count + i) % DECODER_BUFFER_SIZE;
        buf[i] = s_decoded_buffer[idx].character;
    }
    buf[count] = '\0';

    return count;
}

size_t decoder_get_text_with_timestamps(decoded_char_t *buf, size_t max_count) {
    if (buf == NULL || max_count == 0) {
        return 0;
    }

    size_t count = (s_buffer_count < max_count) ? s_buffer_count : max_count;
    if (count == 0) {
        return 0;
    }

    /* Calculate start position */
    size_t start;
    if (s_buffer_count >= DECODER_BUFFER_SIZE) {
        start = s_buffer_head;
    } else {
        start = 0;
    }

    /* Copy entries */
    for (size_t i = 0; i < count; i++) {
        size_t idx = (start + s_buffer_count - count + i) % DECODER_BUFFER_SIZE;
        buf[i] = s_decoded_buffer[idx];
    }

    return count;
}

decoded_char_t decoder_get_last_char(void) {
    if (s_buffer_count == 0) {
        return (decoded_char_t){ .character = '\0', .timestamp_us = 0 };
    }

    size_t idx = (s_buffer_head + DECODER_BUFFER_SIZE - 1) % DECODER_BUFFER_SIZE;
    return s_decoded_buffer[idx];
}

decoded_char_t decoder_pop_char(void) {
    /* Check if there are unread characters */
    if (s_buffer_read == s_buffer_head) {
        return (decoded_char_t){ .character = '\0', .timestamp_us = 0 };
    }

    /* Get character at read position */
    decoded_char_t result = s_decoded_buffer[s_buffer_read];

    /* Advance read pointer */
    s_buffer_read = (s_buffer_read + 1) % DECODER_BUFFER_SIZE;

    return result;
}

uint32_t decoder_get_wpm(void) {
    return timing_classifier_get_wpm(&s_timing);
}

size_t decoder_get_current_pattern(char *buf, size_t max_len) {
    if (buf == NULL || max_len == 0) {
        return 0;
    }

    size_t len = (s_pattern_len < max_len - 1) ? s_pattern_len : max_len - 1;
    memcpy(buf, s_pattern, len);
    buf[len] = '\0';

    return len;
}

decoder_state_t decoder_get_state(void) {
    return s_state;
}

void decoder_get_stats(decoder_stats_t *stats) {
    if (stats != NULL) {
        *stats = s_stats;
    }
}

const timing_classifier_t *decoder_get_timing(void) {
    return &s_timing;
}

void decoder_reset(void) {
    s_buffer_head = 0;
    s_buffer_count = 0;
    s_buffer_read = 0;
    s_pattern_len = 0;
    s_state = DECODER_STATE_IDLE;
    s_last_edge_us = 0;
    s_last_was_mark = false;
    s_last_event_us = 0;
    s_last_event_wall_us = 0;
    s_sample_time_us = 0;

    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_decoded_buffer, 0, sizeof(s_decoded_buffer));
    memset(s_pattern, 0, sizeof(s_pattern));

    timing_classifier_reset(&s_timing, DEFAULT_INITIAL_WPM);
}

size_t decoder_get_buffer_count(void) {
    return s_buffer_count;
}

size_t decoder_get_buffer_capacity(void) {
    return DECODER_BUFFER_SIZE;
}

const char *decoder_state_str(decoder_state_t state) {
    switch (state) {
        case DECODER_STATE_IDLE:      return "IDLE";
        case DECODER_STATE_RECEIVING: return "RECEIVING";
        default:                      return "???";
    }
}
