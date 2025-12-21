/**
 * @file stream.c
 * @brief Lock-free SPMC stream buffer implementation
 *
 * ARCHITECTURE.md compliance:
 * - RULE 3.1.1: Only atomic operations for synchronization
 * - RULE 3.1.2: AcqRel for read-modify-write
 * - RULE 3.1.3: Acquire for read operations
 * - RULE 3.1.4: No operation shall block
 */

#include "stream.h"
#include <assert.h>
#include <string.h>

/* ============================================================================
 * Helper: Check if value is power of 2
 * ============================================================================ */

static inline bool is_power_of_2(size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

/* ============================================================================
 * KeyingStream Implementation
 * ============================================================================ */

void stream_init(keying_stream_t *stream, stream_sample_t *buffer, size_t capacity) {
    assert(stream != NULL);
    assert(buffer != NULL);
    assert(capacity > 0);
    assert(is_power_of_2(capacity) && "Buffer size must be power of 2");

    stream->buffer = buffer;
    stream->capacity = capacity;
    stream->mask = capacity - 1;
    atomic_init(&stream->write_idx, 0);
    atomic_init(&stream->idle_ticks, 0);
    stream->last_sample = STREAM_SAMPLE_EMPTY;

    /* Zero the buffer */
    memset(buffer, 0, capacity * sizeof(stream_sample_t));
}

/**
 * @brief Write a slot to the ring buffer
 *
 * Internal function - always writes, no compression.
 */
static inline bool stream_write_slot(keying_stream_t *stream, stream_sample_t sample) {
    /* RULE 3.1.2: AcqRel for read-modify-write */
    size_t idx = atomic_fetch_add_explicit(&stream->write_idx, 1, memory_order_acq_rel);
    size_t slot_idx = idx & stream->mask;

    /* Write sample to slot */
    stream->buffer[slot_idx] = sample;

    return true;
}

bool stream_push(keying_stream_t *stream, stream_sample_t sample) {
    assert(stream != NULL);

    /* Check for change from last sample */
    if (sample_has_change_from(&sample, &stream->last_sample)) {
        /* State changed: flush accumulated idle ticks, then write sample */

        uint32_t idle = atomic_exchange_explicit(&stream->idle_ticks, 0, memory_order_relaxed);
        if (idle > 0) {
            if (!stream_write_slot(stream, sample_silence(idle))) {
                return false;
            }
        }

        /* Write sample with edge flags */
        stream_sample_t sample_with_edges = sample_with_edges_from(sample, &stream->last_sample);
        if (!stream_write_slot(stream, sample_with_edges)) {
            return false;
        }

        /* Update last sample */
        stream->last_sample = sample;
    } else {
        /* No change: accumulate idle ticks (silence compression) */
        atomic_fetch_add_explicit(&stream->idle_ticks, 1, memory_order_relaxed);
    }

    return true;
}

bool stream_push_raw(keying_stream_t *stream, stream_sample_t sample) {
    assert(stream != NULL);
    return stream_write_slot(stream, sample);
}

void stream_flush(keying_stream_t *stream) {
    assert(stream != NULL);

    uint32_t idle = atomic_exchange_explicit(&stream->idle_ticks, 0, memory_order_relaxed);
    if (idle > 0) {
        stream_write_slot(stream, sample_silence(idle));
    }
}

bool stream_read(const keying_stream_t *stream, size_t idx, stream_sample_t *out) {
    assert(stream != NULL);
    assert(out != NULL);

    /* RULE 3.1.3: Acquire for read */
    size_t write = atomic_load_explicit(&stream->write_idx, memory_order_acquire);

    /* Calculate how far behind this index is */
    size_t behind = write - idx;  /* Wrapping subtraction is OK */

    if (behind == 0) {
        /* Not yet written */
        return false;
    }
    if (behind > stream->capacity) {
        /* Overwritten (consumer too slow) */
        return false;
    }

    size_t slot_idx = idx & stream->mask;
    *out = stream->buffer[slot_idx];

    return true;
}

size_t stream_write_position(const keying_stream_t *stream) {
    assert(stream != NULL);
    return atomic_load_explicit(&stream->write_idx, memory_order_acquire);
}

size_t stream_lag(const keying_stream_t *stream, size_t read_idx) {
    assert(stream != NULL);
    size_t write = atomic_load_explicit(&stream->write_idx, memory_order_acquire);
    return write - read_idx;  /* Wrapping subtraction */
}

bool stream_is_overrun(const keying_stream_t *stream, size_t read_idx) {
    assert(stream != NULL);
    return stream_lag(stream, read_idx) > stream->capacity;
}

/* ============================================================================
 * StreamConsumer Implementation
 * ============================================================================ */

void consumer_init(stream_consumer_t *consumer, const keying_stream_t *stream) {
    assert(consumer != NULL);
    assert(stream != NULL);

    consumer->stream = stream;
    consumer->read_idx = stream_write_position(stream);
}

void consumer_init_at(stream_consumer_t *consumer, const keying_stream_t *stream,
                      size_t position) {
    assert(consumer != NULL);
    assert(stream != NULL);

    consumer->stream = stream;
    consumer->read_idx = position;
}

bool consumer_next(stream_consumer_t *consumer, stream_sample_t *out) {
    assert(consumer != NULL);
    assert(out != NULL);

    if (!stream_read(consumer->stream, consumer->read_idx, out)) {
        return false;
    }

    consumer->read_idx++;
    return true;
}

bool consumer_peek(const stream_consumer_t *consumer, stream_sample_t *out) {
    assert(consumer != NULL);
    assert(out != NULL);

    return stream_read(consumer->stream, consumer->read_idx, out);
}

size_t consumer_lag(const stream_consumer_t *consumer) {
    assert(consumer != NULL);
    return stream_lag(consumer->stream, consumer->read_idx);
}

bool consumer_is_overrun(const stream_consumer_t *consumer) {
    assert(consumer != NULL);
    return stream_is_overrun(consumer->stream, consumer->read_idx);
}

size_t consumer_skip_to_latest(stream_consumer_t *consumer) {
    assert(consumer != NULL);

    size_t old_idx = consumer->read_idx;
    consumer->read_idx = stream_write_position(consumer->stream);
    return consumer->read_idx - old_idx;
}

void consumer_resync(stream_consumer_t *consumer) {
    assert(consumer != NULL);

    size_t write_pos = stream_write_position(consumer->stream);
    size_t capacity = stream_capacity(consumer->stream);

    /* Move to oldest valid position */
    if (write_pos >= capacity) {
        consumer->read_idx = write_pos - capacity;
    } else {
        consumer->read_idx = 0;
    }
}
