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
 * @param base_ms Instant the byte's wait is measured from
 */
static void arm_from_byte(cwnet_play_t *play, uint8_t cmd, int64_t base_ms) {
    int64_t wait_ms = (int64_t)cwstream_decode_timestamp(cmd);

    play->pending_key_down = is_split_chunk(cmd, play->key_down)
                                 ? play->key_down
                                 : ((cmd & KEY_BIT) != 0u);
    play->prev_byte_key_down = (cmd & KEY_BIT) != 0u;
    play->have_prev_byte = true;
    play->deadline_ms = base_ms + wait_ms;
    play->state = CWNET_PLAY_RUNNING;
    schedule_ptt_on(play, play->deadline_ms);
}

/**
 * @brief Anchor an over on its first byte: reception + its wait + B
 *
 * B delays the start of the over and nothing else; from there the timeline
 * runs on the encoded waits alone.
 */
static void arm_if_idle(cwnet_play_t *play) {
    if (play->state != CWNET_PLAY_IDLE || play->fifo.count == 0) {
        return;
    }
    uint8_t cmd = 0;
    int64_t received_at_ms = 0;
    (void)fifo_pop(play, &cmd, &received_at_ms);
    play->have_prev_byte = false;
    arm_from_byte(play, cmd, received_at_ms + (int64_t)play->buffer_ms);
}

/**
 * @brief Give up the over: key up if it was down, and say why
 */
static void underrun(cwnet_play_t *play, int64_t at_ms, cwnet_play_result_t *out) {
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

    uint8_t cmd = 0;
    int64_t received_at_ms = 0;
    if (!fifo_pop(play, &cmd, &received_at_ms)) {
        underrun(play, at_ms, out);
        return;
    }

    /* Two key-up bytes in a row are the reference's end of transmission.
     * The second one is not waited out: its time is silence, and the sooner
     * the key is free the sooner the other end can come back. */
    if (play->have_prev_byte && !play->prev_byte_key_down && ((cmd & KEY_BIT) == 0u)) {
        emit(out, CWNET_PLAY_EV_END_OF_OVER, at_ms);
        play->state = CWNET_PLAY_CLOSING;
        play->have_prev_byte = false;
        if (!play->ptt_on && !play->ptt_on_pending) {
            emit(out, CWNET_PLAY_EV_OVER_FINISHED, at_ms);
            play->state = CWNET_PLAY_IDLE;
        }
        return;
    }

    arm_from_byte(play, cmd, at_ms);
}

/*===========================================================================*/
/* Scheduling                                                                */
/*===========================================================================*/

typedef enum {
    ACT_NONE = 0,
    ACT_PTT_ON,    /* first, so the PTT never trails the carrier */
    ACT_DEADLINE,
    ACT_PTT_OFF,   /* last, so a key-down at the same instant cancels it */
} action_t;

static action_t next_action(const cwnet_play_t *play, int64_t *at_ms) {
    action_t best = ACT_NONE;
    int64_t best_at = 0;

    if (play->ptt_on_pending) {
        best = ACT_PTT_ON;
        best_at = play->ptt_on_at_ms;
    }
    if (play->state == CWNET_PLAY_RUNNING) {
        if (best == ACT_NONE || play->deadline_ms < best_at) {
            best = ACT_DEADLINE;
            best_at = play->deadline_ms;
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
    arm_if_idle(play);
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
        arm_if_idle(play);

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
            case ACT_DEADLINE:
                do_deadline(play, at_ms, out);
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
