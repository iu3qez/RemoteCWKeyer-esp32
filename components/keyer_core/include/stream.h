/**
 * @file stream.h
 * @brief Lock-free SPMC (Single Producer, Multiple Consumer) stream buffer
 *
 * This is the heart of keyer_c. All keying events flow through here.
 *
 * Architecture:
 *   Producers ──────► KeyingStream ──────► Consumers
 *                     (lock-free)         (StreamConsumer)
 *                     (single truth)
 *
 * ARCHITECTURE.md compliance:
 * - RULE 1.1.1: All keying events flow through KeyingStream
 * - RULE 1.1.2: No component communicates except through the stream
 * - RULE 3.1.1: Only atomic operations for synchronization
 * - RULE 3.1.4: No operation shall block
 * - RULE 9.1.4: PSRAM for stream buffer
 */

#ifndef KEYER_STREAM_H
#define KEYER_STREAM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "sample.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * KeyingStream - Lock-free SPMC Ring Buffer
 * ============================================================================ */

/**
 * @brief Lock-free SPMC ring buffer for keying events
 *
 * Single producer (RT thread), multiple consumers.
 * All coordination through atomic operations.
 *
 * Memory ordering:
 * - Producer uses memory_order_acq_rel for write_idx.fetch_add()
 * - Consumer uses memory_order_acquire for write_idx.load()
 *
 * Buffer must be power of 2 for fast modulo via mask.
 */
typedef struct {
    stream_sample_t *buffer;      /**< External buffer (PSRAM) */
    size_t           capacity;    /**< Buffer size (must be power of 2) */
    size_t           mask;        /**< capacity - 1, for fast modulo */
    atomic_size_t    write_idx;   /**< Producer write index (monotonic) */
    atomic_uint_fast32_t idle_ticks; /**< Silence compression counter */
    stream_sample_t  last_sample; /**< Last sample for change detection */
} keying_stream_t;

/**
 * @brief Initialize stream with external buffer
 *
 * @param stream Stream to initialize
 * @param buffer External buffer (should be in PSRAM for large buffers)
 * @param capacity Buffer size (MUST be power of 2)
 *
 * @note Asserts if capacity is not power of 2
 */
void stream_init(keying_stream_t *stream, stream_sample_t *buffer, size_t capacity);

/**
 * @brief Push sample to stream (producer only, RT thread)
 *
 * If sample is unchanged from previous, increments idle counter
 * instead of writing (silence compression / RLE).
 *
 * Timing: O(1), typically < 200ns. Never blocks.
 *
 * @param stream Stream to push to
 * @param sample Sample to push
 * @return true on success, false if buffer full (FAULT condition)
 */
bool stream_push(keying_stream_t *stream, stream_sample_t sample);

/**
 * @brief Push sample unconditionally (no silence compression)
 *
 * Use when every sample must be recorded.
 *
 * @param stream Stream to push to
 * @param sample Sample to push
 * @return true on success, false if buffer full
 */
bool stream_push_raw(keying_stream_t *stream, stream_sample_t sample);

/**
 * @brief Flush accumulated idle ticks as silence marker
 *
 * Call before shutdown to ensure all state is captured.
 *
 * @param stream Stream to flush
 */
void stream_flush(keying_stream_t *stream);

/**
 * @brief Read sample at given index
 *
 * @param stream Stream to read from
 * @param idx Index to read
 * @param out Output sample (written on success)
 * @return true if sample available, false if not yet written or overwritten
 */
bool stream_read(const keying_stream_t *stream, size_t idx, stream_sample_t *out);

/**
 * @brief Get current write position
 *
 * Use for consumer initialization.
 *
 * @param stream Stream to query
 * @return Current write index
 */
size_t stream_write_position(const keying_stream_t *stream);

/**
 * @brief Calculate lag for consumer (samples behind producer)
 *
 * @param stream Stream to query
 * @param read_idx Consumer's current read index
 * @return Number of samples consumer is behind
 */
size_t stream_lag(const keying_stream_t *stream, size_t read_idx);

/**
 * @brief Check if consumer has overrun (fell too far behind)
 *
 * If true, consumer has missed samples and should resync.
 *
 * @param stream Stream to query
 * @param read_idx Consumer's current read index
 * @return true if overrun, false if OK
 */
bool stream_is_overrun(const keying_stream_t *stream, size_t read_idx);

/**
 * @brief Get buffer capacity
 *
 * @param stream Stream to query
 * @return Buffer capacity
 */
static inline size_t stream_capacity(const keying_stream_t *stream) {
    return stream->capacity;
}

/* ============================================================================
 * StreamConsumer - Basic Consumer Handle
 * ============================================================================ */

/**
 * @brief Consumer handle for stream reading
 *
 * Each consumer has its own read_idx (thread-local, no sync needed).
 * Multiple consumers can read independently from the same stream.
 */
typedef struct {
    const keying_stream_t *stream;  /**< Stream being consumed */
    size_t read_idx;                /**< Thread-local read index */
} stream_consumer_t;

/**
 * @brief Initialize consumer at current stream position
 *
 * Consumer will start reading from next sample written.
 *
 * @param consumer Consumer to initialize
 * @param stream Stream to consume from
 */
void consumer_init(stream_consumer_t *consumer, const keying_stream_t *stream);

/**
 * @brief Initialize consumer at specific position
 *
 * Useful for replaying history or resuming from known point.
 *
 * @param consumer Consumer to initialize
 * @param stream Stream to consume from
 * @param position Starting position
 */
void consumer_init_at(stream_consumer_t *consumer, const keying_stream_t *stream,
                      size_t position);

/**
 * @brief Read next sample (non-blocking)
 *
 * @param consumer Consumer handle
 * @param out Output sample (written on success)
 * @return true if sample available, false if caught up
 */
bool consumer_next(stream_consumer_t *consumer, stream_sample_t *out);

/**
 * @brief Peek at next sample without consuming
 *
 * @param consumer Consumer handle
 * @param out Output sample (written on success)
 * @return true if sample available, false if caught up
 */
bool consumer_peek(const stream_consumer_t *consumer, stream_sample_t *out);

/**
 * @brief Get current lag (samples behind producer)
 *
 * @param consumer Consumer handle
 * @return Number of samples behind
 */
size_t consumer_lag(const stream_consumer_t *consumer);

/**
 * @brief Check if consumer has fallen behind and missed samples
 *
 * @param consumer Consumer handle
 * @return true if overrun, false if OK
 */
bool consumer_is_overrun(const stream_consumer_t *consumer);

/**
 * @brief Skip to latest position (for best-effort consumers)
 *
 * Discards backlog and catches up to real-time.
 *
 * @param consumer Consumer handle
 * @return Number of samples skipped
 */
size_t consumer_skip_to_latest(stream_consumer_t *consumer);

/**
 * @brief Resync after overrun
 *
 * Moves read_idx to oldest valid position in buffer.
 *
 * @param consumer Consumer handle
 */
void consumer_resync(stream_consumer_t *consumer);

/**
 * @brief Get current read position
 *
 * @param consumer Consumer handle
 * @return Current read index
 */
static inline size_t consumer_position(const stream_consumer_t *consumer) {
    return consumer->read_idx;
}

#ifdef __cplusplus
}
#endif

#endif /* KEYER_STREAM_H */
