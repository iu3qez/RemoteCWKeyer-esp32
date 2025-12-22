/**
 * @file test_iambic_preset.c
 * @brief Unit tests for iambic preset system
 */

#include "unity.h"
#include "iambic_preset.h"
#include <string.h>

void test_preset_init(void) {
    iambic_preset_init();

    /* Check that preset 0 is active by default */
    TEST_ASSERT_EQUAL(0, iambic_preset_active_index());

    /* Check that preset 0 has default name */
    const iambic_preset_t *p0 = iambic_preset_get(0);
    TEST_ASSERT_NOT_NULL(p0);
    TEST_ASSERT_EQUAL_STRING("Default", p0->name);

    /* Check preset 1 "Contest" has higher WPM */
    const iambic_preset_t *p1 = iambic_preset_get(1);
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_EQUAL_STRING("Contest", p1->name);
    TEST_ASSERT_EQUAL(35, iambic_preset_get_wpm(p1));
}

void test_preset_activate(void) {
    iambic_preset_init();

    /* Activate preset 2 */
    TEST_ASSERT_TRUE(iambic_preset_activate(2));
    TEST_ASSERT_EQUAL(2, iambic_preset_active_index());

    /* Active preset should be "Slow" */
    const iambic_preset_t *active = iambic_preset_active();
    TEST_ASSERT_NOT_NULL(active);
    TEST_ASSERT_EQUAL_STRING("Slow", active->name);
    TEST_ASSERT_EQUAL(15, iambic_preset_get_wpm(active));

    /* Invalid index should fail */
    TEST_ASSERT_FALSE(iambic_preset_activate(10));
    TEST_ASSERT_EQUAL(2, iambic_preset_active_index());  /* Unchanged */
}

void test_preset_get_set_values(void) {
    iambic_preset_init();

    iambic_preset_t *preset = iambic_preset_get_mut(0);
    TEST_ASSERT_NOT_NULL(preset);

    /* Test WPM get/set */
    iambic_preset_set_wpm(preset, 30);
    TEST_ASSERT_EQUAL(30, iambic_preset_get_wpm(preset));

    /* WPM bounds checking */
    iambic_preset_set_wpm(preset, 4);  /* Too low, should be ignored */
    TEST_ASSERT_EQUAL(30, iambic_preset_get_wpm(preset));

    iambic_preset_set_wpm(preset, 101);  /* Too high, should be ignored */
    TEST_ASSERT_EQUAL(30, iambic_preset_get_wpm(preset));

    /* Test mode get/set */
    iambic_preset_set_mode(preset, IAMBIC_MODE_A);
    TEST_ASSERT_EQUAL(IAMBIC_MODE_A, iambic_preset_get_mode(preset));

    iambic_preset_set_mode(preset, IAMBIC_MODE_B);
    TEST_ASSERT_EQUAL(IAMBIC_MODE_B, iambic_preset_get_mode(preset));

    /* Test memory mode get/set */
    iambic_preset_set_memory_mode(preset, MEMORY_MODE_NONE);
    TEST_ASSERT_EQUAL(MEMORY_MODE_NONE, iambic_preset_get_memory_mode(preset));

    iambic_preset_set_memory_mode(preset, MEMORY_MODE_DOT_ONLY);
    TEST_ASSERT_EQUAL(MEMORY_MODE_DOT_ONLY, iambic_preset_get_memory_mode(preset));

    /* Test squeeze mode get/set */
    iambic_preset_set_squeeze_mode(preset, SQUEEZE_MODE_LATCH_ON);
    TEST_ASSERT_EQUAL(SQUEEZE_MODE_LATCH_ON, iambic_preset_get_squeeze_mode(preset));

    /* Test memory window get/set */
    iambic_preset_set_mem_start(preset, 50);
    iambic_preset_set_mem_end(preset, 90);
    TEST_ASSERT_EQUAL(50, iambic_preset_get_mem_start(preset));
    TEST_ASSERT_EQUAL(90, iambic_preset_get_mem_end(preset));

    /* Memory window bounds checking */
    iambic_preset_set_mem_start(preset, 150);  /* Too high, should be ignored */
    TEST_ASSERT_EQUAL(50, iambic_preset_get_mem_start(preset));
}

void test_preset_copy(void) {
    iambic_preset_init();

    /* Modify preset 0 */
    iambic_preset_t *p0 = iambic_preset_get_mut(0);
    iambic_preset_set_wpm(p0, 42);
    iambic_preset_set_mode(p0, IAMBIC_MODE_A);
    iambic_preset_set_name(0, "Custom");

    /* Copy preset 0 to preset 5 */
    TEST_ASSERT_TRUE(iambic_preset_copy(0, 5));

    /* Verify preset 5 has same values */
    const iambic_preset_t *p5 = iambic_preset_get(5);
    TEST_ASSERT_EQUAL_STRING("Custom", p5->name);
    TEST_ASSERT_EQUAL(42, iambic_preset_get_wpm(p5));
    TEST_ASSERT_EQUAL(IAMBIC_MODE_A, iambic_preset_get_mode(p5));

    /* Copy to same index should be no-op */
    TEST_ASSERT_TRUE(iambic_preset_copy(0, 0));

    /* Invalid indices should fail */
    TEST_ASSERT_FALSE(iambic_preset_copy(10, 0));
    TEST_ASSERT_FALSE(iambic_preset_copy(0, 10));
}

void test_preset_reset(void) {
    iambic_preset_init();

    /* Modify preset 0 */
    iambic_preset_t *p0 = iambic_preset_get_mut(0);
    iambic_preset_set_wpm(p0, 99);
    iambic_preset_set_name(0, "Modified");

    /* Reset preset 0 */
    TEST_ASSERT_TRUE(iambic_preset_reset(0));

    /* Verify it's back to defaults */
    TEST_ASSERT_EQUAL_STRING("Default", p0->name);
    TEST_ASSERT_EQUAL(25, iambic_preset_get_wpm(p0));

    /* Invalid index should fail */
    TEST_ASSERT_FALSE(iambic_preset_reset(10));
}

void test_preset_set_name(void) {
    iambic_preset_init();

    /* Set a short name */
    TEST_ASSERT_TRUE(iambic_preset_set_name(4, "Test"));
    const iambic_preset_t *p4 = iambic_preset_get(4);
    TEST_ASSERT_EQUAL_STRING("Test", p4->name);

    /* Set a long name (should be truncated) */
    char long_name[64];
    memset(long_name, 'X', 63);
    long_name[63] = '\0';
    TEST_ASSERT_TRUE(iambic_preset_set_name(4, long_name));
    TEST_ASSERT_EQUAL(IAMBIC_PRESET_NAME_MAX - 1, strlen(p4->name));

    /* NULL name should fail */
    TEST_ASSERT_FALSE(iambic_preset_set_name(4, NULL));

    /* Invalid index should fail */
    TEST_ASSERT_FALSE(iambic_preset_set_name(10, "Test"));
}

void test_preset_timing_helpers(void) {
    /* Test WPM to dit duration conversion */
    /* 20 WPM: dit = 1,200,000 / 20 = 60,000 us = 60 ms */
    TEST_ASSERT_EQUAL(60000, iambic_wpm_to_dit_us(20));

    /* 25 WPM: dit = 1,200,000 / 25 = 48,000 us = 48 ms */
    TEST_ASSERT_EQUAL(48000, iambic_wpm_to_dit_us(25));

    /* Edge case: 0 WPM */
    TEST_ASSERT_EQUAL(0, iambic_wpm_to_dit_us(0));

    /* Test memory window helper */
    TEST_ASSERT_TRUE(iambic_in_memory_window(50, 0, 100));   /* In window */
    TEST_ASSERT_TRUE(iambic_in_memory_window(60, 60, 99));   /* At start */
    TEST_ASSERT_TRUE(iambic_in_memory_window(99, 60, 99));   /* At end */
    TEST_ASSERT_FALSE(iambic_in_memory_window(59, 60, 99));  /* Before window */
    TEST_ASSERT_FALSE(iambic_in_memory_window(100, 60, 99)); /* After window */
}

void test_preset_null_safety(void) {
    iambic_preset_init();

    /* NULL index access should return NULL */
    TEST_ASSERT_NULL(iambic_preset_get(10));
    TEST_ASSERT_NULL(iambic_preset_get_mut(10));

    /* Active preset should never be NULL */
    TEST_ASSERT_NOT_NULL(iambic_preset_active());
}
