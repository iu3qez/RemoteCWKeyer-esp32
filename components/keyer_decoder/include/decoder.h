/**
 * @file decoder.h
 * @brief CW Morse decoder - converts keying stream to text
 *
 * Best-effort consumer on Core 1. Reads mark/space transitions from
 * keying_stream_t, classifies timing, decodes patterns to characters.
 *
 * Architecture:
 *   keying_stream_t → decoder_consumer → timing_classifier → pattern_decoder → text buffer
 */

#ifndef KEYER_DECODER_H
#define KEYER_DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "timing_classifier.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief Decoded character with timestamp
 */
typedef struct {
    char character;         /**< ASCII character (or ' ' for space) */
    int64_t timestamp_us;   /**< When character was decoded */
} decoded_char_t;

/**
 * @brief Decoder state (for status display)
 */
typedef enum {
    DECODER_STATE_IDLE = 0,      /**< Waiting for first dit/dah */
    DECODER_STATE_RECEIVING = 1, /**< Accumulating pattern */
} decoder_state_t;

/**
 * @brief Decoder statistics
 */
typedef struct {
    uint32_t chars_decoded;     /**< Total characters decoded */
    uint32_t words_decoded;     /**< Total words decoded (spaces) */
    uint32_t errors;            /**< Unrecognized patterns */
    uint32_t samples_processed; /**< Stream samples processed */
    uint32_t samples_dropped;   /**< Samples dropped (lag) */
} decoder_stats_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Initialize decoder
 *
 * Call from main() before starting bg_task.
 * Registers as best-effort consumer on keying_stream.
 */
void decoder_init(void);

/**
 * @brief Process samples from stream
 *
 * Call periodically from bg_task (e.g., every 10ms).
 * Non-blocking, processes all available samples.
 */
void decoder_process(void);

/**
 * @brief Enable/disable decoder
 *
 * @param enabled true to enable, false to disable
 */
void decoder_set_enabled(bool enabled);

/**
 * @brief Check if decoder is enabled
 *
 * @return true if enabled
 */
bool decoder_is_enabled(void);

/**
 * @brief Get decoded text (simple, for console)
 *
 * Returns most recent decoded characters as null-terminated string.
 *
 * @param buf Output buffer
 * @param max_len Buffer size (including null terminator)
 * @return Number of characters written (excluding null)
 */
size_t decoder_get_text(char *buf, size_t max_len);

/**
 * @brief Get decoded text with timestamps
 *
 * @param buf Output buffer for decoded_char_t array
 * @param max_count Maximum entries to return
 * @return Number of entries written
 */
size_t decoder_get_text_with_timestamps(decoded_char_t *buf, size_t max_count);

/**
 * @brief Get last decoded character
 *
 * @return Last character with timestamp (character='\0' if none)
 */
decoded_char_t decoder_get_last_char(void);

/**
 * @brief Pop next unread decoded character
 *
 * Returns characters in FIFO order. Each character is returned only once.
 *
 * @return Next unread character (character='\0' if none available)
 */
decoded_char_t decoder_pop_char(void);

/**
 * @brief Get detected WPM
 *
 * @return WPM (0 if not calibrated)
 */
uint32_t decoder_get_wpm(void);

/**
 * @brief Get current pattern being accumulated
 *
 * @param buf Output buffer
 * @param max_len Buffer size
 * @return Pattern length
 */
size_t decoder_get_current_pattern(char *buf, size_t max_len);

/**
 * @brief Get decoder state
 *
 * @return Current state (IDLE or RECEIVING)
 */
decoder_state_t decoder_get_state(void);

/**
 * @brief Get decoder statistics
 *
 * @param stats Output statistics struct
 */
void decoder_get_stats(decoder_stats_t *stats);

/**
 * @brief Get timing classifier (read-only, for stats display)
 *
 * @return Pointer to timing classifier state
 */
const timing_classifier_t *decoder_get_timing(void);

/**
 * @brief Reset decoder state
 *
 * Clears text buffer, resets timing, returns to IDLE.
 */
void decoder_reset(void);

/**
 * @brief Get buffer usage
 *
 * @return Number of characters in buffer
 */
size_t decoder_get_buffer_count(void);

/**
 * @brief Get buffer capacity
 *
 * @return Maximum characters in buffer
 */
size_t decoder_get_buffer_capacity(void);

/**
 * @brief Handle a key event directly (for testing)
 *
 * @param event Event type
 * @param timestamp_us Event timestamp
 */
void decoder_handle_event(key_event_t event, int64_t timestamp_us);

/**
 * @brief Get string name for decoder state
 *
 * @param state Decoder state
 * @return Static string name
 */
const char *decoder_state_str(decoder_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_DECODER_H */
