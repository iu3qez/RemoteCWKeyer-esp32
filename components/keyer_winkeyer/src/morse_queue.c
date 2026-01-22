/**
 * @file morse_queue.c
 * @brief Lock-free SPSC morse queue implementation
 *
 * ARCHITECTURE.md compliance:
 * - Uses stdatomic.h only (RULE 3.1.1)
 * - Never blocks (RULE 3.1.4)
 * - Statically allocated buffer (RULE 9.1)
 *
 * Memory ordering:
 * - Producer: release on write_idx update (makes data visible to consumer)
 * - Consumer: acquire on write_idx read (sees data written by producer)
 * - Consumer: release on read_idx update (tells producer slot is free)
 * - Producer: acquire on read_idx read (sees consumer progress)
 */

#include "morse_queue.h"
#include <string.h>

/**
 * Compile-time check that MORSE_QUEUE_SIZE is power of 2
 */
_Static_assert((MORSE_QUEUE_SIZE & (MORSE_QUEUE_SIZE - 1)) == 0,
               "MORSE_QUEUE_SIZE must be power of 2");
_Static_assert(MORSE_QUEUE_SIZE >= 16,
               "MORSE_QUEUE_SIZE must be at least 16");

void morse_queue_init(morse_queue_t *q) {
    if (q == NULL) {
        return;
    }
    memset(q->buffer, 0, sizeof(q->buffer));
    atomic_store_explicit(&q->write_idx, 0, memory_order_relaxed);
    atomic_store_explicit(&q->read_idx, 0, memory_order_relaxed);
}

bool morse_queue_push(morse_queue_t *q, morse_element_t elem) {
    if (q == NULL) {
        return false;
    }

    /* Load current indices */
    size_t write = atomic_load_explicit(&q->write_idx, memory_order_relaxed);
    size_t read = atomic_load_explicit(&q->read_idx, memory_order_acquire);

    /* Check if full: (write + 1) % size == read */
    size_t next_write = (write + 1) & (MORSE_QUEUE_SIZE - 1);
    if (next_write == read) {
        return false;  /* Queue full */
    }

    /* Write data to buffer */
    q->buffer[write] = elem;

    /* Publish write index (release ensures data is visible to consumer) */
    atomic_store_explicit(&q->write_idx, next_write, memory_order_release);

    return true;
}

bool morse_queue_pop(morse_queue_t *q, morse_element_t *elem) {
    if (q == NULL || elem == NULL) {
        return false;
    }

    /* Load current indices */
    size_t read = atomic_load_explicit(&q->read_idx, memory_order_relaxed);
    size_t write = atomic_load_explicit(&q->write_idx, memory_order_acquire);

    /* Check if empty: read == write */
    if (read == write) {
        return false;  /* Queue empty */
    }

    /* Read data from buffer */
    *elem = q->buffer[read];

    /* Advance read index (release tells producer slot is free) */
    size_t next_read = (read + 1) & (MORSE_QUEUE_SIZE - 1);
    atomic_store_explicit(&q->read_idx, next_read, memory_order_release);

    return true;
}

bool morse_queue_is_empty(const morse_queue_t *q) {
    if (q == NULL) {
        return true;
    }

    size_t read = atomic_load_explicit(&q->read_idx, memory_order_acquire);
    size_t write = atomic_load_explicit(&q->write_idx, memory_order_acquire);

    return read == write;
}

size_t morse_queue_count(const morse_queue_t *q) {
    if (q == NULL) {
        return 0;
    }

    size_t read = atomic_load_explicit(&q->read_idx, memory_order_acquire);
    size_t write = atomic_load_explicit(&q->write_idx, memory_order_acquire);

    /* Handle wraparound: count = (write - read + size) % size */
    return (write - read + MORSE_QUEUE_SIZE) & (MORSE_QUEUE_SIZE - 1);
}

void morse_queue_clear(morse_queue_t *q) {
    if (q == NULL) {
        return;
    }

    /* Set read = write to clear queue */
    size_t write = atomic_load_explicit(&q->write_idx, memory_order_acquire);
    atomic_store_explicit(&q->read_idx, write, memory_order_release);
}
