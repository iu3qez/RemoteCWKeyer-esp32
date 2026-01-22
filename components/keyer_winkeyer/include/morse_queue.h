/**
 * @file morse_queue.h
 * @brief Lock-free SPSC (Single Producer, Single Consumer) morse element queue
 *
 * This queue enables communication between:
 * - Producer: Winkeyer controller on Core 1 (background task)
 * - Consumer: RT task on Core 0 (real-time keying)
 *
 * ARCHITECTURE.md compliance:
 * - RULE 3.1.1: Only atomic operations for synchronization
 * - RULE 3.1.4: No operation shall block
 * - RULE 9.1.1: Statically allocated
 * - RULE 9.1.3: No heap allocation
 */

#ifndef MORSE_QUEUE_H
#define MORSE_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Queue size (MUST be power of 2 for efficient modulo)
 */
#define MORSE_QUEUE_SIZE 64

/**
 * @brief Types of morse elements
 */
typedef enum {
    MORSE_ELEMENT_DIT,         /**< Dit element (1 unit key down) */
    MORSE_ELEMENT_DAH,         /**< Dah element (3 units key down) */
    MORSE_ELEMENT_CHAR_SPACE,  /**< Inter-character space (3 dit gap) */
    MORSE_ELEMENT_WORD_SPACE,  /**< Inter-word space (7 dit gap) */
    MORSE_ELEMENT_KEY_DOWN,    /**< Immediate key down (cmd 0x0B) */
    MORSE_ELEMENT_KEY_UP,      /**< Immediate key up */
} morse_element_type_t;

/**
 * @brief A single morse element in the queue
 */
typedef struct {
    morse_element_type_t type;  /**< Element type */
} morse_element_t;

/**
 * @brief Lock-free SPSC queue for morse elements
 *
 * Memory ordering:
 * - Producer uses memory_order_release for write_idx store
 * - Consumer uses memory_order_acquire for write_idx load
 * - Consumer uses memory_order_release for read_idx store
 * - Producer uses memory_order_acquire for read_idx load
 *
 * Full condition: (write_idx + 1) % size == read_idx
 * Empty condition: write_idx == read_idx
 */
typedef struct {
    morse_element_t buffer[MORSE_QUEUE_SIZE];  /**< Static buffer */
    atomic_size_t   write_idx;                  /**< Producer index */
    atomic_size_t   read_idx;                   /**< Consumer index */
} morse_queue_t;

/**
 * @brief Initialize the queue
 *
 * @param q Queue to initialize
 */
void morse_queue_init(morse_queue_t *q);

/**
 * @brief Push element to queue (producer, Core 1)
 *
 * Non-blocking. Returns false if queue is full.
 *
 * @param q Queue to push to
 * @param elem Element to push
 * @return true if pushed, false if queue full
 */
bool morse_queue_push(morse_queue_t *q, morse_element_t elem);

/**
 * @brief Pop element from queue (consumer, Core 0)
 *
 * Non-blocking. Returns false if queue is empty.
 *
 * @param q Queue to pop from
 * @param elem Output element (written on success)
 * @return true if popped, false if queue empty
 */
bool morse_queue_pop(morse_queue_t *q, morse_element_t *elem);

/**
 * @brief Check if queue is empty
 *
 * @param q Queue to check
 * @return true if empty
 */
bool morse_queue_is_empty(const morse_queue_t *q);

/**
 * @brief Get current element count
 *
 * @param q Queue to query
 * @return Number of elements in queue
 */
size_t morse_queue_count(const morse_queue_t *q);

/**
 * @brief Clear all elements from queue
 *
 * Should only be called when both producer and consumer are stopped,
 * or when only one side is active.
 *
 * @param q Queue to clear
 */
void morse_queue_clear(morse_queue_t *q);

#ifdef __cplusplus
}
#endif

#endif /* MORSE_QUEUE_H */
