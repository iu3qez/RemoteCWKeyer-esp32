/**
 * @file cwnet_play.c
 * @brief Playback of a remote client's MORSE bytes into key edges and PTT
 *
 * The rules and the reasons are in cwnet_play.h. Nothing here reads a clock
 * or writes a log: the caller passes the instant in and reads the events
 * out (KTD10).
 */

#include "cwnet_play.h"
#include "cwnet_timestamp.h"

#include <string.h>

/** Key state lives in bit 7 of a MORSE byte; the wait is the rest */
#define KEY_BIT 0x80u
#define WAIT_MASK 0x7Fu

/** Largest wait one byte can carry: the encoder's split marker */
#define WAIT_MAX_CODE 0x7Fu

/** Room a single action may need in the result before it starts */
#define EVENTS_PER_ACTION 4u

/** Bound on the actions one tick may perform, so a bad state cannot spin */
#define TICK_GUARD (2 * CWNET_PLAY_FIFO_SIZE + 8)

/*===========================================================================*/
/* Events                                                                    */
/*===========================================================================*/

static void emit(cwnet_play_result_t *out, cwnet_play_event_type_t type, int64_t at_ms) {
    if (out->count < CWNET_PLAY_MAX_EVENTS) {
        out->ev[out->count].type = type;
        out->ev[out->count].at_ms = at_ms;
        out->count++;
    }
}

/*===========================================================================*/
/* FIFO                                                                      */
/*===========================================================================*/

static bool fifo_pop(cwnet_play_t *play, uint8_t *cmd, int64_t *received_at_ms) {
    if (play->fifo.count == 0) {
        return false;
    }
    *cmd = play->fifo.cmd[play->fifo.tail];
    *received_at_ms = play->fifo.received_at_ms[play->fifo.tail];
    play->fifo.tail = (uint16_t)((play->fifo.tail + 1u) % CWNET_PLAY_FIFO_SIZE);
    play->fifo.count--;
    return true;
}

/**
 * @brief The configured grace, or the default when the caller left it at zero
 *
 * Zero is not "no grace": a caller that never heard of the field would
 * otherwise fault on every element longer than B, which is the bug this
 * rule exists to fix.
 */
static uint32_t grace_ms(const cwnet_play_t *play) {
    return (play->cfg.key_grace_ms != 0u) ? play->cfg.key_grace_ms
                                          : CWNET_PLAY_DEFAULT_KEY_GRACE_MS;
}

/*===========================================================================*/
/* PTT                                                                       */
/*===========================================================================*/

/**
 * @brief Drop the PTT at at_ms, or keep an earlier drop already scheduled
 */
static void ptt_off_no_later_than(cwnet_play_t *play, int64_t at_ms) {
    if (!play->ptt_on) {
        return;
    }
    if (play->ptt_off_pending && play->ptt_off_at_ms <= at_ms) {
        return;
    }
    play->ptt_off_pending = true;
    play->ptt_off_at_ms = at_ms;
}

/**
 * @brief Raise the PTT a lead ahead of a key-down that is already scheduled
 *
 * The lead is possible only because the edge is known B ms before it has to
 * happen (KTD5); it was clamped to B when the over was armed.
 */
static void schedule_ptt_on(cwnet_play_t *play, int64_t key_down_at_ms) {
    if (!play->pending_key_down || play->ptt_on || play->ptt_on_pending) {
        return;
    }
    play->ptt_on_pending = true;
    play->ptt_on_at_ms = key_down_at_ms - (int64_t)play->lead_ms;
}

/*===========================================================================*/
/* Consuming bytes                                                           */
/*===========================================================================*/

/**
 * @brief True when a byte is a chunk of a wait too long for one byte
 *
 * The encoder splits a wait above CWSTREAM_MAX_WAIT_MS into chunks that all
 * carry the state the wait ends in, every chunk but the last with the 7-bit
 * field at its maximum (CwStreamEnc.c:135-146). Such a chunk carries no edge
 * of its own: it only spends its time in the state the key is already in, so
 * the edge lands after the sum.
 */
static bool is_split_chunk(uint8_t cmd, bool key_down_now) {
    bool byte_key_down = (cmd & KEY_BIT) != 0u;
    return ((cmd & WAIT_MASK) == WAIT_MAX_CODE) && (byte_key_down != key_down_now);
}

/**
 * @brief Take one byte and put its deadline on the timeline
 *
 * @param base_ms        Instant the byte's wait is measured from
 * @param received_at_ms Instant the byte reached us
 *
 * The deadline is base + the decoded wait, so quantisation and scheduling
 * slack never accumulate. The one thing that can move it is the byte
 * arriving after it: an element already on the air cannot be shortened
 * backwards, so the edge is applied on arrival, the element comes out
 * longer by the delay, and the delay is counted and reported (R8).
 */
static void arm_from_byte(cwnet_play_t *play, uint8_t cmd, int64_t base_ms,
                          int64_t received_at_ms, cwnet_play_result_t *out) {
    int64_t wait_ms = (int64_t)cwstream_decode_timestamp(cmd);

    play->pending_key_down = is_split_chunk(cmd, play->key_down)
                                 ? play->key_down
                                 : ((cmd & KEY_BIT) != 0u);
    /* The state this byte actually leaves on the key, not the bit it carries:
     * a chunk of a split wait carries the state the wait ENDS in, and taking
     * its raw bit makes two chunks of a long key-down look like the two
     * key-up bytes that end an over. */
    play->prev_byte_key_down = play->pending_key_down;
    play->have_prev_byte = true;
    play->deadline_ms = base_ms + wait_ms;

    if (received_at_ms > play->deadline_ms) {
        play->late_bytes++;
        play->late_total_ms += received_at_ms - play->deadline_ms;
        play->deadline_ms = received_at_ms;
        emit(out, CWNET_PLAY_EV_LATE_BYTE, received_at_ms);
    }

    play->state = CWNET_PLAY_RUNNING;
    play->grace_pending = false;   /* a byte arrived: the silence is over */
    schedule_ptt_on(play, play->deadline_ms);
}

/**
 * @brief Anchor an over on its first byte: reception + its wait + B
 *
 * B delays the start of the over and nothing else; from there the timeline
 * runs on the encoded waits alone.
 */
static void arm_if_idle(cwnet_play_t *play, cwnet_play_result_t *out) {
    if (play->state != CWNET_PLAY_IDLE || play->fifo.count == 0) {
        return;
    }
    uint8_t cmd = 0;
    int64_t received_at_ms = 0;
    (void)fifo_pop(play, &cmd, &received_at_ms);
    play->have_prev_byte = false;
    /* The anchor is the reception itself plus B, so this byte is never late */
    arm_from_byte(play, cmd, received_at_ms + (int64_t)play->buffer_ms, received_at_ms, out);
}

/**
 * @brief Hold the state applied and wait for the byte that ends it
 *
 * An empty FIFO is what a live over looks like from here: the client sends
 * a byte per edge, at the edge, so every byte lands about B ms before its
 * deadline and any element longer than B empties the queue (R8). The state
 * holds, and at_ms becomes the base the next byte's wait is measured from.
 *
 * The one thing that must not hold forever is a key that is down: the
 * grace bounds it, and nothing else does.
 */
static void hold_and_wait(cwnet_play_t *play, int64_t at_ms) {
    play->state = CWNET_PLAY_WAITING;
    play->last_edge_ms = at_ms;
    play->grace_pending = play->key_down;
    play->grace_at_ms = at_ms + (int64_t)grace_ms(play);
}

/**
 * @brief The grace ran out under a key-down: lift it and say why
 *
 * Nothing is coming on this link, and a carrier nobody is modulating is
 * worse than the silence that replaces it (ARCHITECTURE.md 8.1).
 */
static void grace_fault(cwnet_play_t *play, int64_t at_ms, cwnet_play_result_t *out) {
    play->grace_pending = false;
    if (play->key_down) {
        play->key_down = false;
        emit(out, CWNET_PLAY_EV_KEY_UP, at_ms);
        ptt_off_no_later_than(play, at_ms + (int64_t)play->cfg.ptt_tail_ms);
        emit(out, CWNET_PLAY_EV_UNDERRUN, at_ms);
    }
    /* The bytes that follow restart as a new over with the same B (R8) */
    play->state = CWNET_PLAY_IDLE;
    play->have_prev_byte = false;
}

/**
 * @brief Take the next byte, or hold what is applied until one comes
 *
 * @param base_ms     Instant the next byte's wait is measured from
 * @param event_at_ms Instant an end of over would be reported at
 *
 * The two differ only when the byte is consumed after a wait: the timeline
 * still runs from the edge that started the element (base), while the end
 * of over belongs to the instant we learnt of it, not to a base in the
 * past.
 */
static void load_next(cwnet_play_t *play, int64_t base_ms, int64_t event_at_ms,
                      cwnet_play_result_t *out) {
    uint8_t cmd = 0;
    int64_t received_at_ms = 0;
    if (!fifo_pop(play, &cmd, &received_at_ms)) {
        hold_and_wait(play, base_ms);
        return;
    }

    /* Two key-up bytes in a row are the reference's end of transmission.
     * The second one is not waited out: its time is silence, and the sooner
     * the key is free the sooner the other end can come back. */
    if (play->have_prev_byte && !play->prev_byte_key_down && ((cmd & KEY_BIT) == 0u)) {
        /* Whatever brought us here, the engine does not come to rest with the
         * key down: a stuck carrier is the worst outcome this module has
         * (ARCHITECTURE.md 8.1). Free it before closing, at this instant. */
        if (play->key_down) {
            play->key_down = false;
            emit(out, CWNET_PLAY_EV_KEY_UP, event_at_ms);
            ptt_off_no_later_than(play, event_at_ms + (int64_t)play->cfg.ptt_tail_ms);
        }
        play->pending_key_down = false;
        play->grace_pending = false;
        emit(out, CWNET_PLAY_EV_END_OF_OVER, event_at_ms);
        play->state = CWNET_PLAY_CLOSING;
        play->have_prev_byte = false;
        if (!play->ptt_on && !play->ptt_on_pending) {
            emit(out, CWNET_PLAY_EV_OVER_FINISHED, event_at_ms);
            play->state = CWNET_PLAY_IDLE;
        }
        return;
    }

    arm_from_byte(play, cmd, base_ms, received_at_ms, out);
}

/**
 * @brief A byte reached a waiting engine: put it on the timeline
 *
 * Its wait is measured from the edge that started the element still on the
 * air, not from now: the operator's spacing is what goes out.
 */
static void do_resume(cwnet_play_t *play, int64_t at_ms, cwnet_play_result_t *out) {
    load_next(play, play->last_edge_ms, at_ms, out);
}

/**
 * @brief Everything the engine does when a keying deadline comes due
 */
static void do_deadline(cwnet_play_t *play, int64_t at_ms, cwnet_play_result_t *out) {
    if (play->pending_key_down != play->key_down) {
        play->key_down = play->pending_key_down;
        if (play->key_down) {
            if (!play->ptt_on) {   /* no lead was scheduled: never key without PTT */
                play->ptt_on = true;
                play->ptt_on_pending = false;
                emit(out, CWNET_PLAY_EV_PTT_ON, at_ms);
            }
            play->ptt_off_pending = false;
            emit(out, CWNET_PLAY_EV_KEY_DOWN, at_ms);
        } else {
            emit(out, CWNET_PLAY_EV_KEY_UP, at_ms);
            ptt_off_no_later_than(play, at_ms + (int64_t)play->cfg.ptt_tail_ms);
        }
    }

    load_next(play, at_ms, at_ms, out);
}

/*===========================================================================*/
/* Scheduling                                                                */
/*===========================================================================*/

typedef enum {
    ACT_NONE = 0,
    ACT_PTT_ON,    /* first, so the PTT never trails the carrier */
    ACT_RESUME,    /* before the grace: a byte that arrives in time wins */
    ACT_DEADLINE,
    ACT_GRACE,
    ACT_PTT_OFF,   /* last, so a key-down at the same instant cancels it */
} action_t;

/**
 * @brief When a waiting engine can take the byte at the head of the FIFO
 *
 * As soon as it is there: it either lands before its own deadline, and the
 * deadline is what plays it, or it is late and its edge is due on arrival.
 * Never before the edge it is measured from, so a byte pushed while the
 * engine was still running does not resume in the past.
 */
static int64_t resume_at(const cwnet_play_t *play) {
    int64_t at = play->fifo.received_at_ms[play->fifo.tail];
    return (at < play->last_edge_ms) ? play->last_edge_ms : at;
}

static action_t next_action(const cwnet_play_t *play, int64_t *at_ms) {
    action_t best = ACT_NONE;
    int64_t best_at = 0;

    if (play->ptt_on_pending) {
        best = ACT_PTT_ON;
        best_at = play->ptt_on_at_ms;
    }
    if (play->state == CWNET_PLAY_WAITING && play->fifo.count > 0u) {
        int64_t at = resume_at(play);
        if (best == ACT_NONE || at < best_at) {
            best = ACT_RESUME;
            best_at = at;
        }
    }
    if (play->state == CWNET_PLAY_RUNNING) {
        if (best == ACT_NONE || play->deadline_ms < best_at) {
            best = ACT_DEADLINE;
            best_at = play->deadline_ms;
        }
    }
    if (play->grace_pending) {
        if (best == ACT_NONE || play->grace_at_ms < best_at) {
            best = ACT_GRACE;
            best_at = play->grace_at_ms;
        }
    }
    if (play->ptt_off_pending) {
        if (best == ACT_NONE || play->ptt_off_at_ms < best_at) {
            best = ACT_PTT_OFF;
            best_at = play->ptt_off_at_ms;
        }
    }
    *at_ms = best_at;
    return best;
}

static void do_ptt_off(cwnet_play_t *play, int64_t at_ms, cwnet_play_result_t *out) {
    play->ptt_off_pending = false;
    play->ptt_on = false;
    emit(out, CWNET_PLAY_EV_PTT_OFF, at_ms);
    if (play->state == CWNET_PLAY_CLOSING) {
        emit(out, CWNET_PLAY_EV_OVER_FINISHED, at_ms);
        play->state = CWNET_PLAY_IDLE;
        play->have_prev_byte = false;
    }
}

/*===========================================================================*/
/* Public interface                                                          */
/*===========================================================================*/

void cwnet_play_init(cwnet_play_t *play, const cwnet_play_cfg_t *cfg) {
    if (play == NULL) {
        return;
    }
    memset(play, 0, sizeof(*play));
    if (cfg != NULL) {
        play->cfg = *cfg;
    } else {
        play->cfg.ptt_lead_ms = 0u;
        play->cfg.ptt_tail_ms = CWNET_PLAY_DEFAULT_PTT_TAIL_MS;
    }
    play->state = CWNET_PLAY_IDLE;
}

void cwnet_play_start_over(cwnet_play_t *play, uint32_t buffer_ms) {
    if (play == NULL) {
        return;
    }
    play->buffer_ms = buffer_ms;
    play->lead_ms = (play->cfg.ptt_lead_ms > buffer_ms) ? buffer_ms : play->cfg.ptt_lead_ms;
    play->fifo.tail = 0;
    play->fifo.count = 0;
    play->state = CWNET_PLAY_IDLE;
    /* A PTT raised in anticipation of an over we are throwing away must not
     * survive it; a PTT already up is the caller's to release. */
    play->ptt_on_pending = false;
    play->have_prev_byte = false;
    play->prev_byte_key_down = false;
    play->pending_key_down = play->key_down;
    play->deadline_ms = 0;
    play->last_edge_ms = 0;
    play->grace_pending = false;
    play->grace_at_ms = 0;
}

bool cwnet_play_push(cwnet_play_t *play, uint8_t cmd, int64_t now_ms) {
    if (play == NULL) {
        return false;
    }
    if (play->fifo.count >= CWNET_PLAY_FIFO_SIZE) {
        play->dropped++;
        return false;
    }
    uint16_t head = (uint16_t)((play->fifo.tail + play->fifo.count) % CWNET_PLAY_FIFO_SIZE);
    play->fifo.cmd[head] = cmd;
    play->fifo.received_at_ms[head] = now_ms;
    play->fifo.count++;
    /* Anchoring the over is pure scheduling — the first byte of an over is
     * measured from its own arrival, so it can never be late and can emit
     * nothing. Everything that does emit waits for a tick, which is where
     * the caller reads its events (KTD10). */
    cwnet_play_result_t sink = { .count = 0 };
    arm_if_idle(play, &sink);
    return true;
}

bool cwnet_play_next_deadline(const cwnet_play_t *play, int64_t *out_ms) {
    if (play == NULL || out_ms == NULL) {
        return false;
    }
    int64_t at_ms = 0;
    if (next_action(play, &at_ms) == ACT_NONE) {
        return false;
    }
    *out_ms = at_ms;
    return true;
}

void cwnet_play_tick(cwnet_play_t *play, int64_t now_ms, cwnet_play_result_t *out) {
    cwnet_play_result_t sink;
    if (out == NULL) {
        out = &sink;
    }
    out->count = 0;
    if (play == NULL) {
        return;
    }

    for (int guard = 0; guard < TICK_GUARD; guard++) {
        arm_if_idle(play, out);

        int64_t at_ms = 0;
        action_t act = next_action(play, &at_ms);
        if (act == ACT_NONE || at_ms > now_ms) {
            return;
        }
        if ((CWNET_PLAY_MAX_EVENTS - out->count) < EVENTS_PER_ACTION) {
            return;   /* the rest stays scheduled: the caller ticks again */
        }

        switch (act) {
            case ACT_PTT_ON:
                play->ptt_on_pending = false;
                play->ptt_on = true;
                emit(out, CWNET_PLAY_EV_PTT_ON, at_ms);
                break;
            case ACT_RESUME:
                do_resume(play, at_ms, out);
                break;
            case ACT_DEADLINE:
                do_deadline(play, at_ms, out);
                break;
            case ACT_GRACE:
                grace_fault(play, at_ms, out);
                break;
            case ACT_PTT_OFF:
                do_ptt_off(play, at_ms, out);
                break;
            default:
                return;
        }
    }
}

void cwnet_play_force_release(cwnet_play_t *play, int64_t now_ms, cwnet_play_result_t *out) {
    cwnet_play_result_t sink;
    if (out == NULL) {
        out = &sink;
    }
    out->count = 0;
    if (play == NULL) {
        return;
    }

    play->fifo.tail = 0;
    play->fifo.count = 0;
    play->have_prev_byte = false;
    play->grace_pending = false;
    play->state = CWNET_PLAY_CLOSING;

    if (play->key_down) {
        play->key_down = false;
        emit(out, CWNET_PLAY_EV_KEY_UP, now_ms);
    }
    play->pending_key_down = false;

    /* A PTT raised only in anticipation of a key-down that will never come */
    if (!play->ptt_on) {
        play->ptt_on_pending = false;
    }

    if (play->ptt_on) {
        ptt_off_no_later_than(play, now_ms + (int64_t)play->cfg.ptt_tail_ms);
    } else {
        emit(out, CWNET_PLAY_EV_OVER_FINISHED, now_ms);
        play->state = CWNET_PLAY_IDLE;
    }
}

uint32_t cwnet_play_buffer_ms(const cwnet_play_t *play) {
    return (play == NULL) ? 0u : play->buffer_ms;
}

bool cwnet_play_key_down(const cwnet_play_t *play) {
    return (play != NULL) && play->key_down;
}

bool cwnet_play_ptt_on(const cwnet_play_t *play) {
    return (play != NULL) && play->ptt_on;
}

bool cwnet_play_over_open(const cwnet_play_t *play) {
    if (play == NULL) {
        return false;
    }
    return play->state != CWNET_PLAY_IDLE || play->key_down || play->ptt_on ||
           play->ptt_on_pending || play->ptt_off_pending;
}

uint32_t cwnet_play_dropped(const cwnet_play_t *play) {
    return (play == NULL) ? 0u : play->dropped;
}

uint32_t cwnet_play_late_bytes(const cwnet_play_t *play) {
    return (play == NULL) ? 0u : play->late_bytes;
}

int64_t cwnet_play_late_ms(const cwnet_play_t *play) {
    return (play == NULL) ? 0 : play->late_total_ms;
}
