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
        case CWNET_PLAY_EV_UNDERRUN:      return "UNDERRUN";
        case CWNET_PLAY_EV_OVER_FINISHED: return "OVER_FINISHED";
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

/*===========================================================================*/
/* AE6: the FIFO runs dry under a key-down                                   */
/*===========================================================================*/

/*
 * One key-down byte and nothing else. At T0 + B the key goes down, the
 * FIFO is empty, and the length of that element is unknown: the key goes
 * up at once and the underrun is reported. The PTT drops a tail later,
 * T0 + 50 + 100 = T0 + 150. The over is not finished: no marker was
 * played, and the bytes that follow restart it.
 */
void test_play_underrun_lifts_the_key_at_once_and_reports_it(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t only_down[] = { 0x80 };
    push_bytes(&play, only_down, sizeof only_down, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 500);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,   T0 + 50 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 50 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 50 },
        { CWNET_PLAY_EV_UNDERRUN, T0 + 50 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 150 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);

    TEST_ASSERT_FALSE(cwnet_play_key_down(&play));
    TEST_ASSERT_EQUAL_size_t(0u, count_of(&log, CWNET_PLAY_EV_OVER_FINISHED));
}

/*
 * After the underrun the bytes that follow restart as a new over with the
 * SAME B, without the caller arming anything: a byte received at T0+500
 * keys down at T0+550, and its dot still lasts its encoded 48 ms.
 */
void test_play_after_an_underrun_the_next_byte_restarts_with_the_same_buffer(void) {
    cwnet_play_t play;
    play_setup(&play, 0u, B_MS);

    const uint8_t only_down[] = { 0x80 };
    push_bytes(&play, only_down, sizeof only_down, T0);

    evlog_t log = { .n = 0 };
    run_until(&play, &log, T0 + 400);
    log.n = 0;   /* the underrun itself is pinned by the test above */

    const uint8_t again[] = { 0x80, 0x24 };
    push_bytes(&play, again, sizeof again, T0 + 500);
    run_until(&play, &log, T0 + 1000);

    const exp_ev_t want[] = {
        { CWNET_PLAY_EV_PTT_ON,   T0 + 550 },
        { CWNET_PLAY_EV_KEY_DOWN, T0 + 550 },
        { CWNET_PLAY_EV_KEY_UP,   T0 + 598 },
        { CWNET_PLAY_EV_PTT_OFF,  T0 + 698 },
    };
    assert_log(&log, want, sizeof want / sizeof want[0]);
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
