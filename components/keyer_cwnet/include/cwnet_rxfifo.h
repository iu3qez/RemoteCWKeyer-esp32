/**
 * @file cwnet_rxfifo.h
 * @brief The received-MORSE byte ring, shared by the box's client and the
 *        station daemon's playback engine
 *
 * One copy of the reference's keying FIFO (CW_KEYING_FIFO_SIZE, MorseRxFifo)
 * and of the two questions asked of its contents: how many milliseconds of
 * keying are queued (CwStream_GetNumMillisecondsBufferedInFifo) and whether
 * an end of transmission is in there (CwStream_CheckForAnyEndOfTransmission-
 * InFifo). Both ends of the wire now wait and end an over by the same rules,
 * so there is exactly one place where those rules can be wrong.
 *
 * Header-only on purpose. `cwnet_play.c` is host-only and absent from the
 * ESP-IDF component's SRCS (KTD2); a shared *object* file would either drag
 * the daemon's engine onto the box or need a third source file in the
 * component just to hold a ring. Everything here is `static inline` over
 * `cwnet_timestamp.h`, which the box already compiles, so the box links
 * nothing new.
 *
 * The timestamps stay with their owner
 * ------------------------------------
 * The ring holds the bytes; the instant each byte arrived lives in a
 * parallel array the owner declares, and push/pop hand back the slot the
 * byte went into or came out of. That is not squeamishness about a few
 * hundred bytes of .bss: the two ends genuinely have different clocks. The
 * box stamps with the reference's 31-bit millisecond counter, which wraps
 * (int32_t, cwnet_client.h); the daemon stamps with a 64-bit CLOCK_MONOTONIC
 * deadline that does not (KTD7). Storing the box's wrapping counter in a
 * 64-bit field would make it look like an instant it is not. What is really
 * the same — the ring arithmetic, the sum of the waits, the end-of-over mark
 * — is here; what is really different stays where it belongs.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cwnet_timestamp.h"

/** The reference's CW_KEYING_FIFO_SIZE: 128 bytes, 149 s of wait */
#define CWNET_RXFIFO_SIZE 128

/** A push or pop that found the ring full or empty */
#define CWNET_RXFIFO_NO_SLOT (-1)

/**
 * @brief Received keying bytes, oldest first
 *
 * Kept raw, like the reference's MorseRxFifo, so the buffered time and the
 * end-of-over check are computed on exactly the bytes that came off the wire.
 */
typedef struct {
    uint8_t cmd[CWNET_RXFIFO_SIZE];
    uint16_t tail;    /**< Slot of the oldest byte */
    uint16_t count;   /**< Bytes held */
} cwnet_rxfifo_t;

/** @brief Empty the ring */
static inline void cwnet_rxfifo_reset(cwnet_rxfifo_t *f) {
    f->tail = 0;
    f->count = 0;
}

/** @brief Bytes waiting in the ring */
static inline size_t cwnet_rxfifo_count(const cwnet_rxfifo_t *f) {
    return (size_t)f->count;
}

/** @brief Whether one more byte would find the ring full */
static inline bool cwnet_rxfifo_full(const cwnet_rxfifo_t *f) {
    return f->count >= CWNET_RXFIFO_SIZE;
}

/**
 * @brief Slot of the oldest byte, without taking it
 *
 * @return The slot, or CWNET_RXFIFO_NO_SLOT when the ring is empty.
 */
static inline int cwnet_rxfifo_peek_slot(const cwnet_rxfifo_t *f) {
    return (f->count == 0u) ? CWNET_RXFIFO_NO_SLOT : (int)f->tail;
}

/**
 * @brief Put one byte at the head
 *
 * The caller stamps its own arrival array at the returned slot. A full ring
 * drops the byte and says so; counting the drop is the caller's, because the
 * two ends report it differently (the reference overwrites silently).
 *
 * @return The slot written, or CWNET_RXFIFO_NO_SLOT when the ring is full.
 */
static inline int cwnet_rxfifo_push(cwnet_rxfifo_t *f, uint8_t cmd) {
    if (cwnet_rxfifo_full(f)) {
        return CWNET_RXFIFO_NO_SLOT;
    }
    uint16_t head = (uint16_t)((f->tail + f->count) % CWNET_RXFIFO_SIZE);
    f->cmd[head] = cmd;
    f->count++;
    return (int)head;
}

/**
 * @brief Take the oldest byte
 *
 * The caller reads its own arrival array at the returned slot; the slot is
 * still valid on return, nothing has overwritten it yet.
 *
 * @param cmd Written with the byte when a slot is returned.
 * @return The slot the byte came from, or CWNET_RXFIFO_NO_SLOT when empty.
 */
static inline int cwnet_rxfifo_pop(cwnet_rxfifo_t *f, uint8_t *cmd) {
    if (f->count == 0u) {
        return CWNET_RXFIFO_NO_SLOT;
    }
    uint16_t slot = f->tail;
    *cmd = f->cmd[slot];
    f->tail = (uint16_t)((slot + 1u) % CWNET_RXFIFO_SIZE);
    f->count--;
    return (int)slot;
}

/**
 * @brief Milliseconds of keying held, as CwStream_GetNumMillisecondsBuffered-
 *        InFifo(): the sum of the decoded waits of every byte in the ring
 */
static inline int32_t cwnet_rxfifo_buffered_ms(const cwnet_rxfifo_t *f) {
    int32_t total = 0;
    for (uint16_t i = 0; i < f->count; i++) {
        uint16_t idx = (uint16_t)((f->tail + i) % CWNET_RXFIFO_SIZE);
        total += cwstream_decode_timestamp(f->cmd[idx]);
    }
    return total;
}

/**
 * @brief Whether the ring holds the end of an over
 *
 * Two consecutive key-up bytes, whatever their waits, are the reference's
 * mark (CwStream_CheckForAnyEndOfTransmissionInFifo). This asks it of the
 * queue as it stands; an engine that consumes the bytes one by one asks the
 * same question of the byte it just took, which is a different question and
 * stays with the engine.
 */
static inline bool cwnet_rxfifo_has_end_of_over(const cwnet_rxfifo_t *f) {
    for (uint16_t i = 1; i < f->count; i++) {
        uint16_t prev = (uint16_t)((f->tail + i - 1u) % CWNET_RXFIFO_SIZE);
        uint16_t cur = (uint16_t)((f->tail + i) % CWNET_RXFIFO_SIZE);
        if ((f->cmd[prev] & 0x80u) == 0u && (f->cmd[cur] & 0x80u) == 0u) {
            return true;
        }
    }
    return false;
}
