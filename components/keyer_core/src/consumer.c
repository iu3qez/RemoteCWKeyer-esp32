/**
 * @file consumer.c
 * @brief Hard RT and Best-Effort consumer implementations
 *
 * ARCHITECTURE.md compliance:
 * - RULE 4.2.1: Hard RT consumers FAULT on deadline miss
 * - RULE 4.3.1: Best-effort consumers skip if behind
 */

#include "consumer.h"
#include <assert.h>

/* ============================================================================
 * Hard RT Consumer Implementation
 * ============================================================================ */

void hard_rt_consumer_init(hard_rt_consumer_t *consumer,
                           const keying_stream_t *stream,
                           fault_state_t *fault,
                           size_t max_lag) {
    assert(consumer != NULL);
    assert(stream != NULL);
    assert(fault != NULL);
    assert(max_lag > 0);

    consumer->stream = stream;
    consumer->fault = fault;
    consumer->read_idx = stream_write_position(stream);
    consumer->max_lag = max_lag;
}

hard_rt_result_t hard_rt_consumer_tick(hard_rt_consumer_t *consumer,
                                       stream_sample_t *out) {
    assert(consumer != NULL);
    assert(out != NULL);

    /* Check if already faulted */
    if (fault_is_active(consumer->fault)) {
        return HARD_RT_FAULT;
    }

    /* Check lag */
    size_t lag = stream_lag(consumer->stream, consumer->read_idx);

    if (lag > consumer->max_lag) {
        /* FAULT: latency exceeded */
        fault_set(consumer->fault, FAULT_LATENCY_EXCEEDED, (uint32_t)lag);
        return HARD_RT_FAULT;
    }

    if (lag == 0) {
        /* Caught up - no new data */
        return HARD_RT_NO_DATA;
    }

    /* Check for overrun (buffer wrapped) */
    if (stream_is_overrun(consumer->stream, consumer->read_idx)) {
        fault_set(consumer->fault, FAULT_OVERRUN, (uint32_t)lag);
        return HARD_RT_FAULT;
    }

    /* Read sample */
    if (!stream_read(consumer->stream, consumer->read_idx, out)) {
        /* Should not happen if lag > 0 and not overrun */
        fault_set(consumer->fault, FAULT_OVERRUN, (uint32_t)lag);
        return HARD_RT_FAULT;
    }

    consumer->read_idx++;
    return HARD_RT_OK;
}

void hard_rt_consumer_resync(hard_rt_consumer_t *consumer) {
    assert(consumer != NULL);

    consumer->read_idx = stream_write_position(consumer->stream);
}

size_t hard_rt_consumer_lag(const hard_rt_consumer_t *consumer) {
    assert(consumer != NULL);
    return stream_lag(consumer->stream, consumer->read_idx);
}

/* ============================================================================
 * Best Effort Consumer Implementation
 * ============================================================================ */

void best_effort_consumer_init(best_effort_consumer_t *consumer,
                               const keying_stream_t *stream,
                               size_t skip_threshold) {
    assert(consumer != NULL);
    assert(stream != NULL);

    consumer->stream = stream;
    consumer->read_idx = stream_write_position(stream);
    consumer->dropped = 0;
    consumer->skip_threshold = skip_threshold;
}

bool best_effort_consumer_tick(best_effort_consumer_t *consumer,
                               stream_sample_t *out) {
    assert(consumer != NULL);
    assert(out != NULL);

    /* Check lag */
    size_t lag = stream_lag(consumer->stream, consumer->read_idx);

    if (lag == 0) {
        /* Caught up - no new data */
        return false;
    }

    /* Check for overrun or lag threshold exceeded */
    if (stream_is_overrun(consumer->stream, consumer->read_idx) ||
        (consumer->skip_threshold > 0 && lag > consumer->skip_threshold)) {
        /* Skip to near-latest, keep some buffer for smooth operation */
        size_t skip_to = stream_write_position(consumer->stream);
        if (skip_to > 2) {
            skip_to -= 2;  /* Keep 2 samples buffer */
        }

        size_t skipped = skip_to - consumer->read_idx;
        consumer->dropped += skipped;
        consumer->read_idx = skip_to;

        /* Recalculate lag after skip */
        lag = stream_lag(consumer->stream, consumer->read_idx);
        if (lag == 0) {
            return false;
        }
    }

    /* Read sample */
    if (!stream_read(consumer->stream, consumer->read_idx, out)) {
        /* Buffer might have wrapped during our calculations - skip again */
        consumer->read_idx = stream_write_position(consumer->stream);
        consumer->dropped++;
        return false;
    }

    consumer->read_idx++;
    return true;
}

size_t best_effort_consumer_lag(const best_effort_consumer_t *consumer) {
    assert(consumer != NULL);
    return stream_lag(consumer->stream, consumer->read_idx);
}
