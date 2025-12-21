/**
 * @file fault.c
 * @brief RT-safe fault state implementation
 *
 * ARCHITECTURE.md compliance:
 * - RULE 6.1.1: FAULT immediately on timing corruption
 * - RULE 6.1.2: Silence over corrupt timing
 */

#include "fault.h"

void fault_init(fault_state_t *fault) {
    atomic_init(&fault->active, false);
    atomic_init(&fault->code, (unsigned char)FAULT_NONE);
    atomic_init(&fault->data, 0);
    atomic_init(&fault->count, 0);
}

void fault_set(fault_state_t *fault, fault_code_t code, uint32_t data) {
    /* Store fault info */
    atomic_store_explicit(&fault->code, (unsigned char)code, memory_order_relaxed);
    atomic_store_explicit(&fault->data, data, memory_order_relaxed);

    /* Increment counter */
    atomic_fetch_add_explicit(&fault->count, 1, memory_order_relaxed);

    /* Set active flag last (with release to ensure visibility) */
    atomic_store_explicit(&fault->active, true, memory_order_release);
}

void fault_clear(fault_state_t *fault) {
    /* Clear active flag (with release) */
    atomic_store_explicit(&fault->active, false, memory_order_release);

    /* Clear code and data */
    atomic_store_explicit(&fault->code, (unsigned char)FAULT_NONE, memory_order_relaxed);
    atomic_store_explicit(&fault->data, 0, memory_order_relaxed);
}

const char *fault_code_str(fault_code_t code) {
    switch (code) {
        case FAULT_NONE:             return "NONE";
        case FAULT_OVERRUN:          return "OVERRUN";
        case FAULT_LATENCY_EXCEEDED: return "LATENCY_EXCEEDED";
        case FAULT_PRODUCER_OVERRUN: return "PRODUCER_OVERRUN";
        case FAULT_HARDWARE:         return "HARDWARE";
        default:                     return "UNKNOWN";
    }
}
