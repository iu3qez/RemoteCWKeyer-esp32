/**
 * @file stream.c
 * @brief Lock-free SPMC stream buffer implementation
 *
 * ARCHITECTURE.md compliance:
 * - RULE 3.1.1: Only atomic operations for synchronization
 * - RULE 3.1.2: The one producer stores the sample, then publishes write_idx (release)
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

/**
 * @brief Whether index idx is overwritten, or being overwritten, at write position write
 *
 * The slot capacity behind the write position is the producer's next.
 */
static inline bool overrun_at(size_t write, size_t idx, size_t capacity) {
    return write - idx >= capacity;  /* Wrapping subtraction */
}

/* ============================================================================
 * KeyingStream Implementation
 * ============================================================================ */

void stream_init(keying_stream_t *stream, stream_sample_t *buffer, size_t capacity) {
    assert(stream != NULL);
    assert(buffer != NULL);
    assert(capacity >= 2 && "one slot is always the producer's next: capacity 1 has none to read");
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
 *
 * Single producer: nobody else moves write_idx, so it is read relaxed,
 * the sample is stored, and only then the index is published with
 * release. A consumer that acquires the new index sees the whole sample
 * (RULE 3.1.2). Publishing first, as fetch_add did, let a consumer read
 * the slot before or during the store (#57).
 *
 * The release fence before the store is the other half of the seqlock:
 * write_idx == idx is the announcement that slot idx is being written,
 * and the store that made it (the previous publish) does not order the
 * stores after it. On a weakly ordered core the slot's bytes could land
 * before the announcement, and a reader's re-check would pass on a torn
 * copy. Seen on arm64 in CI; the fence orders the announcement first.
 */
static inline bool stream_write_slot(keying_stream_t *stream, stream_sample_t sample) {
    size_t idx = atomic_load_explicit(&stream->write_idx, memory_order_relaxed);
    size_t slot_idx = idx & stream->mask;

    atomic_thread_fence(memory_order_release);
    stream->buffer[slot_idx] = sample;

    atomic_store_explicit(&stream->write_idx, idx + 1, memory_order_release);

    return true;
}

bool stream_push(keying_stream_t *stream, stream_sample_t sample) {
    assert(stream != NULL);

    /* Check for change from last sample */
    if (sample_has_change_from(&sample, &stream->last_sample)) {
        /* State changed: flush accumulated idle ticks, then write sample */

        uint32_t idle = (uint32_t)atomic_exchange_explicit(&stream->idle_ticks, 0, memory_order_relaxed);
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

    uint32_t idle = (uint32_t)atomic_exchange_explicit(&stream->idle_ticks, 0, memory_order_relaxed);
    if (idle > 0) {
        stream_write_slot(stream, sample_silence(idle));
    }
}

bool stream_read(const keying_stream_t *stream, size_t idx, stream_sample_t *out) {
    assert(stream != NULL);
    assert(out != NULL);

    /* RULE 3.1.3: Acquire for read */
    size_t write = atomic_load_explicit(&stream->write_idx, memory_order_acquire);

    if (write == idx) {
        /* Not yet written */
        return false;
    }
    if (overrun_at(write, idx, stream->capacity)) {
        /* Consumer too slow */
        return false;
    }

    stream_sample_t copy = stream->buffer[idx & stream->mask];

    /* The producer may have reached this slot while we copied it. It
     * publishes after the store, so an index that still says the slot is
     * ours proves the copy was whole. The fence is what orders the copy's
     * loads before the re-read: an acquire load alone lets earlier loads
     * complete after it on a weakly ordered core (seqlock reader, RULE
     * 3.1.3). */
    atomic_thread_fence(memory_order_acquire);
    write = atomic_load_explicit(&stream->write_idx, memory_order_relaxed);
    if (overrun_at(write, idx, stream->capacity)) {
        return false;
    }

    *out = copy;
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
    return overrun_at(stream_write_position(stream), read_idx, stream->capacity);
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
    size_t oldest = stream_capacity(consumer->stream) - 1;

    /* Move to the oldest readable position: capacity - 1 behind, since the
     * slot capacity behind is the producer's next */
    if (write_pos > oldest) {
        consumer->read_idx = write_pos - oldest;
    } else {
        consumer->read_idx = 0;
    }
}
