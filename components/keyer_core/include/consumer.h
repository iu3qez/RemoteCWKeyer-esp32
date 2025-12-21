/**
 * @file consumer.h
 * @brief Hard RT and Best-Effort consumer implementations
 *
 * Two consumer types:
 * - HardRtConsumer: MUST keep up, FAULTs on lag exceed
 * - BestEffortConsumer: Skips samples if behind, never FAULTs
 *
 * ARCHITECTURE.md compliance:
 * - RULE 4.2.1: Hard RT consumers FAULT on deadline miss
 * - RULE 4.3.1: Best-effort consumers skip if behind
 */

#ifndef KEYER_CONSUMER_H
#define KEYER_CONSUMER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "stream.h"
#include "fault.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Hard RT Consumer
 * ============================================================================ */

/**
 * @brief Hard real-time consumer
 *
 * MUST keep up with producer. If lag exceeds max_lag, triggers FAULT
 * and stops processing (silence is better than corrupt CW timing).
 *
 * Used for: Audio/TX output on Core 0.
 */
typedef struct {
    const keying_stream_t *stream;  /**< Stream being consumed */
    fault_state_t *fault;           /**< Fault state (shared with RT loop) */
    size_t read_idx;                /**< Current read position */
    size_t max_lag;                 /**< Maximum allowed lag before FAULT */
} hard_rt_consumer_t;

/**
 * @brief Hard RT consumer tick result
 */
typedef enum {
    HARD_RT_OK,       /**< Sample read successfully */
    HARD_RT_NO_DATA,  /**< No new data available (caught up) */
    HARD_RT_FAULT,    /**< Fault triggered (lag exceeded) */
} hard_rt_result_t;

/**
 * @brief Initialize hard RT consumer
 *
 * @param consumer Consumer to initialize
 * @param stream Stream to consume from
 * @param fault Shared fault state (triggers on lag exceed)
 * @param max_lag Maximum allowed lag before FAULT
 */
void hard_rt_consumer_init(hard_rt_consumer_t *consumer,
                           const keying_stream_t *stream,
                           fault_state_t *fault,
                           size_t max_lag);

/**
 * @brief Tick hard RT consumer
 *
 * Attempts to read next sample. If lag exceeds max_lag, sets FAULT
 * and returns HARD_RT_FAULT.
 *
 * @param consumer Consumer handle
 * @param out Output sample (valid if HARD_RT_OK returned)
 * @return Result code
 */
hard_rt_result_t hard_rt_consumer_tick(hard_rt_consumer_t *consumer,
                                       stream_sample_t *out);

/**
 * @brief Resync hard RT consumer after fault recovery
 *
 * Moves read_idx to current write position.
 *
 * @param consumer Consumer handle
 */
void hard_rt_consumer_resync(hard_rt_consumer_t *consumer);

/**
 * @brief Get current lag
 *
 * @param consumer Consumer handle
 * @return Current lag in samples
 */
size_t hard_rt_consumer_lag(const hard_rt_consumer_t *consumer);

/* ============================================================================
 * Best Effort Consumer
 * ============================================================================ */

/**
 * @brief Best-effort consumer
 *
 * Skips samples if behind producer. Never FAULTs - just tracks dropped count.
 *
 * Used for: Remote forwarder, decoder, diagnostics on Core 1.
 */
typedef struct {
    const keying_stream_t *stream;  /**< Stream being consumed */
    size_t read_idx;                /**< Current read position */
    size_t dropped;                 /**< Counter of skipped samples */
    size_t skip_threshold;          /**< Lag threshold for auto-skip */
} best_effort_consumer_t;

/**
 * @brief Initialize best-effort consumer
 *
 * @param consumer Consumer to initialize
 * @param stream Stream to consume from
 * @param skip_threshold Lag threshold for auto-skip (0 = never skip)
 */
void best_effort_consumer_init(best_effort_consumer_t *consumer,
                               const keying_stream_t *stream,
                               size_t skip_threshold);

/**
 * @brief Tick best-effort consumer
 *
 * Reads next sample. If lag exceeds skip_threshold, skips to near-latest
 * and increments dropped counter.
 *
 * @param consumer Consumer handle
 * @param out Output sample (valid if true returned)
 * @return true if sample available, false if caught up
 */
bool best_effort_consumer_tick(best_effort_consumer_t *consumer,
                               stream_sample_t *out);

/**
 * @brief Get dropped sample count
 *
 * @param consumer Consumer handle
 * @return Number of samples skipped due to lag
 */
static inline size_t best_effort_consumer_dropped(const best_effort_consumer_t *consumer) {
    return consumer->dropped;
}

/**
 * @brief Reset dropped counter
 *
 * @param consumer Consumer handle
 */
static inline void best_effort_consumer_reset_dropped(best_effort_consumer_t *consumer) {
    consumer->dropped = 0;
}

/**
 * @brief Get current lag
 *
 * @param consumer Consumer handle
 * @return Current lag in samples
 */
size_t best_effort_consumer_lag(const best_effort_consumer_t *consumer);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONSUMER_H */
