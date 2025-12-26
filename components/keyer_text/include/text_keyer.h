/**
 * @file text_keyer.h
 * @brief Text-to-Morse keyer
 *
 * Converts text to morse code and produces samples on keying stream.
 * Runs on Core 1 (bg_task) with ~10ms tick resolution.
 *
 * Features:
 * - Free-form text via send command
 * - 8 memory slots in NVS
 * - Paddle abort via atomic flag
 * - Uses global WPM from config
 */

#ifndef KEYER_TEXT_KEYER_H
#define KEYER_TEXT_KEYER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include "stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum text length for send command */
#define TEXT_KEYER_MAX_LEN 128

/**
 * @brief Text keyer state
 */
typedef enum {
    TEXT_KEYER_IDLE = 0,      /**< Ready for new text */
    TEXT_KEYER_SENDING,       /**< Transmitting */
    TEXT_KEYER_PAUSED,        /**< Paused (can resume) */
} text_keyer_state_t;

/**
 * @brief Text keyer configuration
 */
typedef struct {
    keying_stream_t *stream;          /**< Output stream (non-owning) */
    const atomic_bool *paddle_abort;  /**< Abort trigger (non-owning, can be NULL) */
} text_keyer_config_t;

/**
 * @brief Initialize text keyer
 *
 * @param config Configuration (stream is required)
 * @return 0 on success, -1 on error
 */
int text_keyer_init(const text_keyer_config_t *config);

/**
 * @brief Send text as morse code
 *
 * @param text Text to send (A-Z, 0-9, punctuation, prosigns, spaces)
 * @return 0 on success, -1 if already sending or invalid
 */
int text_keyer_send(const char *text);

/**
 * @brief Abort current transmission
 */
void text_keyer_abort(void);

/**
 * @brief Pause current transmission
 */
void text_keyer_pause(void);

/**
 * @brief Resume paused transmission
 */
void text_keyer_resume(void);

/**
 * @brief Get current state
 *
 * @return Current state
 */
text_keyer_state_t text_keyer_get_state(void);

/**
 * @brief Get transmission progress
 *
 * @param sent Output: characters sent so far
 * @param total Output: total characters
 */
void text_keyer_get_progress(size_t *sent, size_t *total);

/**
 * @brief Tick function - call from bg_task (~10ms)
 *
 * @param now_us Current timestamp in microseconds
 */
void text_keyer_tick(int64_t now_us);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_TEXT_KEYER_H */
