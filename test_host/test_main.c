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

/* Morse table tests */
void test_morse_lookup_letters(void);
void test_morse_lookup_numbers(void);
void test_morse_lookup_punctuation(void);
void test_morse_lookup_prosigns(void);
void test_morse_lookup_invalid(void);
void test_morse_reverse_lookup(void);
void test_morse_table_count(void);
void test_morse_match_prosign(void);
void test_morse_get_prosign_tag(void);

/* Timing classifier tests */
void test_timing_init(void);
void test_timing_classify_dit(void);
void test_timing_classify_dah(void);
void test_timing_classify_gaps(void);
void test_timing_warmup(void);
void test_timing_ema_adaptation(void);
void test_timing_ratio(void);
void test_timing_ignore_short_durations(void);
void test_timing_ignore_long_durations(void);
void test_timing_reset(void);
void test_timing_null_safety(void);
void test_key_event_str(void);

/* Decoder tests */
void test_decoder_init(void);
void test_decoder_decode_letter_a(void);
void test_decoder_decode_letter_e(void);
void test_decoder_decode_letter_t(void);
void test_decoder_decode_sos(void);
void test_decoder_word_gap_adds_space(void);
void test_decoder_get_current_pattern(void);
void test_decoder_unknown_pattern(void);
void test_decoder_enable_disable(void);
void test_decoder_reset(void);
void test_decoder_stats(void);
void test_decoder_timing_access(void);
void test_decoder_state_str(void);
void test_decoder_buffer_circular(void);
void test_decoder_get_text_with_timestamps(void);

/* Morse lookup tests */
void test_morse_lookup_A(void);
void test_morse_lookup_B(void);
void test_morse_lookup_E(void);
void test_morse_lookup_T(void);
void test_morse_lookup_0(void);
void test_morse_lookup_5(void);
void test_morse_lookup_1(void);
void test_morse_lookup_period(void);
void test_morse_lookup_question(void);
void test_morse_lookup_slash(void);
void test_morse_lookup_equals(void);
void test_morse_lookup_comma(void);
void test_morse_lookup_lowercase(void);
void test_morse_lookup_space_returns_null(void);
void test_morse_lookup_unsupported_returns_null(void);
void test_morse_lookup_all_letters(void);
void test_morse_lookup_all_digits(void);

/* Morse queue tests */
void test_morse_queue_init(void);
void test_morse_queue_push_pop(void);
void test_morse_queue_all_element_types(void);
void test_morse_queue_pop_empty_returns_false(void);
void test_morse_queue_fill_to_capacity(void);
void test_morse_queue_wraparound(void);
void test_morse_queue_clear(void);
void test_morse_queue_interleaved_ops(void);
void test_morse_queue_size_is_power_of_2(void);

/* Winkeyer parser tests */
void test_parser_init(void);
void test_parser_host_open(void);
void test_parser_host_close(void);
void test_parser_admin_echo(void);
void test_parser_admin_reset(void);
void test_parser_speed_command(void);
void test_parser_speed_requires_session(void);
void test_parser_sidetone_command(void);
void test_parser_weight_command(void);
void test_parser_ptt_timing_command(void);
void test_parser_pin_config_command(void);
void test_parser_mode_command(void);
void test_parser_text_characters(void);
void test_parser_text_requires_session(void);
void test_parser_text_full_alphabet(void);
void test_parser_clear_buffer_command(void);
void test_parser_key_immediate_down(void);
void test_parser_key_immediate_up(void);
void test_parser_pause_command(void);
void test_parser_unpause_command(void);
void test_parser_invalid_command_ignored(void);
void test_parser_invalid_admin_sub_ignored(void);
void test_parser_state_transitions(void);
void test_parser_two_param_state_transitions(void);
void test_protocol_constants(void);
void test_parser_null_callbacks_safe(void);
void test_parser_null_response_safe(void);

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

    /* Morse table tests */
    printf("\n=== Morse Table Tests ===\n");
    RUN_TEST(test_morse_lookup_letters);
    RUN_TEST(test_morse_lookup_numbers);
    RUN_TEST(test_morse_lookup_punctuation);
    RUN_TEST(test_morse_lookup_prosigns);
    RUN_TEST(test_morse_lookup_invalid);
    RUN_TEST(test_morse_reverse_lookup);
    RUN_TEST(test_morse_table_count);
    RUN_TEST(test_morse_match_prosign);
    RUN_TEST(test_morse_get_prosign_tag);

    /* Timing classifier tests */
    printf("\n=== Timing Classifier Tests ===\n");
    RUN_TEST(test_timing_init);
    RUN_TEST(test_timing_classify_dit);
    RUN_TEST(test_timing_classify_dah);
    RUN_TEST(test_timing_classify_gaps);
    RUN_TEST(test_timing_warmup);
    RUN_TEST(test_timing_ema_adaptation);
    RUN_TEST(test_timing_ratio);
    RUN_TEST(test_timing_ignore_short_durations);
    RUN_TEST(test_timing_ignore_long_durations);
    RUN_TEST(test_timing_reset);
    RUN_TEST(test_timing_null_safety);
    RUN_TEST(test_key_event_str);

    /* Decoder tests */
    printf("\n=== Decoder Tests ===\n");
    RUN_TEST(test_decoder_init);
    RUN_TEST(test_decoder_decode_letter_a);
    RUN_TEST(test_decoder_decode_letter_e);
    RUN_TEST(test_decoder_decode_letter_t);
    RUN_TEST(test_decoder_decode_sos);
    RUN_TEST(test_decoder_word_gap_adds_space);
    RUN_TEST(test_decoder_get_current_pattern);
    RUN_TEST(test_decoder_unknown_pattern);
    RUN_TEST(test_decoder_enable_disable);
    RUN_TEST(test_decoder_reset);
    RUN_TEST(test_decoder_stats);
    RUN_TEST(test_decoder_timing_access);
    RUN_TEST(test_decoder_state_str);
    RUN_TEST(test_decoder_buffer_circular);
    RUN_TEST(test_decoder_get_text_with_timestamps);

    /* Morse lookup tests */
    printf("\n=== Morse Lookup Tests ===\n");
    RUN_TEST(test_morse_lookup_A);
    RUN_TEST(test_morse_lookup_B);
    RUN_TEST(test_morse_lookup_E);
    RUN_TEST(test_morse_lookup_T);
    RUN_TEST(test_morse_lookup_0);
    RUN_TEST(test_morse_lookup_5);
    RUN_TEST(test_morse_lookup_1);
    RUN_TEST(test_morse_lookup_period);
    RUN_TEST(test_morse_lookup_question);
    RUN_TEST(test_morse_lookup_slash);
    RUN_TEST(test_morse_lookup_equals);
    RUN_TEST(test_morse_lookup_comma);
    RUN_TEST(test_morse_lookup_lowercase);
    RUN_TEST(test_morse_lookup_space_returns_null);
    RUN_TEST(test_morse_lookup_unsupported_returns_null);
    RUN_TEST(test_morse_lookup_all_letters);
    RUN_TEST(test_morse_lookup_all_digits);

    /* Morse queue tests */
    printf("\n=== Morse Queue Tests ===\n");
    RUN_TEST(test_morse_queue_init);
    RUN_TEST(test_morse_queue_push_pop);
    RUN_TEST(test_morse_queue_all_element_types);
    RUN_TEST(test_morse_queue_pop_empty_returns_false);
    RUN_TEST(test_morse_queue_fill_to_capacity);
    RUN_TEST(test_morse_queue_wraparound);
    RUN_TEST(test_morse_queue_clear);
    RUN_TEST(test_morse_queue_interleaved_ops);
    RUN_TEST(test_morse_queue_size_is_power_of_2);

    /* Winkeyer parser tests */
    printf("\n=== Winkeyer Parser Tests ===\n");
    RUN_TEST(test_parser_init);
    RUN_TEST(test_parser_host_open);
    RUN_TEST(test_parser_host_close);
    RUN_TEST(test_parser_admin_echo);
    RUN_TEST(test_parser_admin_reset);
    RUN_TEST(test_parser_speed_command);
    RUN_TEST(test_parser_speed_requires_session);
    RUN_TEST(test_parser_sidetone_command);
    RUN_TEST(test_parser_weight_command);
    RUN_TEST(test_parser_ptt_timing_command);
    RUN_TEST(test_parser_pin_config_command);
    RUN_TEST(test_parser_mode_command);
    RUN_TEST(test_parser_text_characters);
    RUN_TEST(test_parser_text_requires_session);
    RUN_TEST(test_parser_text_full_alphabet);
    RUN_TEST(test_parser_clear_buffer_command);
    RUN_TEST(test_parser_key_immediate_down);
    RUN_TEST(test_parser_key_immediate_up);
    RUN_TEST(test_parser_pause_command);
    RUN_TEST(test_parser_unpause_command);
    RUN_TEST(test_parser_invalid_command_ignored);
    RUN_TEST(test_parser_invalid_admin_sub_ignored);
    RUN_TEST(test_parser_state_transitions);
    RUN_TEST(test_parser_two_param_state_transitions);
    RUN_TEST(test_protocol_constants);
    RUN_TEST(test_parser_null_callbacks_safe);
    RUN_TEST(test_parser_null_response_safe);

    return UNITY_END();
}
