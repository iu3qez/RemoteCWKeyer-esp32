/**
 * @file audio_buffer.c
 * @brief Lock-free SPSC audio ring buffer implementation
 */

#include "audio_buffer.h"
#include <assert.h>
#include <string.h>

static inline bool is_power_of_2(size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

void audio_buffer_init(audio_ring_buffer_t *buf, int16_t *storage, size_t capacity) {
    assert(buf != NULL);
    assert(storage != NULL);
    assert(capacity > 0);
    assert(is_power_of_2(capacity) && "Capacity must be power of 2");

    buf->buffer = storage;
    buf->capacity = capacity;
    buf->mask = capacity - 1;
    atomic_init(&buf->write_idx, 0);
    atomic_init(&buf->read_idx, 0);

    memset(storage, 0, capacity * sizeof(int16_t));
}

void audio_buffer_push(audio_ring_buffer_t *buf, int16_t sample) {
    assert(buf != NULL);

    size_t write = atomic_load_explicit(&buf->write_idx, memory_order_relaxed);
    size_t slot = write & buf->mask;

    buf->buffer[slot] = sample;

    atomic_store_explicit(&buf->write_idx, write + 1, memory_order_release);
}

bool audio_buffer_pop(audio_ring_buffer_t *buf, int16_t *out) {
    assert(buf != NULL);
    assert(out != NULL);

    size_t read = atomic_load_explicit(&buf->read_idx, memory_order_relaxed);
    size_t write = atomic_load_explicit(&buf->write_idx, memory_order_acquire);

    if (read == write) {
        /* Empty */
        return false;
    }

    size_t slot = read & buf->mask;
    *out = buf->buffer[slot];

    atomic_store_explicit(&buf->read_idx, read + 1, memory_order_release);
    return true;
}

size_t audio_buffer_len(const audio_ring_buffer_t *buf) {
    assert(buf != NULL);

    size_t write = atomic_load_explicit(&buf->write_idx, memory_order_acquire);
    size_t read = atomic_load_explicit(&buf->read_idx, memory_order_relaxed);

    return write - read;
}

void audio_buffer_clear(audio_ring_buffer_t *buf) {
    assert(buf != NULL);

    size_t write = atomic_load_explicit(&buf->write_idx, memory_order_acquire);
    atomic_store_explicit(&buf->read_idx, write, memory_order_release);
}
