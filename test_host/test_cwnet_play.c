/**
 * @file test_cwnet_play.c
 * @brief The playback engine against the reference capture and the codec
 *
 * Every expected instant below is derived BY HAND from the 7-bit waits the
 * bytes carry (cwnet_timestamp.h, CwStreamEnc.c), never read back from the
 * engine. The derivation is written out next to each scenario so it can be
 * checked without redoing it.
 *
 * The clock base T0 sits above 2^31 ms on purpose: a 32-bit instant would
 * wrap there and every assertion would move.
 */

#include "unity.h"
#include "cwnet_play.h"
#include "cwnet_frame.h"
#include "cwnet_timestamp.h"
#include "cwnet_fixtures.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Beyond 2^31 - 1 = 2147483647: instants and deadlines are 64-bit */
#define T0 ((int64_t)4000000000)

#define B_MS 50u
#define TAIL_MS 100u

/*===========================================================================*/
/* Harness: a simulated clock, and the events the engine hands back          */
/*===========================================================================*/

typedef struct {
    cwnet_play_event_t ev[64];
    size_t n;
} evlog_t;

typedef struct {
    cwnet_play_event_type_t type;
    int64_t at;
} exp_ev_t;

static void log_append(evlog_t *log, const cwnet_play_result_t *r) {
    for (size_t i = 0; i < r->count; i++) {
        TEST_ASSERT_LESS_THAN_size_t(64u, log->n);
        log->ev[log->n++] = r->ev[i];
    }
}

/**
 * @brief Run the engine on an ideal scheduler: tick exactly at each deadline
 */
static void run_until(cwnet_play_t *play, evlog_t *log, int64_t until_ms) {
    for (int guard = 0; guard < 500; guard++) {
        int64_t next = 0;
        if (!cwnet_play_next_deadline(play, &next) || next > until_ms) {
            return;
        }
        cwnet_play_result_t r;
        cwnet_play_tick(play, next, &r);
        log_append(log, &r);
    }
    TEST_FAIL_MESSAGE("run_until did not settle");
}

static const char *ev_name(cwnet_play_event_type_t t) {
    switch (t) {
        case CWNET_PLAY_EV_KEY_DOWN:      return "KEY_DOWN";
        case CWNET_PLAY_EV_KEY_UP:        return "KEY_UP";
        case CWNET_PLAY_EV_PTT_ON:        return "PTT_ON";
        case CWNET_PLAY_EV_PTT_OFF:       return "PTT_OFF";
        case CWNET_PLAY_EV_END_OF_OVER:   return "END_OF_OVER";
        case CWNET_PLAY_EV_OVER_FINISHED: return "OVER_FINISHED";
        case CWNET_PLAY_EV_LATE_BYTE:     return "LATE_BYTE";
        default:                          return "?";
    }
}

static void assert_log(const evlog_t *log, const exp_ev_t *exp, size_t n) {
    char msg[128];
    for (size_t i = 0; i < n && i < log->n; i++) {
        snprintf(msg, sizeof msg, "event %zu: got %s at T0%+lld, want %s at T0%+lld",
                 i, ev_name(log->ev[i].type), (long long)(log->ev[i].at_ms - T0),
                 ev_name(exp[i].type), (long long)(exp[i].at - T0));
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)exp[i].type, (int)log->ev[i].type, msg);
        TEST_ASSERT_EQUAL_INT64_MESSAGE(exp[i].at, log->ev[i].at_ms, msg);
    }
    snprintf(msg, sizeof msg, "event count: got %zu, want %zu", log->n, n);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(n, log->n, msg);
}

static size_t count_of(const evlog_t *log, cwnet_play_event_type_t t) {
    size_t n = 0;
    for (size_t i = 0; i < log->n; i++) {
        if (log->ev[i].type == t) {
            n++;
        }
    }
    return n;
}

static void push_bytes(cwnet_play_t *play, const uint8_t *bytes, size_t len, int64_t at_ms) {
    for (size_t i = 0; i < len; i++) {
        TEST_ASSERT_TRUE(cwnet_play_push(play, bytes[i], at_ms));
    }
}

/**
 * @brief Push the payload of every MORSE frame of a capture slice
 *
 * This is the path the daemon walks: capture bytes -> frame parser ->
 * keying bytes -> engine.
 */
static void push_morse_payload(cwnet_play_t *play, const uint8_t *frames, size_t len,
                               int64_t at_ms) {
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);
    size_t off = 0;
    while (off < len) {
        cwnet_parse_result_t r = cwnet_frame_parse(&parser, frames + off, len - off);
        TEST_ASSERT_EQUAL(CWNET_PARSE_OK, r.status);
        TEST_ASSERT_EQUAL(CWNET_CMD_MORSE, r.command);
        push_bytes(play, r.payload, r.payload_len, at_ms);
        off += r.bytes_consumed;
    }
}

/**
 * @brief Push the MORSE bytes of ref_first_over, through the fixture helper
 *
 * ref_morse_frames() keeps the MORSE frames of the capture slice and drops
 * the two rigctld set_ptt frames the reference interleaves: those never
 * reach this module (R10).
 */
static void push_first_over_morse(cwnet_play_t *play, int64_t at_ms) {
    uint8_t morse[sizeof ref_first_over];
    size_t len = 0;
    TEST_ASSERT_TRUE(ref_morse_frames(ref_first_over, sizeof ref_first_over, morse, &len));
    push_morse_payload(play, morse, len, at_ms);
}

/**
 * @brief The MORSE payload bytes of ref_first_over, in order
 *
 * The same path as push_first_over_morse(), stopping one step earlier: the
 * scenarios that deliver the over live need the bytes one at a time, each
 * with an instant of its own.
 */
static void first_over_morse_bytes(uint8_t *out, size_t cap, size_t *out_len) {
    uint8_t morse[sizeof ref_first_over];
    size_t len = 0;
    TEST_ASSERT_TRUE(ref_morse_frames(ref_first_over, sizeof ref_first_over, morse, &len));

    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);
    size_t off = 0;
    *out_len = 0;
    while (off < len) {
        cwnet_parse_result_t r = cwnet_frame_parse(&parser, morse + off, len - off);
        TEST_ASSERT_EQUAL(CWNET_PARSE_OK, r.status);
        TEST_ASSERT_EQUAL(CWNET_CMD_MORSE, r.command);
        for (size_t i = 0; i < r.payload_len; i++) {
            TEST_ASSERT_LESS_THAN_size_t(cap, *out_len);
            out[(*out_len)++] = r.payload[i];
        }
        off += r.bytes_consumed;
    }
}

static void play_setup(cwnet_play_t *play, uint32_t lead_ms, uint32_t buffer_ms) {
    cwnet_play_cfg_t cfg = { .ptt_lead_ms = lead_ms, .ptt_tail_ms = TAIL_MS };
    cwnet_play_init(play, &cfg);
    cwnet_play_start_over(play, buffer_ms);
}

/*===========================================================================*/
/* AE2: the first over of the capture                                        */
/*===========================================================================*/

/*
 * ref_first_over, session 12 of the 2026-09-05 capture: the letter A at
 * 25 WPM (dot 48 ms). Its five MORSE bytes and their hand decode:
 *
 *   0x80  bit7=1 key down, 0x00 -> linear range        ->    0 ms
 *   0x24  bit7=0 key up,   0x24 -> 32 + 4*(0x24-0x20)  ->   48 ms
 *   0xA4  bit7=1 key down, 0x24 -> same                ->   48 ms
 *   0x3C  bit7=0 key up,   0x3C -> 32 + 4*(0x3C-0x20)  ->  144 ms
 *   0x60  bit7=0 key up,   0x60 -> 157 + 16*(0x60-0x40) -> 669 ms
 *
 * With B = 50 the timeline is, from the instant the first byte arrived:
 *
 *   first byte: T0 + wait(0) + B          = T0 +  50  key down
 *   += 48                                 = T0 +  98  key up
 *   += 48                                 = T0 + 146  key down
 *   += 144                                = T0 + 290  key up
 *   0x60 is the second key-up in a row    = T0 + 290  end of over
 *   last key-up + tail(100)               = T0 + 390  PTT off, over finished
 *
 * B delays the first edge and nothing else: 98 - 50 = 48 is the dot itself.
 */
void test_play_first_over_of_the_capture_plays_at_the_encoded_instants(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    push_first_over_morse(&play, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 1000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,        T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN,      T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,        T0 + 98 },
        { CWNET_PLAY_EV_KEY_DOWN,      T0 + 146 },
        { CWNET_PLAY_EV_KEY_UP,        T0 + 290 },
        { CWNET_PLAY_EV_END_OF_OVER,   T0 + 290 },
        { CWNET_PLAY_EV_PTT_OFF,       T0 + 390 },
        { CWNET_PLAY_EV_OVER_FINISHED, T0 + 390 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);

    TEST_ASSERT_FALSE(cwnet_play_key_down(&play));
    TEST_ASSERT_FALSE(cwnet_play_ptt_on(&play));
    TEST_ASSERT_FALSE(cwnet_play_over_open(&play));
}

/*
 * Same bytes, one late tick: a caller that wakes 1000 ms after the over
 * started still reports every edge at the instant it belonged to. The
 * deadline advances by the decoded wait, never by the measured time.
 */
void test_play_a_late_tick_does_not_move_the_edges(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    push_first_over_morse(&play, T0);

    evlog_t log = { .n = 0 };
    cwnet_play_result_t r;
    cwnet_play_tick(&play, T0 + 1000, &r);
    log_append(&log, &r);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,        T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN,      T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,        T0 + 98 },
        { CWNET_PLAY_EV_KEY_DOWN,      T0 + 146 },
        { CWNET_PLAY_EV_KEY_UP,        T0 + 290 },
        { CWNET_PLAY_EV_END_OF_OVER,   T0 + 290 },
        { CWNET_PLAY_EV_PTT_OFF,       T0 + 390 },
        { CWNET_PLAY_EV_OVER_FINISHED, T0 + 390 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);
}

/*
 * A byte that arrives late, but before the deadline that will consume it,
 * does not drag the timeline with it: 0xA4 arrives at T0+90 and its edge
 * still lands 48 ms after the key-up it follows, at T0+146, not at T0+138.
 */
void test_play_a_late_byte_does_not_move_the_deadline_it_follows(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t first[] = { 0x80, 0x24 };
    push_bytes(&play, first, sizeof first, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 50);      /* key down at T0+50 */

    const uint8_t late[] = { 0xA4, 0x24 };
    push_bytes(&play, late, sizeof late, T0 + 90);

    run_until(&play, &log, T0 + 400);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,   T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 98 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 146 },   /* not T0+138: 98 + 48, not 90 + 48 */
        { CWNET_PLAY_EV_KEY_UP,   T0 + 194 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 294 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);
}

/*===========================================================================*/
/* AE9: the same over, delivered as it is keyed                              */
/*===========================================================================*/

/*
 * The five bytes of ref_first_over again, but arriving the way a live fist
 * produces them: the client sends a byte AT each edge, so byte i leaves the
 * key at the instant of its own edge and reaches us a link latency later.
 * From the hand decode above, the sender's edges are the running sum of the
 * waits:
 *
 *   0x80  ->    0   key down at   0
 *   0x24  ->   48   key up   at  48
 *   0xA4  ->   48   key down at  96
 *   0x3C  ->  144   key up   at 240
 *   0x60  ->  669   the marker,  at 909
 *
 * With a constant 30 ms of one-way latency every byte lands 30 ms after its
 * own edge, so taking the reception of the first byte as T0 the others land
 * at T0+48, +96, +240 and +909. B = 100 ms.
 *
 *   first byte: T0 + wait(0) + B(100)     = T0 + 100  key down
 *   += 48                                 = T0 + 148  key up
 *   += 48                                 = T0 + 196  key down
 *
 * At T0+196 the dash starts and the FIFO IS EMPTY: 0x3C is still under the
 * operator's finger and will not exist until T0+240. That is the normal
 * state of a live over, not an underrun — every byte arrives B ms before
 * its own deadline, and an element longer than B empties the FIFO by
 * construction (R8). The state applied holds:
 *
 *   += 144 (0x3C lands at +240, its deadline is +340, so it is not late)
 *                                         = T0 + 340  key up
 *   last key-up + tail(100)               = T0 + 440  PTT off
 *   0x60 lands at +909: second key-up in a row, consumed on arrival
 *                                         = T0 + 909  end of over, finished
 *
 * The edges are 48, 48 and 144 apart, exactly as in AE2: what the operator
 * sent is what goes on the air. Nothing was late and nothing faulted.
 */
void test_play_an_over_delivered_as_it_is_keyed_plays_like_a_buffered_one(void) {
    uint8_t bytes[16];
    size_t len = 0;
    first_over_morse_bytes(bytes, sizeof bytes, &len);

    /* The bytes the hand decode above is written against */
    static const uint8_t want_bytes[] = { 0x80, 0x24, 0xA4, 0x3C, 0x60 };
    TEST_ASSERT_EQUAL_size_t(sizeof want_bytes, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want_bytes, bytes, sizeof want_bytes);

    /* Sender's edge instants, the running sum of the decoded waits */
    static const int64_t edge_at[] = { 0, 48, 96, 240, 909 };

    cwnet_play_t play;
    play_setup(&play, 0u, 100u);

    evlog_t log = { .n = 0 };
    for (size_t i = 0; i < len; i++) {
        /* Everything the engine owes is done before the next byte lands */
        run_until(&play, &log, T0 + edge_at[i]);
        TEST_ASSERT_TRUE(cwnet_play_push(&play, bytes[i], T0 + edge_at[i]));
    }
    run_until(&play, &log, T0 + 2000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,        T0 + 100 },
        { CWNET_PLAY_EV_KEY_DOWN,      T0 + 100 },
        { CWNET_PLAY_EV_KEY_UP,        T0 + 148 },
        { CWNET_PLAY_EV_KEY_DOWN,      T0 + 196 },
        { CWNET_PLAY_EV_KEY_UP,        T0 + 340 },
        { CWNET_PLAY_EV_PTT_OFF,       T0 + 440 },
        { CWNET_PLAY_EV_END_OF_OVER,   T0 + 909 },
        { CWNET_PLAY_EV_OVER_FINISHED, T0 + 909 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);

    /* The two things AE9 is for: no byte was late, nothing faulted */
    TEST_ASSERT_EQUAL_UINT32(0u, cwnet_play_late_bytes(&play));
    TEST_ASSERT_EQUAL_INT64(0, cwnet_play_late_ms(&play));
    TEST_ASSERT_EQUAL_size_t(0u, count_of(&log, CWNET_PLAY_EV_LATE_BYTE));
}

/*
 * The same defect at the buffer the station actually runs: B = 50, the
 * letter A of the capture keyed live. The dash lasts 144 ms, so the FIFO is
 * empty for 94 ms in the middle of it, and the edges must still be the ones
 * of AE2. Read as an underrun instead, the dash comes out with a length of
 * zero and the station writes a fault for a link that is working.
 */
void test_play_an_element_longer_than_the_buffer_is_not_an_underrun(void) {
    static const uint8_t bytes[]   = { 0x80, 0x24, 0xA4, 0x3C };
    static const int64_t edge_at[] = {    0,   48,   96,  240 };

    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    evlog_t log = { .n = 0 };
    for (size_t i = 0; i < sizeof bytes / sizeof bytes[0]; i++) {
        run_until(&play, &log, T0 + edge_at[i]);
        TEST_ASSERT_TRUE(cwnet_play_push(&play, bytes[i], T0 + edge_at[i]));
    }
    run_until(&play, &log, T0 + 2000);

    /* B(50) + 0, then the dot, the space and the dash: 48, 48, 144 */
    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,   T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 98 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 146 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 290 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 390 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, cwnet_play_late_bytes(&play));
}

/*===========================================================================*/
/* Two keying events per frame, the shape a server sees at speed             */
/*===========================================================================*/

/*
 * ref_two_event_frames, session 12 bytes 6982..6989: two frames of two
 * events each from the fast part of the session (dot about 20 ms).
 *
 *   0x96  key down, 0x16 = 22 -> linear ->  22 ms
 *   0x12  key up,   0x12 = 18 -> linear ->  18 ms
 *
 * With B = 0, from the instant they arrived:
 *   first byte: T0 + wait(22) + B(0) = T0 + 22   key down
 *   += 18                            = T0 + 40   key up
 *   += 22                            = T0 + 62   key down
 *   += 18                            = T0 + 80   key up
 * and the PTT drops a tail after the last key-up, T0 + 180.
 */
void test_play_two_event_frames_play_at_their_encoded_distances(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, 0u);

    push_morse_payload(&play, ref_two_event_frames, sizeof ref_two_event_frames, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 500);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,   T0 + 22 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 22 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 40 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 62 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 80 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 180 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);
}

/*===========================================================================*/
/* A wait split over several bytes                                           */
/*===========================================================================*/

/*
 * Case C of tools/cwnet/keyer_sim.c, the reference KeyerThread encoding a
 * 2000 ms gap at dot 240 ms: 80 14 FF EA 21. The gap is longer than one
 * byte can carry, so the encoder emits it as 1165 + the rest, both bytes
 * carrying the state the gap ends in (CwStreamEnc.c:135-146).
 *
 *   0x80  key down, 0x00                                ->    0 ms
 *   0x14  key up,   0x14 = 20 -> linear                 ->   20 ms
 *   0xFF  key down, 0x7F -> 157 + 16*(0x7F-0x40) = 1165 -> 1165 ms, split
 *   0xEA  key down, 0x6A -> 157 + 16*(0x6A-0x40)        ->  829 ms
 *   0x21  key up,   0x21 -> 32 + 4*(0x21-0x20)          ->   36 ms
 *
 * The split carries ONE edge, after the sum: 1165 + 829 = 1994.
 *   first byte: T0 + 0 + B(50)  = T0 +   50  key down
 *   += 20                       = T0 +   70  key up
 *   += 1165 (no edge, split)    = T0 + 1235
 *   += 829                      = T0 + 2064  key down   (= 70 + 1994)
 *   += 36                       = T0 + 2100  key up
 *
 * The PTT drops a tail after each key-up and rises again with the key:
 * T0+170, then T0+2064, then T0+2200.
 */
void test_play_a_split_wait_makes_one_edge_after_the_sum(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t case_c[] = { 0x80, 0x14, 0xFF, 0xEA, 0x21 };
    push_bytes(&play, case_c, sizeof case_c, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 3000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,   T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 70 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 170 },
        { CWNET_PLAY_EV_PTT_ON,   T0 + 2064 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 2064 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 2100 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 2200 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);

    /* Two key-downs, not three: the 1165 ms byte is not an edge of its own */
    TEST_ASSERT_EQUAL_size_t(2u, count_of(&log, CWNET_PLAY_EV_KEY_DOWN));
    /* Two key-downs in a row are never an end of over */
    TEST_ASSERT_EQUAL_size_t(0u, count_of(&log, CWNET_PLAY_EV_END_OF_OVER));
}

/*
 * The other half of the split: an ELEMENT longer than one byte can carry.
 * Holding the key down to tune is ordinary practice, and at 1994 ms the
 * key-up edge that closes it does not fit one byte either. send_morse()
 * splits it exactly as CwStream_EncodeKeyUpDownEvent does — a full chunk
 * of 1165 ms, then the remainder — and both chunks carry the state the
 * wait ends in, up (cwnet_client.c:157-186).
 *
 *   0x80  key down, 0x00                                 ->    0 ms
 *   0x7F  key up,   0x7F -> 157 + 16*(0x7F-0x40) = 1165  -> 1165 ms, split
 *   0x6A  key up,   0x6A -> 157 + 16*(0x6A-0x40)         ->  829 ms
 *
 * Two key-up bytes in a row, and they are NOT the end-of-over marker: the
 * first spends its time in the state the key is already in, so the single
 * edge lands after the sum, 1165 + 829 = 1994.
 *
 *   first byte: T0 + 0 + B(50)   = T0 +   50  key down
 *   += 1165 (no edge, split)     = T0 + 1215
 *   += 829                       = T0 + 2044  key up   (= 50 + 1994)
 *   last key-up + tail(100)      = T0 + 2144  PTT off
 *
 * Taken for an end of over at T0+1215 instead, the over goes to CLOSING
 * with the key still down, the PTT still up and nothing scheduled: the
 * carrier stays on air until something outside this module notices.
 */
void test_play_a_key_down_longer_than_one_byte_is_not_an_end_of_over(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t long_down[] = { 0x80, 0x7F, 0x6A };
    push_bytes(&play, long_down, sizeof long_down, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 3000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,   T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 2044 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 2144 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);

    /* One edge for one element, and no end of over anywhere near it */
    TEST_ASSERT_EQUAL_size_t(1u, count_of(&log, CWNET_PLAY_EV_KEY_UP));
    TEST_ASSERT_EQUAL_size_t(0u, count_of(&log, CWNET_PLAY_EV_END_OF_OVER));
    TEST_ASSERT_FALSE(cwnet_play_key_down(&play));
    TEST_ASSERT_FALSE(cwnet_play_ptt_on(&play));
}


/*
 * The same 1994 ms hold as the test above, but delivered the way a client
 * that tunes up actually delivers it: the operator leans on the key, and
 * NOTHING goes out until the release. Then the whole split wait arrives in
 * one frame, at once, and every chunk of it is already past the deadline
 * its own 1165 ms would have had.
 *
 *   0x80  key down, 0x00                                 pushed at T0
 *   0x7F  key up,   0x7F -> 1165 ms, chunk of the wait   pushed at T0 + 2010
 *   0x6A  key up,   0x6A ->  829 ms, the edge            pushed at T0 + 2010
 *   0x06  key up,   0x06 ->    6 ms, the end-of-over mark
 *
 * A chunk carries no edge: its only job is to move the timeline on. So the
 * deadline that means anything is the EDGE's, and it is the previous edge
 * plus the sum of the whole group — T0 + 50 + 1165 + 829 = T0 + 2044 —
 * which the group beats by 34 ms. Nothing here is late, and the key stays
 * down 1994 ms, the 2000 ms hold as the 7-bit codec can express it.
 *
 * Judging each chunk against a deadline of its own instead, the 1165 ms
 * one is 795 ms "late", the timeline restarts from the arrival, and the
 * 829 ms remainder is spent from there: the key goes up at T0 + 2839 and
 * the station keys on for 2789 ms of a 2000 ms hold. That is a carrier
 * left in the air three quarters of a second after the operator let go.
 */
void test_play_a_split_wait_delivered_at_the_release_keys_the_hold(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t down[] = { 0x80 };
    push_bytes(&play, down, sizeof down, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 2010);
    TEST_ASSERT_TRUE(cwnet_play_key_down(&play));

    /* The release: the whole wait, in one frame, 2010 ms after the client
     * keyed down — B ahead of the edge it carries, like any other byte. */
    const uint8_t release[] = { 0x7F, 0x6A, 0x06 };
    push_bytes(&play, release, sizeof release, T0 + 2010);
    run_until(&play, &log, T0 + 4000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,       T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN,     T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,       T0 + 2044 },
        { CWNET_PLAY_EV_END_OF_OVER,  T0 + 2044 },
        { CWNET_PLAY_EV_PTT_OFF,      T0 + 2144 },
        { CWNET_PLAY_EV_OVER_FINISHED, T0 + 2144 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);

    /* 1994 ms of carrier for a 1994 ms hold, and not a byte late: the
     * chunk had no deadline to miss. */
    TEST_ASSERT_EQUAL_UINT32(0u, cwnet_play_late_bytes(&play));
    TEST_ASSERT_EQUAL_INT64(0, cwnet_play_late_ms(&play));
    TEST_ASSERT_EQUAL_size_t(0u, count_of(&log, CWNET_PLAY_EV_LATE_BYTE));
}

/*
 * R8 still applies to the group, it just applies to it ONCE. The same
 * release, held back on the link until T0 + 2500 — 456 ms past the edge's
 * deadline of T0 + 2044. The edge comes out on arrival, the element is
 * longer by the delay, and the delay is counted: one late byte, 456 ms,
 * not one per chunk and not the arrival taken as a fresh timeline.
 */
void test_play_a_split_wait_that_arrives_late_makes_its_edge_on_arrival(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t down[] = { 0x80 };
    push_bytes(&play, down, sizeof down, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 2500);

    const uint8_t release[] = { 0x7F, 0x6A };
    push_bytes(&play, release, sizeof release, T0 + 2500);
    run_until(&play, &log, T0 + 4000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,   T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 50 },
        { CWNET_PLAY_EV_LATE_BYTE, T0 + 2500 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 2500 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 2600 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);

    TEST_ASSERT_EQUAL_UINT32(1u, cwnet_play_late_bytes(&play));
    TEST_ASSERT_EQUAL_INT64(2500 - 2044, cwnet_play_late_ms(&play));
}

/*===========================================================================*/
/* AE6: a key held down, and a byte that comes late                          */
/*===========================================================================*/

/*
 * The operator holds the key down to tune up, and for ten seconds nothing
 * else reaches the station. That is not a fault and it has no deadline: an
 * empty FIFO under a key-down is a finger that has not lifted yet, and the
 * keying stream cannot tell that finger from a client that died. The PING
 * can, so the engine holds and the release comes from the caller (R8,
 * KTD3).
 *
 * The bytes are the ones a live client actually sends. A wait longer than
 * one byte can carry is split by the encoder as it elapses
 * (CwStreamEnc.c:135-146): a chunk of 0x7F, worth CWSTREAM_MAX_WAIT_MS =
 * 1165 ms, every time the wait in progress passes that, then the
 * remainder. Ten seconds is 8 x 1165 = 9320 plus 680, and 680 encodes as
 * 0x60 which decodes back to 669: the element comes out 9320 + 669 =
 * 9989 ms long, the hold as the 7-bit codec can express it.
 *
 *   T0 + B(50)                          key down
 *   + 8 chunks and the remainder        key up at T0 + 50 + 9989
 *   + tail(100)                         PTT off
 */
void test_play_a_key_held_down_holds_until_the_byte_that_lifts_it(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t down[] = { 0x80 };
    push_bytes(&play, down, sizeof down, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 5000);

    /* Halfway through the hold: the key is down, and the engine has nothing
     * at all in its diary. That empty diary IS the decision — with a grace
     * armed there would be an instant here, and it would lift the key while
     * the operator is still leaning on it. */
    TEST_ASSERT_TRUE(cwnet_play_key_down(&play));
    int64_t next = 0;
    TEST_ASSERT_FALSE(cwnet_play_next_deadline(&play, &next));

    /* The chunks of the wait, each sent when the encoder would send it */
    for (int k = 1; k <= 8; k++) {
        int64_t at = T0 + 1165 * k;
        run_until(&play, &log, at);
        TEST_ASSERT_TRUE(cwnet_play_push(&play, 0x7F, at));
    }
    run_until(&play, &log, T0 + 10000);
    TEST_ASSERT_TRUE(cwnet_play_key_down(&play));

    /* 10000 - 9320 = 680 ms left, which encodes as 0x60 (669 ms) */
    TEST_ASSERT_TRUE(cwnet_play_push(&play, 0x60, T0 + 10000));
    run_until(&play, &log, T0 + 12000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,   T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 50 + 9989 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 50 + 9989 + (int64_t)TAIL_MS },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);

    /* Ten seconds of holding, and not one byte was late: every chunk landed
     * its own B ahead of the deadline it carried. */
    TEST_ASSERT_EQUAL_UINT32(0u, cwnet_play_late_bytes(&play));
    TEST_ASSERT_EQUAL_INT64(0, cwnet_play_late_ms(&play));
}

/*
 * The other end of the same hold: the client stops answering, the caller's
 * PINGs declare it dead (R16) and it forces the release. From a held
 * key-down — the state the test above leaves the engine in — that means the
 * key up at that instant and the PTT a tail later, not at some deadline of
 * the engine's own.
 */
void test_play_force_release_lifts_a_held_key_at_the_instant_it_is_called(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t down[] = { 0x80 };
    push_bytes(&play, down, sizeof down, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 6000);
    TEST_ASSERT_TRUE(cwnet_play_key_down(&play));

    cwnet_play_result_t r;
    cwnet_play_force_release(&play, T0 + 6000, &r);
    log_append(&log, &r);
    run_until(&play, &log, T0 + 8000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,        T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN,      T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,        T0 + 6000 },
        { CWNET_PLAY_EV_PTT_OFF,       T0 + 6100 },
        { CWNET_PLAY_EV_OVER_FINISHED, T0 + 6100 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);
    TEST_ASSERT_FALSE(cwnet_play_over_open(&play));
}

/*
 * The last part of AE6. The key-up that ends the dot is due at T0 + 98,
 * 48 ms after the key-down at T0 + 50, and the link hiccups: it arrives at
 * T0 + 128, 30 ms past its own deadline. The element cannot be shortened
 * back into the past, so it is applied on arrival — the dot comes out
 * 78 ms long instead of 48 — and the 30 ms are counted. This is jitter,
 * not a fault: nothing but the edge itself is reported.
 */
void test_play_a_byte_past_its_deadline_makes_its_edge_on_arrival(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t only_down[] = { 0x80 };
    push_bytes(&play, only_down, sizeof only_down, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 128);

    const uint8_t late_up[] = { 0x24 };
    push_bytes(&play, late_up, sizeof late_up, T0 + 128);
    run_until(&play, &log, T0 + 1000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,    T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN,  T0 + 50 },
        { CWNET_PLAY_EV_LATE_BYTE, T0 + 128 },
        { CWNET_PLAY_EV_KEY_UP,    T0 + 128 },
        { CWNET_PLAY_EV_PTT_OFF,   T0 + 228 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);

    /* One byte late, by the 30 ms between its deadline and its arrival */
    TEST_ASSERT_EQUAL_UINT32(1u, cwnet_play_late_bytes(&play));
    TEST_ASSERT_EQUAL_INT64(30, cwnet_play_late_ms(&play));
}

/*===========================================================================*/
/* End of over                                                               */
/*===========================================================================*/

/*
 * Two consecutive key-up bytes end the over, and the second one is not
 * waited out: 0x80, 0x24, 0x24 gives key down at T0+50, key up at T0+98,
 * and the end of over at T0+98 even though the last byte carries 48 ms.
 * The key is released when the PTT has dropped, T0+198.
 */
void test_play_end_of_over_does_not_wait_out_the_marker(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t bytes[] = { 0x80, 0x24, 0x24 };
    push_bytes(&play, bytes, sizeof bytes, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 1000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,        T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN,      T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,        T0 + 98 },
        { CWNET_PLAY_EV_END_OF_OVER,   T0 + 98 },
        { CWNET_PLAY_EV_PTT_OFF,       T0 + 198 },
        { CWNET_PLAY_EV_OVER_FINISHED, T0 + 198 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);
}

/*
 * Key-ups separated by a key-down are ordinary keying, not an end of over:
 * 0x80 0x24 0xA4 0x24 plays four edges and leaves the over open.
 */
void test_play_key_ups_split_by_a_key_down_are_not_an_end_of_over(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t bytes[] = { 0x80, 0x24, 0xA4, 0x24 };
    push_bytes(&play, bytes, sizeof bytes, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 1000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,   T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 98 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 146 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 194 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 294 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);
    TEST_ASSERT_EQUAL_size_t(0u, count_of(&log, CWNET_PLAY_EV_END_OF_OVER));
    TEST_ASSERT_EQUAL_size_t(0u, count_of(&log, CWNET_PLAY_EV_OVER_FINISHED));
}

/*===========================================================================*/
/* PTT lead                                                                  */
/*===========================================================================*/

/*
 * A 20 ms lead with B = 50: the first key-down is known 50 ms ahead, so
 * the PTT can rise at T0 + 50 - 20 = T0 + 30 and the key still goes down
 * at T0 + 50.
 */
void test_play_ptt_lead_raises_the_ptt_before_the_first_key_down(void) {
    cwnet_play_t play;
    play_setup(&play, 20u, B_MS);

    const uint8_t bytes[] = { 0x80, 0x24 };
    push_bytes(&play, bytes, sizeof bytes, T0);

    int64_t next = 0;
    TEST_ASSERT_TRUE(cwnet_play_next_deadline(&play, &next));
    TEST_ASSERT_EQUAL_INT64(T0 + 30, next);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 1000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,   T0 + 30 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 98 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 198 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);
}

/*
 * A lead longer than B would put the PTT before the byte arrived: it is
 * clamped to B, so with lead 80 and B 50 the PTT rises at T0 exactly.
 */
void test_play_ptt_lead_is_clamped_to_the_buffer(void) {
    cwnet_play_t play;
    play_setup(&play, 80u, B_MS);

    const uint8_t bytes[] = { 0x80, 0x24 };
    push_bytes(&play, bytes, sizeof bytes, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 1000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,   T0 + 0 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 98 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 198 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);
}

/*===========================================================================*/
/* Back pressure and the safety nets                                         */
/*===========================================================================*/

/*
 * The FIFO holds CWNET_PLAY_FIFO_SIZE bytes, the reference's size. What
 * does not fit is dropped and counted, and nothing spurious comes out.
 */
void test_play_a_full_fifo_drops_the_byte_and_counts_it(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    /* The byte being played is held outside the FIFO, so the queue takes
     * CWNET_PLAY_FIFO_SIZE more once the over is anchored. */
    for (size_t i = 0; i <= CWNET_PLAY_FIFO_SIZE; i++) {
        uint8_t b = (i == 0) ? 0x80u : ((i & 1u) ? 0x24u : 0xA4u);
        TEST_ASSERT_TRUE(cwnet_play_push(&play, b, T0));
    }
    TEST_ASSERT_FALSE(cwnet_play_push(&play, 0x24u, T0));
    TEST_ASSERT_FALSE(cwnet_play_push(&play, 0xA4u, T0));
    TEST_ASSERT_EQUAL_UINT32(2u, cwnet_play_dropped(&play));

    /* Nothing is due before the first deadline at T0 + B */
    cwnet_play_result_t r;
    cwnet_play_tick(&play, T0 + 49, &r);
    TEST_ASSERT_EQUAL_size_t(0u, r.count);
}

/*
 * force_release with the key down: up at once, and the PTT down a tail
 * later. This is the shape U3 needs when the holder's TCP dies mid-over.
 */
void test_play_force_release_lifts_the_key_and_drops_the_ptt_at_the_tail(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t bytes[] = { 0x80, 0x3C };
    push_bytes(&play, bytes, sizeof bytes, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 60);
    TEST_ASSERT_TRUE(cwnet_play_key_down(&play));

    cwnet_play_result_t r;
    cwnet_play_force_release(&play, T0 + 100, &r);
    log_append(&log, &r);

    run_until(&play, &log, T0 + 1000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,        T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN,      T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,        T0 + 100 },
        { CWNET_PLAY_EV_PTT_OFF,       T0 + 200 },
        { CWNET_PLAY_EV_OVER_FINISHED, T0 + 200 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);
    TEST_ASSERT_FALSE(cwnet_play_over_open(&play));
}

/*
 * force_release on an engine that is not playing: nothing on the key,
 * nothing on the PTT, the over is finished there and then.
 */
void test_play_force_release_when_idle_finishes_the_over_at_once(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    cwnet_play_result_t r;
    cwnet_play_force_release(&play, T0 + 7, &r);

    TEST_ASSERT_EQUAL_size_t(1u, r.count);
    TEST_ASSERT_EQUAL_INT(CWNET_PLAY_EV_OVER_FINISHED, (int)r.ev[0].type);
    TEST_ASSERT_EQUAL_INT64(T0 + 7, r.ev[0].at_ms);

    int64_t next = 0;
    TEST_ASSERT_FALSE(cwnet_play_next_deadline(&play, &next));
}

/*
 * start_over fixes B for the over and throws away what was queued: the
 * bytes of an interrupted over never leak into the next one.
 */
void test_play_start_over_fixes_the_buffer_and_clears_the_queue(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t stale[] = { 0x80, 0x24, 0xA4 };
    push_bytes(&play, stale, sizeof stale, T0);

    cwnet_play_start_over(&play, 200u);
    int64_t next = 0;
    TEST_ASSERT_FALSE(cwnet_play_next_deadline(&play, &next));

    const uint8_t fresh[] = { 0x80, 0x24 };
    push_bytes(&play, fresh, sizeof fresh, T0 + 1000);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 3000);

    /* B = 200 now: T0+1000 + 0 + 200, then the dot's own 48 ms */
    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,   T0 + 1200 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 1200 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 1248 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 1348 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);
}

/*
 * Every entry point survives a NULL engine and reports nothing.
 */
void test_play_survives_null_and_reports_nothing(void) {
    cwnet_play_result_t r;
    int64_t next = 0;

    cwnet_play_init(NULL, NULL);
    cwnet_play_start_over(NULL, 50u);
    TEST_ASSERT_FALSE(cwnet_play_push(NULL, 0x80u, T0));
    TEST_ASSERT_FALSE(cwnet_play_next_deadline(NULL, &next));
    cwnet_play_tick(NULL, T0, &r);
    cwnet_play_force_release(NULL, T0, &r);
    TEST_ASSERT_FALSE(cwnet_play_key_down(NULL));
    TEST_ASSERT_FALSE(cwnet_play_ptt_on(NULL));
    TEST_ASSERT_FALSE(cwnet_play_over_open(NULL));
    TEST_ASSERT_EQUAL_UINT32(0u, cwnet_play_dropped(NULL));

    cwnet_play_t play;
    cwnet_play_init(&play, NULL);   /* NULL cfg -> defaults */
    TEST_ASSERT_FALSE(cwnet_play_next_deadline(&play, NULL));
    cwnet_play_tick(&play, T0, NULL);

    /* The default tail is the box's 100 ms */
    cwnet_play_start_over(&play, B_MS);
    const uint8_t bytes[] = { 0x80, 0x24 };
    push_bytes(&play, bytes, sizeof bytes, T0);
    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 1000);
    TEST_ASSERT_EQUAL_size_t(4u, log.n);
    TEST_ASSERT_EQUAL_INT(CWNET_PLAY_EV_PTT_OFF, (int)log.ev[3].type);
    TEST_ASSERT_EQUAL_INT64(T0 + 198, log.ev[3].at_ms);
}

/*===========================================================================*/
/* The invariant: at rest with the key down means a held element             */
/*===========================================================================*/

/**
 * @brief Tick once, then check the one state that must never exist
 *
 * A key down with an empty FIFO and nothing scheduled is legitimate: the
 * byte that ends the element is under a finger that has not lifted, and the
 * engine has no timer for that (R8, KTD3). What is never legitimate is the
 * key being down while the engine believes an over is over — CLOSING, its
 * queue thrown away and only the PTT left to drop, or IDLE, waiting for a
 * byte to anchor a new over on. In those two the key would be down with
 * nobody left who will ever key it up, and that is a stuck carrier
 * (ARCHITECTURE.md 8.1, and worse than either side of it).
 */
static void tick_and_check_invariant(cwnet_play_t *play, int64_t now_ms) {
    cwnet_play_result_t r;
    cwnet_play_tick(play, now_ms, &r);

    if (play->key_down && play->state != CWNET_PLAY_RUNNING &&
        play->state != CWNET_PLAY_WAITING) {
        char msg[128];
        snprintf(msg, sizeof msg, "key down at T0%+lld in state %d, with the over closed",
                 (long long)(now_ms - T0), (int)play->state);
        TEST_FAIL_MESSAGE(msg);
    }
}

/*
 * The sequences this file pins, plus the reference's own slow end of over
 * (keyer_sim.c case F, 80 31 7F 44), each ticked at every millisecond of
 * its span. The engine is allowed to key down, to hold that state with an
 * empty FIFO for as long as the operator holds the key, and to close; it is
 * never allowed to close or to go idle with the key still down.
 */
void test_play_only_a_held_element_leaves_the_key_down(void) {
    static const uint8_t long_down[]   = { 0x80, 0x7F, 0x6A };              /* 1994 ms down */
    static const uint8_t split_gap[]   = { 0x80, 0x14, 0xFF, 0xEA, 0x21 };  /* 1994 ms up */
    static const uint8_t end_of_over[] = { 0x80, 0x24, 0x24 };
    static const uint8_t starved[]     = { 0x80 };
    static const uint8_t slow_eoo[]    = { 0x80, 0x31, 0x7F, 0x44 };
    /* A key-down whose next byte keeps it down and then stops: the engine
     * waits twice, and both waits are the operator's, not a defect. */
    static const uint8_t held_then_gone[] = { 0x80, 0xFF };

    const struct {
        const uint8_t *bytes;
        size_t len;
    } cases[] = {
        { long_down,   sizeof long_down },
        { split_gap,   sizeof split_gap },
        { end_of_over, sizeof end_of_over },
        { starved,     sizeof starved },
        { slow_eoo,    sizeof slow_eoo },
        { held_then_gone, sizeof held_then_gone },
    };

    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        cwnet_play_t play;
        play_setup(&play, 0u, B_MS);
        push_bytes(&play, cases[c].bytes, cases[c].len, T0);
        for (int64_t t = T0; t <= T0 + 2400; t++) {
            tick_and_check_invariant(&play, t);
        }
        /* And the release the caller always has: from wherever that case
         * ended up, force_release leaves the key up and the over shut. */
        cwnet_play_result_t r;
        cwnet_play_force_release(&play, T0 + 2400, &r);
        cwnet_play_tick(&play, T0 + 2400 + (int64_t)TAIL_MS, &r);
        TEST_ASSERT_FALSE(cwnet_play_key_down(&play));
        TEST_ASSERT_FALSE(cwnet_play_over_open(&play));
    }
}

/*===========================================================================*/
/* The shared ring itself (cwnet_rxfifo.h)                                   */
/*===========================================================================*/

/*
 * Until now this arithmetic was only exercised sideways, through the engine
 * and through the client, and it existed twice. Now there is one copy, so
 * the wrap is worth pinning where it lives: an off-by-one here would move
 * both ends of the wire at once.
 */

/** Wind the ring's tail forward to `slot`, leaving it empty */
static void rxfifo_seek_tail(cwnet_rxfifo_t *f, unsigned slot) {
    cwnet_rxfifo_reset(f);
    for (unsigned i = 0; i < slot; i++) {
        uint8_t b = 0;
        TEST_ASSERT_NOT_EQUAL(CWNET_RXFIFO_NO_SLOT, cwnet_rxfifo_push(f, 0x00));
        TEST_ASSERT_NOT_EQUAL(CWNET_RXFIFO_NO_SLOT, cwnet_rxfifo_pop(f, &b));
    }
    TEST_ASSERT_EQUAL_size_t(0u, cwnet_rxfifo_count(f));
}

void test_rxfifo_fills_wraps_and_keeps_order(void) {
    cwnet_rxfifo_t f;
    cwnet_rxfifo_reset(&f);

    /* Fill it: the reference's 128 bytes, each landing in its own slot */
    for (int i = 0; i < CWNET_RXFIFO_SIZE; i++) {
        TEST_ASSERT_EQUAL_INT(i, cwnet_rxfifo_push(&f, (uint8_t)i));
    }
    TEST_ASSERT_TRUE(cwnet_rxfifo_full(&f));
    TEST_ASSERT_EQUAL_size_t((size_t)CWNET_RXFIFO_SIZE, cwnet_rxfifo_count(&f));

    /* One more finds it full: no slot, nothing overwritten, count unmoved */
    TEST_ASSERT_EQUAL_INT(CWNET_RXFIFO_NO_SLOT, cwnet_rxfifo_push(&f, 0xAA));
    TEST_ASSERT_EQUAL_size_t((size_t)CWNET_RXFIFO_SIZE, cwnet_rxfifo_count(&f));

    /* Take 100 back out, oldest first, from the slots they went into */
    for (int i = 0; i < 100; i++) {
        uint8_t b = 0;
        TEST_ASSERT_EQUAL_INT(i, cwnet_rxfifo_pop(&f, &b));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, b);
    }
    TEST_ASSERT_EQUAL_size_t(28u, cwnet_rxfifo_count(&f));

    /* 100 more: the head wraps past 127 and reuses slots 0..99 */
    for (int i = 0; i < 100; i++) {
        TEST_ASSERT_EQUAL_INT(i, cwnet_rxfifo_push(&f, (uint8_t)(200 + i)));
    }
    TEST_ASSERT_TRUE(cwnet_rxfifo_full(&f));

    /* Out in order: the 28 that were left, then the 100 that came after */
    for (int i = 100; i < CWNET_RXFIFO_SIZE; i++) {
        uint8_t b = 0;
        TEST_ASSERT_EQUAL_INT(i, cwnet_rxfifo_pop(&f, &b));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)i, b);
    }
    for (int i = 0; i < 100; i++) {
        uint8_t b = 0;
        TEST_ASSERT_EQUAL_INT(i, cwnet_rxfifo_pop(&f, &b));
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(200 + i), b);
    }

    /* Empty: no slot, and nothing written through the out parameter */
    uint8_t untouched = 0x5A;
    TEST_ASSERT_EQUAL_INT(CWNET_RXFIFO_NO_SLOT, cwnet_rxfifo_pop(&f, &untouched));
    TEST_ASSERT_EQUAL_UINT8(0x5A, untouched);
    TEST_ASSERT_EQUAL_INT(CWNET_RXFIFO_NO_SLOT, cwnet_rxfifo_peek_slot(&f));
    TEST_ASSERT_EQUAL_size_t(0u, cwnet_rxfifo_count(&f));
}

void test_rxfifo_peek_does_not_consume(void) {
    cwnet_rxfifo_t f;
    rxfifo_seek_tail(&f, 127u);

    TEST_ASSERT_EQUAL_INT(127, cwnet_rxfifo_push(&f, 0x81));
    TEST_ASSERT_EQUAL_INT(0, cwnet_rxfifo_push(&f, 0x02));

    /* Twice, because a peek that consumed would answer differently */
    TEST_ASSERT_EQUAL_INT(127, cwnet_rxfifo_peek_slot(&f));
    TEST_ASSERT_EQUAL_INT(127, cwnet_rxfifo_peek_slot(&f));
    TEST_ASSERT_EQUAL_size_t(2u, cwnet_rxfifo_count(&f));

    uint8_t b = 0;
    TEST_ASSERT_EQUAL_INT(127, cwnet_rxfifo_pop(&f, &b));
    TEST_ASSERT_EQUAL_UINT8(0x81, b);
    TEST_ASSERT_EQUAL_INT(0, cwnet_rxfifo_peek_slot(&f));
}

void test_rxfifo_buffered_ms_sums_across_the_wrap(void) {
    cwnet_rxfifo_t f;
    rxfifo_seek_tail(&f, 126u);

    /* Waits 1, 2 and 3 ms in the linear range, straddling slot 127 -> 0 */
    TEST_ASSERT_EQUAL_INT(126, cwnet_rxfifo_push(&f, 0x81));
    TEST_ASSERT_EQUAL_INT(127, cwnet_rxfifo_push(&f, 0x02));
    TEST_ASSERT_EQUAL_INT(0, cwnet_rxfifo_push(&f, 0x83));
    TEST_ASSERT_EQUAL_INT32(6, cwnet_rxfifo_buffered_ms(&f));

    /* The key bit is no part of the sum, and what is popped stops counting */
    uint8_t b = 0;
    (void)cwnet_rxfifo_pop(&f, &b);
    TEST_ASSERT_EQUAL_INT32(5, cwnet_rxfifo_buffered_ms(&f));

    cwnet_rxfifo_reset(&f);
    TEST_ASSERT_EQUAL_INT32(0, cwnet_rxfifo_buffered_ms(&f));
    TEST_ASSERT_EQUAL_size_t(0u, cwnet_rxfifo_count(&f));
}

void test_rxfifo_end_of_over_seen_across_the_wrap(void) {
    cwnet_rxfifo_t f;

    /* Two key-ups in a row, the pair straddling slot 127 -> 0 */
    rxfifo_seek_tail(&f, 126u);
    (void)cwnet_rxfifo_push(&f, 0x81);   /* down */
    (void)cwnet_rxfifo_push(&f, 0x02);   /* up   */
    TEST_ASSERT_FALSE(cwnet_rxfifo_has_end_of_over(&f));
    (void)cwnet_rxfifo_push(&f, 0x03);   /* up: the mark */
    TEST_ASSERT_TRUE(cwnet_rxfifo_has_end_of_over(&f));

    /* A key-down between them is not the mark, wrap or no wrap */
    rxfifo_seek_tail(&f, 126u);
    (void)cwnet_rxfifo_push(&f, 0x02);   /* up   */
    (void)cwnet_rxfifo_push(&f, 0x83);   /* down */
    (void)cwnet_rxfifo_push(&f, 0x04);   /* up   */
    TEST_ASSERT_FALSE(cwnet_rxfifo_has_end_of_over(&f));

    /* A single byte cannot be a pair, whatever it carries */
    rxfifo_seek_tail(&f, 0u);
    (void)cwnet_rxfifo_push(&f, 0x00);
    TEST_ASSERT_FALSE(cwnet_rxfifo_has_end_of_over(&f));
}
