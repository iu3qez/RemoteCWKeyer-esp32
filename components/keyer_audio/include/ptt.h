/**
 * @file ptt.h
 * @brief PTT (Push-To-Talk) controller with tail timeout
 *
 * Activates PTT on first key-down, deactivates after silence timeout.
 */

#ifndef KEYER_PTT_H
#define KEYER_PTT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PTT state
 */
typedef enum {
    PTT_OFF = 0,
    PTT_ON = 1,
} ptt_state_t;

/**
 * @brief PTT controller
 */
typedef struct {
    ptt_state_t state;       /**< Current PTT state */
    uint64_t tail_us;        /**< Tail timeout in microseconds */
    uint64_t last_audio_us;  /**< Timestamp of last audio activity */
    bool audio_active;       /**< Audio currently active */
} ptt_controller_t;

/**
 * @brief Initialize PTT controller
 *
 * @param ptt Controller to initialize
 * @param tail_ms Tail timeout in milliseconds
 */
void ptt_init(ptt_controller_t *ptt, uint32_t tail_ms);

/**
 * @brief Notify PTT controller of audio sample
 *
 * Called when audio output is active. Updates last_audio timestamp.
 *
 * @param ptt Controller
 * @param timestamp_us Current timestamp in microseconds
 */
void ptt_audio_sample(ptt_controller_t *ptt, uint64_t timestamp_us);

/**
 * @brief Tick PTT controller
 *
 * Updates PTT state based on audio activity and timeout.
 *
 * @param ptt Controller
 * @param timestamp_us Current timestamp in microseconds
 */
void ptt_tick(ptt_controller_t *ptt, uint64_t timestamp_us);

/**
 * @brief Get current PTT state
 *
 * @param ptt Controller
 * @return Current PTT state
 */
static inline ptt_state_t ptt_get_state(const ptt_controller_t *ptt) {
    return ptt->state;
}

/**
 * @brief Check if PTT is on
 *
 * @param ptt Controller
 * @return true if PTT is on
 */
static inline bool ptt_is_on(const ptt_controller_t *ptt) {
    return ptt->state == PTT_ON;
}

/**
 * @brief Force PTT off immediately
 *
 * Used for fault recovery.
 *
 * @param ptt Controller
 */
void ptt_force_off(ptt_controller_t *ptt);

/**
 * @brief Set tail timeout
 *
 * @param ptt Controller
 * @param tail_ms New tail timeout in milliseconds
 */
void ptt_set_tail(ptt_controller_t *ptt, uint32_t tail_ms);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_PTT_H */
