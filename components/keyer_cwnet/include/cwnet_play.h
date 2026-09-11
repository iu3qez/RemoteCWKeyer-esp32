/**
 * @file cwnet_play.h
 * @brief Playback of a remote client's MORSE bytes into key edges and PTT
 *
 * This is the station side of the keying stream: the bytes of the client
 * that holds the key go in, key edges and PTT transitions come out at the
 * instants the sender meant them to have, delayed by a buffer B that
 * absorbs the jitter of the link.
 *
 * HOST ONLY. This module is deliberately absent from the ESP-IDF component
 * SRCS: it belongs to the station daemon, never to the box's RT path.
 *
 * It has no clock and no log of its own. The caller passes the current
 * instant to every entry point and reads what happened out of a result
 * structure; printing it is the daemon's business.
 *
 * Timeline
 * --------
 * Each MORSE byte carries the new key state in bit 7 and, in bits 6..0, the
 * 7-bit encoded time to wait BEFORE applying it, measured from the previous
 * edge (CwStreamEnc.c, CwStream_DecodeKeyUpDownEvent). Playback starts at
 * the first byte of an over, delayed by B, and from there every deadline
 * advances by the *decoded* wait, never by the measured time, so
 * quantisation and scheduling slack do not accumulate. Instants and
 * deadlines are 64-bit milliseconds; the 31-bit clock stays on the wire.
 *
 * B is fixed when the over starts and does not change for its duration.
 * The eligibility ceiling on B belongs to the caller, not here.
 *
 * A wait longer than CWSTREAM_MAX_WAIT_MS is split by the sender over
 * several bytes carrying the same key state, all but the last with the
 * 7-bit field at its maximum (CwStreamEnc.c:135-146). A byte whose 7-bit
 * field is that maximum therefore carries no edge here: it only advances
 * the timeline by CWSTREAM_MAX_WAIT_MS, and the state is applied when the
 * remainder arrives, so the edge lands after the sum of the waits. Only
 * the byte that carries the edge owns a deadline, and that deadline is the
 * previous edge plus the sum of the whole group; a chunk is never late,
 * however long after its own share of the wait it turns up. It has to be
 * that way: a client holding the key to tune sends nothing at all until
 * the release, and then the whole split wait in one frame, every chunk of
 * it already past a share it never owned.
 *
 * The reference's own receiver applies such a byte immediately and keys up
 * to a second early; that is corrupted timing, and we do not copy it
 * (ARCHITECTURE.md 8.1). The one wait this costs us is a wait of exactly
 * CWSTREAM_MAX_WAIT_MS, the single value the encoder can emit as a lone
 * maximum byte: its edge is dropped, which is silence, not wrong timing.
 *
 * End of over is the reference's mark: two consecutive key-up bytes
 * (CwStream_CheckForAnyEndOfTransmissionInFifo). It is reported when the
 * second one is consumed, without waiting out its encoded time, and the
 * over is finished once the PTT has dropped. What counts is the state each
 * byte leaves on the key, not the bit it carries: a chunk of a long
 * key-down carries the up bit and is no part of the mark. Whatever the
 * bytes say, the key never stays down across that mark. A marker that
 * arrives while the engine is holding a state is reported at the instant
 * it arrives: waiting out a wait that ends an over would keep the key
 * busy for up to a second after the operator let go of it.
 *
 * An empty FIFO, jitter, and the key that is held
 * -----------------------------------------------
 * A client that keys live sends one byte per edge, at the edge. Every byte
 * therefore reaches us about B ms before its own deadline, and during any
 * element longer than B the FIFO is empty by construction: the byte that
 * ends the element is under a finger that has not lifted yet. An empty
 * FIFO at a deadline is thus the normal state of a live over, not an
 * error, and the state applied simply holds until the next byte arrives
 * (R8).
 *
 * A byte that arrives after its own deadline cannot shorten an element
 * that is already on the air: it is applied on arrival, the element in
 * progress comes out longer by the delay, and the delay is counted and
 * reported as CWNET_PLAY_EV_LATE_BYTE. A split wait is judged that way
 * once, at its edge and against the sum, never chunk by chunk: judged per
 * chunk, the arrival becomes a fresh timeline and every later chunk spends
 * its wait from there, keying the station on well after the operator let
 * go. A byte carrying the state already applied makes no edge.
 *
 * The engine never lifts the key on its own, and a key held down with
 * nothing arriving has no deadline here. That is what an operator tuning
 * up looks like, and at low power holding the key is the procedure, not a
 * preference: silence in the keying is indistinguishable between a hand
 * that has not let go and a client that has died. The PING tells those two
 * apart, because it is answered by the program and not by the hand, so the
 * release comes from there (the caller's PING verdict, R16), from the
 * socket closing, or from the ceiling on an over — all of them through
 * cwnet_play_force_release(). Whatever the reason, the key comes up at
 * once and the PTT drops at the tail (R8, KTD3).
 *
 * PTT
 * ---
 * The PTT follows the keying that is played, not the bytes that arrive:
 * up with the first key-down of an over, advanced by a lead that is never
 * longer than B, and down a tail after the last key-up played. Any release
 * of the key lowers it at the tail at the latest. The client's "set_ptt"
 * strings never reach this module.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cwnet_rxfifo.h"

/** The reference's CW_KEYING_FIFO_SIZE. The ring itself is cwnet_rxfifo.h,
 *  shared with the box's client. */
#define CWNET_PLAY_FIFO_SIZE CWNET_RXFIFO_SIZE

/** Events one cwnet_play_tick() call can report before the caller must tick again */
#define CWNET_PLAY_MAX_EVENTS 16

/** Default PTT tail, the box's own value, in milliseconds */
#define CWNET_PLAY_DEFAULT_PTT_TAIL_MS 100u

/**
 * @brief What the engine did, at the instant it was meant to happen
 */
typedef enum {
    CWNET_PLAY_EV_KEY_DOWN = 0,   /**< Key edge: carrier on */
    CWNET_PLAY_EV_KEY_UP,         /**< Key edge: carrier off */
    CWNET_PLAY_EV_PTT_ON,         /**< PTT raised, lead ahead of the first key-down */
    CWNET_PLAY_EV_PTT_OFF,        /**< PTT dropped, a tail after the last key-up */
    CWNET_PLAY_EV_END_OF_OVER,    /**< Two consecutive key-up bytes were played */
    CWNET_PLAY_EV_OVER_FINISHED,  /**< Over played out and PTT down: the key can be released */
    CWNET_PLAY_EV_LATE_BYTE,      /**< A byte arrived past its deadline; its edge is here */
} cwnet_play_event_type_t;

/**
 * @brief One event with the instant it belongs to
 *
 * @note at_ms is the scheduled instant, not the instant the caller ticked:
 *       a late tick reports the edges where they belonged.
 */
typedef struct {
    cwnet_play_event_type_t type;
    int64_t at_ms;
} cwnet_play_event_t;

/**
 * @brief Events of one tick, oldest first
 *
 * When the array fills, the tick stops and leaves the rest scheduled: the
 * caller ticks again while cwnet_play_next_deadline() is still due.
 */
typedef struct {
    cwnet_play_event_t ev[CWNET_PLAY_MAX_EVENTS];
    size_t count;
} cwnet_play_result_t;

/**
 * @brief Knobs the daemon owns; defaults are the reference's or the box's
 */
typedef struct {
    uint32_t ptt_lead_ms;  /**< PTT ahead of the first key-down, clamped to B (default 0) */
    uint32_t ptt_tail_ms;  /**< PTT after the last key-up (default 100, the box's) */
} cwnet_play_cfg_t;

/** Where the engine is in an over */
typedef enum {
    CWNET_PLAY_IDLE = 0,   /**< Armed with B, waiting for the first byte to anchor on */
    CWNET_PLAY_RUNNING,    /**< A keying deadline is scheduled */
    CWNET_PLAY_WAITING,    /**< State applied, FIFO empty: it holds until the next byte */
    CWNET_PLAY_CLOSING,    /**< End of over played, waiting for the PTT to drop */
} cwnet_play_state_t;

/**
 * @brief The playback engine. Allocated by the caller, never by us.
 */
typedef struct {
    cwnet_play_cfg_t cfg;
    /* The bytes live in the shared ring; their instants of reception stay
     * here, on this end's 64-bit monotonic clock, indexed by the slot the
     * ring hands back (cwnet_rxfifo.h). */
    cwnet_rxfifo_t fifo;
    int64_t received_at_ms[CWNET_PLAY_FIFO_SIZE];
    uint32_t dropped;             /**< Bytes that found the FIFO full */

    uint32_t buffer_ms;           /**< B, fixed for the over */
    uint32_t lead_ms;             /**< Effective lead, clamped to B */
    cwnet_play_state_t state;

    int64_t deadline_ms;          /**< Next keying deadline (CWNET_PLAY_RUNNING) */
    int64_t last_edge_ms;         /**< Instant the state now applied began: the timeline base */
    uint32_t late_bytes;          /**< Bytes applied past their own deadline, since init */
    int64_t late_total_ms;        /**< Milliseconds those bytes were late, in total */
    bool pending_key_down;        /**< State to apply at that deadline */
    bool key_down;                /**< State on the output now */
    bool prev_byte_key_down;      /**< State the last byte consumed leaves on the key */
    bool have_prev_byte;          /**< A byte has been consumed in this over */

    bool ptt_on;                  /**< PTT on the output now */
    bool ptt_on_pending;          /**< A PTT-on is scheduled */
    int64_t ptt_on_at_ms;
    bool ptt_off_pending;         /**< A PTT-off is scheduled */
    int64_t ptt_off_at_ms;
} cwnet_play_t;

/**
 * @brief Set up an engine with its knobs; nothing is playing yet
 *
 * @param play Engine, not NULL
 * @param cfg  Knobs, or NULL for the defaults (lead 0, tail 100 ms)
 */
void cwnet_play_init(cwnet_play_t *play, const cwnet_play_cfg_t *cfg);

/**
 * @brief Arm an over with its buffer, discarding whatever was queued
 *
 * Called when a client takes the key. B is fixed here and does not move
 * for the over; the lead is clamped to it. A PTT raised in anticipation of
 * the discarded over is cancelled, but a PTT already up and a key already
 * down are left alone: a caller that arms an over on top of a live one
 * releases it first.
 *
 * @param play      Engine, not NULL
 * @param buffer_ms B in milliseconds
 */
void cwnet_play_start_over(cwnet_play_t *play, uint32_t buffer_ms);

/**
 * @brief Queue one received MORSE byte with the instant it arrived
 *
 * The byte whose deadline is running is held outside the queue, so an armed
 * over takes CWNET_PLAY_FIFO_SIZE bytes beyond the one being played.
 *
 * The instant matters: it anchors the over, and it is what decides whether
 * a byte is late. Nothing is played here — the events of a byte queued on
 * a waiting engine come out of the next cwnet_play_tick().
 *
 * @return false when the FIFO is full; the byte is dropped and counted.
 */
bool cwnet_play_push(cwnet_play_t *play, uint8_t cmd, int64_t now_ms);

/**
 * @brief Instant of the next thing the engine has to do
 *
 * The nearer of the keying deadline and the PTT's own transitions. A key
 * held down with nothing queued schedules nothing at all: that state ends
 * when a byte arrives or when the caller forces the release (R8), never on
 * a timer of ours. The daemon sleeps until this instant, or until data
 * arrives.
 *
 * A byte queued on an engine that is holding a state makes this instant
 * due at once, possibly in the past: the caller ticks, it does not sleep
 * on a negative timeout.
 *
 * @return false when nothing is scheduled.
 */
bool cwnet_play_next_deadline(const cwnet_play_t *play, int64_t *out_ms);

/**
 * @brief Do everything that is due at or before now_ms
 *
 * @param out Events, cleared first, never NULL. If it fills, the rest
 *            stays scheduled: tick again while the next deadline is due.
 */
void cwnet_play_tick(cwnet_play_t *play, int64_t now_ms, cwnet_play_result_t *out);

/**
 * @brief Safety net: end the over now, whatever the FIFO holds
 *
 * The key goes up at once if it was down, the queue is dropped, and the
 * PTT drops at the tail at the latest; the over is finished when it does.
 */
void cwnet_play_force_release(cwnet_play_t *play, int64_t now_ms, cwnet_play_result_t *out);

/** B of the over in progress, zero when no over is open. The engine owns
 *  this number: it is fixed when the over is armed and does not move. */
uint32_t cwnet_play_buffer_ms(const cwnet_play_t *play);

/** @brief Key state on the output */
bool cwnet_play_key_down(const cwnet_play_t *play);

/** @brief PTT state on the output */
bool cwnet_play_ptt_on(const cwnet_play_t *play);

/** @brief True while an over is being played or is closing */
bool cwnet_play_over_open(const cwnet_play_t *play);

/** @brief Bytes dropped because the FIFO was full, since init */
uint32_t cwnet_play_dropped(const cwnet_play_t *play);

/** @brief Bytes applied after their own deadline, since init */
uint32_t cwnet_play_late_bytes(const cwnet_play_t *play);

/** @brief Milliseconds those late bytes added to the elements, in total */
int64_t cwnet_play_late_ms(const cwnet_play_t *play);
