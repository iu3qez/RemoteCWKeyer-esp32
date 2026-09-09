/**
 * @file clock.h
 * @brief Monotonic clock for host programs (cwnetd and friends).
 *
 * POSIX today (CLOCK_MONOTONIC). No esp_timer, no test_host stub: this is
 * the host's own clock, independent of the firmware build.
 */
#ifndef HOST_PLATFORM_CLOCK_H
#define HOST_PLATFORM_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Monotonic milliseconds since an unspecified epoch, as a 64-bit value so it
 * never wraps within a daemon's lifetime. This is the value schedule/timeout
 * arithmetic (KTD7: absolute deadlines, poll() timeout to the next one)
 * should be done in.
 */
uint64_t clock_now_ms(void);

/**
 * The low 31 bits of a monotonic-ms reading — the width the CWNet wire
 * timestamp codec actually carries (see keyer_cwnet's non-linear ms codec
 * upstream of this layer). Callers needing a wire-ready value call this
 * instead of masking clock_now_ms() themselves: the width is a wire
 * contract, not something to reconstruct at each call site.
 */
uint32_t clock_wire_ms(uint64_t monotonic_ms);

#ifdef __cplusplus
}
#endif

#endif /* HOST_PLATFORM_CLOCK_H */
