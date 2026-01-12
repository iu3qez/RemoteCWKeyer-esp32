/**
 * @file cwnet_timestamp.c
 * @brief CWNet 7-bit non-linear timestamp encoding/decoding implementation
 */

#include "cwnet_timestamp.h"

/* Encoding range boundaries */
#define LINEAR_MAX_MS       31
#define MEDIUM_MIN_MS       32
#define MEDIUM_MAX_MS       156
#define LONG_MIN_MS         157
#define LONG_MAX_MS         1165

/* Encoding base values */
#define MEDIUM_BASE         0x20
#define LONG_BASE           0x40

/* Resolution divisors */
#define MEDIUM_DIVISOR      4
#define LONG_DIVISOR        16

/* Maximum encoded value */
#define MAX_ENCODED         0x7F

/* Key bit mask */
#define KEY_BIT_MASK        0x7F

uint8_t cwstream_encode_timestamp(int ms) {
    /* Clamp negative to zero */
    if (ms < 0) {
        return 0x00;
    }

    /* Linear range: 0-31ms */
    if (ms <= LINEAR_MAX_MS) {
        return (uint8_t)ms;
    }

    /* Medium range: 32-156ms at 4ms resolution */
    if (ms <= MEDIUM_MAX_MS) {
        return (uint8_t)(MEDIUM_BASE + (ms - MEDIUM_MIN_MS) / MEDIUM_DIVISOR);
    }

    /* Long range: 157-1165ms at 16ms resolution */
    if (ms <= LONG_MAX_MS) {
        return (uint8_t)(LONG_BASE + (ms - LONG_MIN_MS) / LONG_DIVISOR);
    }

    /* Overflow: clamp to max */
    return MAX_ENCODED;
}

int cwstream_decode_timestamp(uint8_t encoded) {
    /* Strip key bit (bit 7) */
    uint8_t value = encoded & KEY_BIT_MASK;

    /* Linear range: 0x00-0x1F */
    if (value < MEDIUM_BASE) {
        return (int)value;
    }

    /* Medium range: 0x20-0x3F */
    if (value < LONG_BASE) {
        return MEDIUM_MIN_MS + MEDIUM_DIVISOR * (value - MEDIUM_BASE);
    }

    /* Long range: 0x40-0x7F */
    return LONG_MIN_MS + LONG_DIVISOR * (value - LONG_BASE);
}
