/**
 * @file audio_buffer.h
 * @brief Lock-free SPSC ring buffer for audio samples
 *
 * Single Producer, Single Consumer ring buffer using atomic indices.
 * Power of 2 size for efficient modulo.
 */

#ifndef KEYER_AUDIO_BUFFER_H
#define KEYER_AUDIO_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lock-free SPSC audio ring buffer
 */
typedef struct {
    int16_t *buffer;        /**< External buffer (must be power of 2 size) */
    size_t capacity;        /**< Buffer capacity */
    size_t mask;            /**< capacity - 1 for fast modulo */
    atomic_size_t write_idx; /**< Producer write index */
    atomic_size_t read_idx;  /**< Consumer read index */
} audio_ring_buffer_t;

/**
 * @brief Initialize audio ring buffer
 *
 * @param buf Buffer to initialize
 * @param storage External storage array
 * @param capacity Buffer capacity (must be power of 2)
 */
void audio_buffer_init(audio_ring_buffer_t *buf, int16_t *storage, size_t capacity);

/**
 * @brief Push sample to buffer (producer side)
 *
 * Non-blocking. If buffer is full, overwrites oldest sample.
 *
 * @param buf Buffer
 * @param sample Audio sample to push
 */
void audio_buffer_push(audio_ring_buffer_t *buf, int16_t sample);

/**
 * @brief Pop sample from buffer (consumer side)
 *
 * Non-blocking.
 *
 * @param buf Buffer
 * @param out Output sample (written on success)
 * @return true if sample available, false if empty
 */
bool audio_buffer_pop(audio_ring_buffer_t *buf, int16_t *out);

/**
 * @brief Get number of samples available
 *
 * @param buf Buffer
 * @return Number of samples in buffer
 */
size_t audio_buffer_len(const audio_ring_buffer_t *buf);

/**
 * @brief Check if buffer is empty
 *
 * @param buf Buffer
 * @return true if empty
 */
static inline bool audio_buffer_is_empty(const audio_ring_buffer_t *buf) {
    return audio_buffer_len(buf) == 0;
}

/**
 * @brief Check if buffer is full
 *
 * @param buf Buffer
 * @return true if full
 */
static inline bool audio_buffer_is_full(const audio_ring_buffer_t *buf) {
    return audio_buffer_len(buf) >= buf->capacity;
}

/**
 * @brief Clear buffer
 *
 * @param buf Buffer
 */
void audio_buffer_clear(audio_ring_buffer_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_AUDIO_BUFFER_H */
