/**
 * @file clock.c
 * @brief POSIX CLOCK_MONOTONIC backing for clock.h.
 */
#include "clock.h"

#include <time.h>

#define WIRE_MS_MASK 0x7FFFFFFFu /* 31 bits: the codec's wire width */

uint64_t clock_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    /* tv_sec is a signed time_t; CLOCK_MONOTONIC never returns a negative
     * reading, so the cast below just states that as far as the compiler's
     * -Wsign-conversion is concerned. */
    uint64_t sec_ms = (uint64_t)ts.tv_sec * 1000u;
    uint64_t nsec_ms = (uint64_t)(ts.tv_nsec) / 1000000u;
    return sec_ms + nsec_ms;
}

uint32_t clock_wire_ms(uint64_t monotonic_ms) {
    return (uint32_t)(monotonic_ms & WIRE_MS_MASK);
}
