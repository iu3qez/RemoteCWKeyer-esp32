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
void test_iambic_squeeze_prolonged(void);

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

void test_config_find_param_wpm(void);
void test_config_find_param_unknown(void);
void test_config_get_param_str_wpm(void);
void test_config_set_param_str_wpm(void);
void test_config_set_param_str_out_of_range(void);

void test_history_push_and_prev(void);
void test_history_next(void);
void test_history_wrap_at_depth(void);
void test_history_skip_duplicates(void);
void test_history_skip_empty(void);
void test_history_reset_nav(void);
void test_history_empty(void);

void test_complete_command_help(void);
void test_complete_param_after_set(void);
void test_complete_param_after_show(void);
void test_complete_no_match(void);
void test_complete_multiple_matches(void);
void test_complete_reset(void);
void test_complete_empty_line(void);
void test_complete_buffer_limit(void);
void test_complete_debug_shows_all(void);
void test_complete_debug_with_prefix(void);
void test_complete_diag(void);

void test_diag_disabled_by_default(void);
void test_diag_enable_disable(void);
void test_diag_macro_does_not_crash_when_disabled(void);
void test_diag_macro_logs_when_enabled(void);

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
    RUN_TEST(test_iambic_squeeze_prolonged);

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

    /* Config console tests - TEMPORARILY DISABLED (requires full console system) */
    /* printf("\n=== Config Console Tests ===\n");
    RUN_TEST(test_config_find_param_wpm);
    RUN_TEST(test_config_find_param_unknown);
    RUN_TEST(test_config_get_param_str_wpm);
    RUN_TEST(test_config_set_param_str_wpm);
    RUN_TEST(test_config_set_param_str_out_of_range); */

    /* History tests - TEMPORARILY DISABLED (requires console system) */
    /* printf("\n=== History Tests ===\n");
    RUN_TEST(test_history_push_and_prev);
    RUN_TEST(test_history_next);
    RUN_TEST(test_history_wrap_at_depth);
    RUN_TEST(test_history_skip_duplicates);
    RUN_TEST(test_history_skip_empty);
    RUN_TEST(test_history_reset_nav);
    RUN_TEST(test_history_empty); */

    /* Completion tests - TEMPORARILY DISABLED (requires commands.c) */
    /* printf("\n=== Completion Tests ===\n");
    RUN_TEST(test_complete_command_help);
    RUN_TEST(test_complete_param_after_set);
    RUN_TEST(test_complete_param_after_show);
    RUN_TEST(test_complete_no_match);
    RUN_TEST(test_complete_multiple_matches);
    RUN_TEST(test_complete_reset);
    RUN_TEST(test_complete_empty_line);
    RUN_TEST(test_complete_buffer_limit);
    RUN_TEST(test_complete_debug_shows_all);
    RUN_TEST(test_complete_debug_with_prefix);
    RUN_TEST(test_complete_diag); */

    /* RT Diagnostic tests */
    printf("\n=== RT Diagnostic Tests ===\n");
    RUN_TEST(test_diag_disabled_by_default);
    RUN_TEST(test_diag_enable_disable);
    RUN_TEST(test_diag_macro_does_not_crash_when_disabled);
    RUN_TEST(test_diag_macro_logs_when_enabled);

    return UNITY_END();
}
