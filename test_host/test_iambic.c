/**
 * @file test_iambic.c
 * @brief Unit tests for iambic keyer FSM
 */

#include "unity.h"
#include "iambic.h"
#include "sample.h"
#include "stubs/esp_stubs.h"

static iambic_processor_t s_iambic;
static const int64_t DIT_DURATION_20WPM = 60000;  /* 60ms at 20 WPM */

void test_iambic_init(void) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;

    iambic_init(&s_iambic, &config);

    TEST_ASSERT_EQUAL(20, s_iambic.config.wpm);
    TEST_ASSERT_EQUAL(IAMBIC_STATE_IDLE, s_iambic.state);
    TEST_ASSERT_FALSE(s_iambic.key_down);
}

void test_iambic_dit(void) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;
    iambic_init(&s_iambic, &config);

    /* Press DIT paddle */
    gpio_state_t gpio = gpio_from_paddles(true, false);
    esp_timer_set_time(0);
    stream_sample_t sample = iambic_tick(&s_iambic, 0, gpio);

    /* Should be in DIT state with key down */
    TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DIT, s_iambic.state);
    TEST_ASSERT_TRUE(s_iambic.key_down);
    TEST_ASSERT_EQUAL(1, sample.local_key);

    /* After DIT duration, key should go up */
    esp_timer_set_time(DIT_DURATION_20WPM + 1000);
    sample = iambic_tick(&s_iambic, DIT_DURATION_20WPM + 1000, gpio);

    /* Should be in inter-element space */
    TEST_ASSERT_EQUAL(IAMBIC_STATE_GAP, s_iambic.state);
    TEST_ASSERT_FALSE(s_iambic.key_down);
    TEST_ASSERT_EQUAL(0, sample.local_key);
}

void test_iambic_dah(void) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;
    iambic_init(&s_iambic, &config);

    /* Press DAH paddle */
    gpio_state_t gpio = gpio_from_paddles(false, true);
    esp_timer_set_time(0);
    stream_sample_t sample = iambic_tick(&s_iambic, 0, gpio);

    /* Should be in DAH state with key down */
    TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DAH, s_iambic.state);
    TEST_ASSERT_TRUE(s_iambic.key_down);
    TEST_ASSERT_EQUAL(1, sample.local_key);

    /* DAH is 3x DIT duration */
    int64_t dah_duration = DIT_DURATION_20WPM * 3;
    esp_timer_set_time(dah_duration + 1000);
    sample = iambic_tick(&s_iambic, dah_duration + 1000, gpio);

    /* Should be in inter-element space */
    TEST_ASSERT_EQUAL(IAMBIC_STATE_GAP, s_iambic.state);
    TEST_ASSERT_FALSE(s_iambic.key_down);
}

void test_iambic_mode_a_squeeze(void) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;
    config.mode = IAMBIC_MODE_A;
    iambic_init(&s_iambic, &config);

    /* Squeeze both paddles */
    gpio_state_t gpio = gpio_from_paddles(true, true);
    esp_timer_set_time(0);
    stream_sample_t sample = iambic_tick(&s_iambic, 0, gpio);

    /* Should start with DIT (or DAH depending on implementation) */
    TEST_ASSERT_TRUE(s_iambic.key_down);
    TEST_ASSERT_TRUE(s_iambic.state == IAMBIC_STATE_SEND_DIT || s_iambic.state == IAMBIC_STATE_SEND_DAH);

    /* Release during element - Mode A should stop */
    gpio = gpio_from_paddles(false, false);

    /* Complete current element */
    int64_t time = DIT_DURATION_20WPM + 1000;
    esp_timer_set_time(time);
    sample = iambic_tick(&s_iambic, time, gpio);

    /* Mode A: should go to GAP then IDLE since paddles released */
    TEST_ASSERT_EQUAL(IAMBIC_STATE_GAP, s_iambic.state);

    /* After GAP, should go idle */
    time += DIT_DURATION_20WPM + 1000;
    esp_timer_set_time(time);
    sample = iambic_tick(&s_iambic, time, gpio);

    TEST_ASSERT_EQUAL(IAMBIC_STATE_IDLE, s_iambic.state);
    (void)sample;
}

void test_iambic_mode_b_squeeze(void) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;
    config.mode = IAMBIC_MODE_B;
    iambic_init(&s_iambic, &config);

    /* Squeeze both paddles */
    gpio_state_t gpio = gpio_from_paddles(true, true);
    esp_timer_set_time(0);
    stream_sample_t sample = iambic_tick(&s_iambic, 0, gpio);

    TEST_ASSERT_TRUE(s_iambic.key_down);
    iambic_state_t first_state = s_iambic.state;

    /* Release during element */
    gpio = gpio_from_paddles(false, false);

    /* Complete current element */
    int64_t time = DIT_DURATION_20WPM + 1000;
    esp_timer_set_time(time);
    sample = iambic_tick(&s_iambic, time, gpio);

    /* Mode B: should complete current then do opposite element */
    TEST_ASSERT_EQUAL(IAMBIC_STATE_GAP, s_iambic.state);

    /* After GAP, Mode B should queue opposite element */
    time += DIT_DURATION_20WPM + 1000;
    esp_timer_set_time(time);
    sample = iambic_tick(&s_iambic, time, gpio);

    /* Should be in opposite state */
    if (first_state == IAMBIC_STATE_SEND_DIT) {
        TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DAH, s_iambic.state);
    } else {
        TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DIT, s_iambic.state);
    }
    (void)sample;
}

void test_iambic_memory(void) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;
    config.mode = IAMBIC_MODE_B;
    /* This test asserts on dah_memory halfway through the dit mark, which is a
     * property of the windowed edge model, not of iambic keying as such: under
     * SQUEEZE_MODE_SAMPLED no grid instant has been crossed at 0.5u, so the
     * memory is legitimately still clear there. Name the mode explicitly rather
     * than letting the test track whatever the default happens to be. */
    config.squeeze_mode = SQUEEZE_MODE_LATCH_OFF;
    iambic_init(&s_iambic, &config);

    /* Start DIT */
    gpio_state_t gpio = gpio_from_paddles(true, false);
    esp_timer_set_time(0);
    iambic_tick(&s_iambic, 0, gpio);

    TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DIT, s_iambic.state);

    /* Press DAH while DIT is sounding (memory) */
    gpio = gpio_from_paddles(true, true);
    int64_t time = DIT_DURATION_20WPM / 2;  /* Halfway through DIT */
    esp_timer_set_time(time);
    iambic_tick(&s_iambic, time, gpio);

    /* DAH memory should be set */
    TEST_ASSERT_TRUE(s_iambic.dah_memory);

    /* Release all */
    gpio = gpio_from_paddles(false, false);

    /* Complete DIT */
    time = DIT_DURATION_20WPM + 1000;
    esp_timer_set_time(time);
    iambic_tick(&s_iambic, time, gpio);

    /* Complete GAP */
    time += DIT_DURATION_20WPM + 1000;
    esp_timer_set_time(time);
    iambic_tick(&s_iambic, time, gpio);

    /* Should play DAH from memory */
    TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DAH, s_iambic.state);
}

void test_iambic_squeeze_prolonged(void) {
    /* Test prolonged squeeze produces DIT-DAH-DIT-DAH alternation
     * This test captures the bug where only DITs are sent followed by one DAH */
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;
    config.mode = IAMBIC_MODE_B;
    config.memory_mode = MEMORY_MODE_DOT_AND_DAH;
    config.mem_window_start_pct = 0;
    config.mem_window_end_pct = 100;
    iambic_init(&s_iambic, &config);

    int64_t time = 0;
    gpio_state_t squeeze = gpio_from_paddles(true, true);

    /* First element: should start with DIT */
    esp_timer_set_time(time);
    stream_sample_t sample = iambic_tick(&s_iambic, time, squeeze);
    TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DIT, s_iambic.state);
    TEST_ASSERT_TRUE(s_iambic.key_down);

    /* Complete DIT (60ms) */
    time += DIT_DURATION_20WPM + 1000;
    esp_timer_set_time(time);
    sample = iambic_tick(&s_iambic, time, squeeze);
    TEST_ASSERT_EQUAL(IAMBIC_STATE_GAP, s_iambic.state);

    /* Complete GAP (60ms) */
    time += DIT_DURATION_20WPM + 1000;
    esp_timer_set_time(time);
    sample = iambic_tick(&s_iambic, time, squeeze);

    /* Second element: should be DAH (opposite of first) */
    TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DAH, s_iambic.state);
    TEST_ASSERT_TRUE(s_iambic.key_down);

    /* Complete DAH (180ms) */
    time += (DIT_DURATION_20WPM * 3) + 1000;
    esp_timer_set_time(time);
    sample = iambic_tick(&s_iambic, time, squeeze);
    TEST_ASSERT_EQUAL(IAMBIC_STATE_GAP, s_iambic.state);

    /* Complete GAP */
    time += DIT_DURATION_20WPM + 1000;
    esp_timer_set_time(time);
    sample = iambic_tick(&s_iambic, time, squeeze);

    /* Third element: should be DIT again (alternation) */
    TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DIT, s_iambic.state);
    TEST_ASSERT_TRUE(s_iambic.key_down);

    /* Complete DIT */
    time += DIT_DURATION_20WPM + 1000;
    esp_timer_set_time(time);
    sample = iambic_tick(&s_iambic, time, squeeze);
    TEST_ASSERT_EQUAL(IAMBIC_STATE_GAP, s_iambic.state);

    /* Complete GAP */
    time += DIT_DURATION_20WPM + 1000;
    esp_timer_set_time(time);
    sample = iambic_tick(&s_iambic, time, squeeze);

    /* Fourth element: should be DAH again (continued alternation) */
    TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DAH, s_iambic.state);
    TEST_ASSERT_TRUE(s_iambic.key_down);

    (void)sample;
}

/* ============================================================================
 * SQUEEZE_MODE_SAMPLED — the K1EL K8 sampling rule (issue #32)
 *
 * The K8 reads the paddle *level* on a one-unit grid phase-locked to the
 * element boundary. A dah element spans 4 units (3u mark + 1u space) and is
 * sampled at 1u, 2u, 3u, 4u. A press that opens and closes strictly between
 * two instants is never observed.
 *
 * These tests drive the FSM at 1 ms, the same cadence as rt_task.c:176, so a
 * sample instant is resolved at the first tick at or after k*u.
 * ============================================================================
 */

#define K8_U_US  DIT_DURATION_20WPM      /* one dit-unit at 20 WPM = 60 ms */

/* Elements started so far, as Morse: '.' = dit, '-' = dah. */
#define K8_MAX_ELEMS 16
static char k8_seq[K8_MAX_ELEMS + 1];
static int  k8_seq_len;
static iambic_state_t k8_prev_state;

static void k8_seq_reset(void) {
    k8_seq_len = 0;
    k8_seq[0] = '\0';
    k8_prev_state = IAMBIC_STATE_IDLE;
}

/** Tick the FSM at 1 ms from its current time up to (and including) t_end,
 *  recording each element the FSM starts. 1 ms is rt_task.c's own cadence. */
static void k8_run_until(int64_t *now_us, int64_t t_end, bool dit, bool dah) {
    gpio_state_t gpio = gpio_from_paddles(dit, dah);
    for (int64_t t = *now_us; t <= t_end; t += 1000) {
        esp_timer_set_time(t);
        (void)iambic_tick(&s_iambic, t, gpio);
        if (s_iambic.state != k8_prev_state && k8_seq_len < K8_MAX_ELEMS) {
            if (s_iambic.state == IAMBIC_STATE_SEND_DIT) {
                k8_seq[k8_seq_len++] = '.';
            } else if (s_iambic.state == IAMBIC_STATE_SEND_DAH) {
                k8_seq[k8_seq_len++] = '-';
            }
            k8_seq[k8_seq_len] = '\0';
        }
        k8_prev_state = s_iambic.state;
        *now_us = t;
    }
    *now_us = t_end + 1000;
}

static void k8_setup_sampled(void) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;
    config.mode = IAMBIC_MODE_A;          /* isolate sampling from the Mode B bonus */
    config.squeeze_mode = SQUEEZE_MODE_SAMPLED;
    config.memory_mode = MEMORY_MODE_DOT_AND_DAH;
    iambic_init(&s_iambic, &config);
    k8_seq_reset();
}

/** Same, with the memory disabled: DJ5IL's plain-iambic control case. */
static void k8_setup_sampled_no_memory(void) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;
    config.mode = IAMBIC_MODE_A;
    config.squeeze_mode = SQUEEZE_MODE_SAMPLED;
    config.memory_mode = MEMORY_MODE_NONE;
    iambic_init(&s_iambic, &config);
    k8_seq_reset();
}

/**
 * A dit tap that opens and closes strictly BETWEEN two sample instants is lost.
 * Press at 1.2u, release at 1.6u: the grid ticks at 1u and 2u, so nothing sees
 * it. This is the assertion a percentage-window model cannot satisfy, because
 * 1.2u..1.6u lies inside [0%, 100%] of the dah mark.
 */
void test_iambic_k8_tap_between_instants_is_lost(void) {
    k8_setup_sampled();
    int64_t now = 0;

    /* Start a dah: 3u mark + 1u space. */
    k8_run_until(&now, (K8_U_US * 12) / 10 - 1000, false, true);
    TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DAH, s_iambic.state);

    /* Tap dit from 1.2u to 1.6u, entirely between the 1u and 2u instants. */
    k8_run_until(&now, (K8_U_US * 16) / 10 - 1000, true, true);
    k8_run_until(&now, K8_U_US * 4 + 2000, false, true);

    /* The tap was never sampled, so no dit may have been queued. */
    TEST_ASSERT_FALSE(s_iambic.dit_memory);
    TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DAH, s_iambic.state);
}

/**
 * The same tap, moved so that it spans the 2u instant, IS observed.
 * Press at 1.8u, release at 2.2u. Same duration as above, different phase:
 * the difference is the grid, not the length of the press.
 */
void test_iambic_k8_tap_spanning_instant_is_seen(void) {
    k8_setup_sampled();
    int64_t now = 0;

    k8_run_until(&now, (K8_U_US * 18) / 10 - 1000, false, true);
    TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DAH, s_iambic.state);

    /* Tap dit from 1.8u to 2.2u, crossing the 2u instant. */
    k8_run_until(&now, (K8_U_US * 22) / 10 - 1000, true, true);
    k8_run_until(&now, K8_U_US * 4 - 1000, false, true);

    /* The 2u sample found the contact closed, so a dit is queued and sent. */
    k8_run_until(&now, K8_U_US * 4 + 2000, false, true);
    TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DIT, s_iambic.state);
}

/**
 * DJ5IL's memory test, [Lit4]: key an "N" with both levers released before the
 * dash-element completes. With dot memory the dash is followed by a dot and you
 * get N; without it the dot is lost and you get T.
 *
 * On the K8 the dah element spans 4u and is sampled at 1u, 2u, 3u and 4u, so a
 * dit held across any of those instants survives.
 */
void test_iambic_k8_lit4_memory_gives_N(void) {
    k8_setup_sampled();
    int64_t now = 0;

    /* Dah first, dit joins at 0.5u, both released at 2.5u — well before the
     * element boundary at 4u, but across the 1u and 2u instants. */
    k8_run_until(&now, K8_U_US / 2 - 1000, false, true);
    k8_run_until(&now, (K8_U_US * 25) / 10 - 1000, true, true);
    k8_run_until(&now, K8_U_US * 8, false, false);

    TEST_ASSERT_EQUAL_STRING("-.", k8_seq);
}

void test_iambic_k8_lit4_no_memory_gives_T(void) {
    k8_setup_sampled_no_memory();
    int64_t now = 0;

    k8_run_until(&now, K8_U_US / 2 - 1000, false, true);
    k8_run_until(&now, (K8_U_US * 25) / 10 - 1000, true, true);
    k8_run_until(&now, K8_U_US * 8, false, false);

    TEST_ASSERT_EQUAL_STRING("-", k8_seq);
}

/**
 * DJ5IL's [Lit5], settled against the emulator rather than against his wording:
 * a dash pressed during a dot and released BEFORE the 1u sample is lost, giving
 * E; released after it, the 1u sample latches it, giving A.
 *
 * Both cases pin real K8 behaviour. The reference is the K8, not the article,
 * so the release instants are concrete and the label is what may be debated.
 */
void test_iambic_k8_lit5_release_before_first_sample_gives_E(void) {
    k8_setup_sampled();
    int64_t now = 0;

    /* Dit starts at 0. Dah pressed 0.3u, both released 0.8u: nothing is sampled
     * at 0.8u, and the first instant is 1u. */
    k8_run_until(&now, (K8_U_US * 3) / 10 - 1000, true, false);
    k8_run_until(&now, (K8_U_US * 8) / 10 - 1000, true, true);
    k8_run_until(&now, K8_U_US * 6, false, false);

    TEST_ASSERT_EQUAL_STRING(".", k8_seq);
}

void test_iambic_k8_lit5_release_after_first_sample_gives_A(void) {
    k8_setup_sampled();
    int64_t now = 0;

    /* Same press, released at 1.5u instead: the 1u sample found it closed. */
    k8_run_until(&now, (K8_U_US * 3) / 10 - 1000, true, false);
    k8_run_until(&now, (K8_U_US * 15) / 10 - 1000, false, true);
    k8_run_until(&now, K8_U_US * 8, false, false);

    TEST_ASSERT_EQUAL_STRING(".-", k8_seq);
}

/**
 * DJ5IL's A-versus-B test, [Lit6]/[Lit7]: squeeze a "K" and release both levers
 * during the second dash. Mode A completes it and gives K; Mode B adds one
 * alternate element and gives C.
 *
 * The maintainer's note applies: from idle the K8 sends DIT on a simultaneous
 * squeeze, so the operator must press dah first and close dit after.
 */
static void k8_squeeze_K(int64_t *now, bool mode_b) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;
    config.mode = mode_b ? IAMBIC_MODE_B : IAMBIC_MODE_A;
    config.squeeze_mode = SQUEEZE_MODE_SAMPLED;
    config.memory_mode = MEMORY_MODE_DOT_AND_DAH;
    iambic_init(&s_iambic, &config);
    k8_seq_reset();
    *now = 0;

    /* dah, then close dit: elements alternate dah, dit, dah (4u + 2u + 4u).
     * Release both inside the second dah, which runs from 6u to 10u. */
    k8_run_until(now, K8_U_US / 4 - 1000, false, true);
    k8_run_until(now, (K8_U_US * 7) - 1000, true, true);
    k8_run_until(now, K8_U_US * 16, false, false);
}

void test_iambic_k8_lit6_mode_a_squeeze_gives_K(void) {
    int64_t now;
    k8_squeeze_K(&now, false);
    TEST_ASSERT_EQUAL_STRING("-.-", k8_seq);
}

void test_iambic_k8_lit7_mode_b_squeeze_gives_C(void) {
    int64_t now;
    k8_squeeze_K(&now, true);
    TEST_ASSERT_EQUAL_STRING("-.-.", k8_seq);
}

/**
 * A simultaneous squeeze from idle sends DIT first. In the K8 this falls out of
 * NSAMPLE evaluating GP0 then GP1, so INLAST ends up reflecting the right
 * contact and CHK_SINGLE falls through to CHK_S1 (issue #32).
 */
void test_iambic_k8_first_element_of_squeeze_is_dit(void) {
    k8_setup_sampled();
    int64_t now = 0;

    k8_run_until(&now, K8_U_US / 2, true, true);

    TEST_ASSERT_EQUAL(IAMBIC_STATE_SEND_DIT, s_iambic.state);
    TEST_ASSERT_EQUAL_STRING(".", k8_seq);
}

/**
 * The grid is not anchored to the first unit only: a tap between the 2u and 3u
 * instants of a dah is lost just as one between 1u and 2u is. Guards the grid
 * arithmetic against an off-by-one that would only get the first instant right.
 */
void test_iambic_k8_tap_between_later_instants_is_lost(void) {
    k8_setup_sampled();
    int64_t now = 0;

    k8_run_until(&now, (K8_U_US * 22) / 10 - 1000, false, true);
    k8_run_until(&now, (K8_U_US * 27) / 10 - 1000, true, true);  /* 2.2u .. 2.7u */
    k8_run_until(&now, K8_U_US * 8, false, false);

    TEST_ASSERT_EQUAL_STRING("-", k8_seq);
}

/**
 * The grid follows the dit-unit, not a hard-coded millisecond value. Same
 * scenario at 12 WPM, where a unit is 100 ms instead of 60 ms.
 */
void test_iambic_k8_grid_scales_with_wpm(void) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 12;
    config.mode = IAMBIC_MODE_A;
    config.squeeze_mode = SQUEEZE_MODE_SAMPLED;
    config.memory_mode = MEMORY_MODE_DOT_AND_DAH;
    iambic_init(&s_iambic, &config);
    k8_seq_reset();

    const int64_t u = 100000;  /* 1200000 / 12 */
    int64_t now = 0;

    k8_run_until(&now, (u * 12) / 10 - 1000, false, true);
    k8_run_until(&now, (u * 16) / 10 - 1000, true, true);  /* 1.2u .. 1.6u */
    k8_run_until(&now, u * 8, false, false);

    TEST_ASSERT_EQUAL_STRING("-", k8_seq);
}
