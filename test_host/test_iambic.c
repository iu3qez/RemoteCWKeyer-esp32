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
