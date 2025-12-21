/**
 * @file fault.h
 * @brief RT-safe fault state tracking
 *
 * Atomic fault state for tracking timing failures. When a fault occurs,
 * the RT path immediately stops TX/audio (silence is better than corrupt CW).
 *
 * ARCHITECTURE.md compliance:
 * - RULE 6.1.1: FAULT immediately on timing corruption
 * - RULE 6.1.2: Silence over corrupt timing
 * - RULE 6.2: Clear recovery path
 */

#ifndef KEYER_FAULT_H
#define KEYER_FAULT_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Fault Codes
 * ============================================================================ */

/**
 * @brief Fault code enumeration
 */
typedef enum {
    FAULT_NONE = 0,              /**< No fault active */
    FAULT_OVERRUN = 1,           /**< Consumer overrun (missed samples) */
    FAULT_LATENCY_EXCEEDED = 2,  /**< Hard RT latency deadline missed */
    FAULT_PRODUCER_OVERRUN = 3,  /**< Stream buffer full */
    FAULT_HARDWARE = 4,          /**< Hardware failure detected */
} fault_code_t;

/* ============================================================================
 * Fault State
 * ============================================================================ */

/**
 * @brief Atomic fault state
 *
 * All fields are atomic for lock-free access.
 */
typedef struct {
    atomic_bool  active;   /**< Fault active flag */
    atomic_uchar code;     /**< Fault code (fault_code_t) */
    atomic_uint  data;     /**< Additional fault data (e.g., lag value) */
    atomic_uint  count;    /**< Fault occurrence counter */
} fault_state_t;

/**
 * @brief Static initializer for fault_state_t
 */
#define FAULT_STATE_INIT { \
    .active = ATOMIC_VAR_INIT(false), \
    .code = ATOMIC_VAR_INIT(0), \
    .data = ATOMIC_VAR_INIT(0), \
    .count = ATOMIC_VAR_INIT(0) \
}

/**
 * @brief Initialize fault state
 *
 * @param fault Fault state to initialize
 */
void fault_init(fault_state_t *fault);

/**
 * @brief Set fault state
 *
 * Called when a fault condition is detected. Sets active flag,
 * stores code and data, increments counter.
 *
 * @param fault Fault state
 * @param code Fault code
 * @param data Additional data (e.g., lag value)
 */
void fault_set(fault_state_t *fault, fault_code_t code, uint32_t data);

/**
 * @brief Check if fault is active
 *
 * @param fault Fault state
 * @return true if fault is active
 */
static inline bool fault_is_active(const fault_state_t *fault) {
    return atomic_load_explicit(&fault->active, memory_order_acquire);
}

/**
 * @brief Get fault code
 *
 * @param fault Fault state
 * @return Fault code (FAULT_NONE if no fault)
 */
static inline fault_code_t fault_get_code(const fault_state_t *fault) {
    return (fault_code_t)atomic_load_explicit(&fault->code, memory_order_relaxed);
}

/**
 * @brief Get fault data
 *
 * @param fault Fault state
 * @return Additional fault data
 */
static inline uint32_t fault_get_data(const fault_state_t *fault) {
    return atomic_load_explicit(&fault->data, memory_order_relaxed);
}

/**
 * @brief Get fault count
 *
 * @param fault Fault state
 * @return Total number of faults that have occurred
 */
static inline uint32_t fault_get_count(const fault_state_t *fault) {
    return atomic_load_explicit(&fault->count, memory_order_relaxed);
}

/**
 * @brief Clear fault state
 *
 * Called during recovery after fault condition is resolved.
 *
 * @param fault Fault state
 */
void fault_clear(fault_state_t *fault);

/**
 * @brief Get fault code as string
 *
 * @param code Fault code
 * @return Human-readable fault description
 */
const char *fault_code_str(fault_code_t code);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_FAULT_H */
