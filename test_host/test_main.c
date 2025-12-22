/**
 * @file test_main.c
 * @brief Unity test runner main entry point
 */

#include "unity.h"
#include <stdio.h>

/* Test declarations from test files */
void test_stream_init(void);
void test_stream_push_pop(void);
void test_stream_wrap_around(void);
void test_stream_overrun_detection(void);
void test_stream_multiple_consumers(void);

void test_iambic_init(void);
void test_iambic_dit(void);
void test_iambic_dah(void);
void test_iambic_mode_a_squeeze(void);
void test_iambic_mode_b_squeeze(void);
void test_iambic_memory(void);

void test_preset_init(void);
void test_preset_activate(void);
void test_preset_get_set_values(void);
void test_preset_copy(void);
void test_preset_reset(void);
void test_preset_set_name(void);
void test_preset_timing_helpers(void);
void test_preset_null_safety(void);

void test_sidetone_init(void);
void test_sidetone_keying(void);
void test_sidetone_fade(void);

void test_fault_init(void);
void test_fault_set_clear(void);
void test_fault_count(void);

void test_parse_empty_line(void);
void test_parse_simple_command(void);
void test_parse_command_with_one_arg(void);
void test_parse_command_with_two_args(void);
void test_parse_command_with_three_args(void);
void test_parse_extra_args_ignored(void);
void test_parse_leading_whitespace(void);
void test_parse_trailing_whitespace(void);
void test_parse_multiple_spaces(void);

void setUp(void) {
    /* Called before each test */
}

void tearDown(void) {
    /* Called after each test */
}

int main(void) {
    UNITY_BEGIN();

    /* Stream tests */
    printf("\n=== Stream Tests ===\n");
    RUN_TEST(test_stream_init);
    RUN_TEST(test_stream_push_pop);
    RUN_TEST(test_stream_wrap_around);
    RUN_TEST(test_stream_overrun_detection);
    RUN_TEST(test_stream_multiple_consumers);

    /* Iambic tests */
    printf("\n=== Iambic Tests ===\n");
    RUN_TEST(test_iambic_init);
    RUN_TEST(test_iambic_dit);
    RUN_TEST(test_iambic_dah);
    RUN_TEST(test_iambic_mode_a_squeeze);
    RUN_TEST(test_iambic_mode_b_squeeze);
    RUN_TEST(test_iambic_memory);

    /* Iambic Preset tests */
    printf("\n=== Iambic Preset Tests ===\n");
    RUN_TEST(test_preset_init);
    RUN_TEST(test_preset_activate);
    RUN_TEST(test_preset_get_set_values);
    RUN_TEST(test_preset_copy);
    RUN_TEST(test_preset_reset);
    RUN_TEST(test_preset_set_name);
    RUN_TEST(test_preset_timing_helpers);
    RUN_TEST(test_preset_null_safety);

    /* Sidetone tests */
    printf("\n=== Sidetone Tests ===\n");
    RUN_TEST(test_sidetone_init);
    RUN_TEST(test_sidetone_keying);
    RUN_TEST(test_sidetone_fade);

    /* Fault tests */
    printf("\n=== Fault Tests ===\n");
    RUN_TEST(test_fault_init);
    RUN_TEST(test_fault_set_clear);
    RUN_TEST(test_fault_count);

    /* Console parser tests */
    printf("\n=== Console Parser Tests ===\n");
    RUN_TEST(test_parse_empty_line);
    RUN_TEST(test_parse_simple_command);
    RUN_TEST(test_parse_command_with_one_arg);
    RUN_TEST(test_parse_command_with_two_args);
    RUN_TEST(test_parse_command_with_three_args);
    RUN_TEST(test_parse_extra_args_ignored);
    RUN_TEST(test_parse_leading_whitespace);
    RUN_TEST(test_parse_trailing_whitespace);
    RUN_TEST(test_parse_multiple_spaces);

    return UNITY_END();
}
