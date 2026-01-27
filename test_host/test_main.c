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

/* CWNet Timestamp tests */
void test_timestamp_encode_zero(void);
void test_timestamp_encode_1ms(void);
void test_timestamp_encode_15ms(void);
void test_timestamp_encode_31ms(void);
void test_timestamp_encode_32ms(void);
void test_timestamp_encode_36ms(void);
void test_timestamp_encode_60ms(void);
void test_timestamp_encode_100ms(void);
void test_timestamp_encode_156ms(void);
void test_timestamp_encode_157ms(void);
void test_timestamp_encode_173ms(void);
void test_timestamp_encode_500ms(void);
void test_timestamp_encode_1000ms(void);
void test_timestamp_encode_1165ms(void);
void test_timestamp_encode_negative(void);
void test_timestamp_encode_negative_large(void);
void test_timestamp_encode_overflow(void);
void test_timestamp_encode_large_overflow(void);
void test_timestamp_encode_int_max(void);
void test_timestamp_encode_33ms(void);
void test_timestamp_encode_34ms(void);
void test_timestamp_encode_35ms(void);
void test_timestamp_encode_158ms(void);
void test_timestamp_encode_165ms(void);
void test_timestamp_decode_zero(void);
void test_timestamp_decode_linear_max(void);
void test_timestamp_decode_medium_start(void);
void test_timestamp_decode_medium_end(void);
void test_timestamp_decode_long_start(void);
void test_timestamp_decode_long_end(void);
void test_timestamp_decode_with_key_bit_0x80(void);
void test_timestamp_decode_with_key_bit_0x9F(void);
void test_timestamp_decode_with_key_bit_0xBF(void);
void test_timestamp_decode_with_key_bit_0xFF(void);
void test_timestamp_roundtrip_linear(void);
void test_timestamp_roundtrip_medium(void);
void test_timestamp_roundtrip_long(void);
void test_timestamp_decode_medium_0x27(void);
void test_timestamp_decode_medium_0x31(void);
void test_timestamp_decode_long_0x55(void);
void test_timestamp_decode_long_0x72(void);

/* CWNet Frame Parser tests */
void test_frame_category_no_payload(void);
void test_frame_category_short(void);
void test_frame_category_long(void);
void test_frame_category_reserved(void);
void test_cmd_type_connect(void);
void test_cmd_type_disconnect(void);
void test_cmd_type_ping(void);
void test_cmd_type_morse(void);
void test_cmd_type_audio(void);
void test_parse_disconnect_frame(void);
void test_parse_ping_frame(void);
void test_parse_connect_frame(void);
void test_parse_morse_frame_5bytes(void);
void test_parse_audio_frame_320(void);
void test_parse_audio_frame_256(void);
void test_stream_parse_disconnect_byte_by_byte(void);
void test_stream_parse_ping_byte_by_byte(void);
void test_stream_parse_partial_header(void);
void test_stream_parse_partial_payload(void);
void test_stream_parse_two_disconnects(void);
void test_stream_parse_disconnect_then_ping(void);
void test_parse_reserved_category(void);
void test_parse_incomplete_returns_need_more(void);
void test_parse_zero_length_short_block(void);
void test_parse_null_buffer(void);
void test_parse_empty_buffer(void);
void test_parser_reset_clears_state(void);
void test_parser_init_state(void);
void test_parser_null_safety(void);
void test_stream_parse_long_block_partial_length(void);

/* CWNet PING/Timer tests */
void test_timer_sync_init(void);
void test_timer_sync_no_drift(void);
void test_timer_sync_client_ahead(void);
void test_timer_sync_client_behind(void);
void test_timer_sync_cumulative(void);
void test_timer_sync_drift_correction(void);
void test_timer_null_safety(void);
void test_ping_parse_request(void);
void test_ping_parse_response_1(void);
void test_ping_parse_response_2(void);
void test_ping_parse_invalid_length(void);
void test_ping_parse_null(void);
void test_ping_parse_negative_timestamps(void);
void test_ping_build_response_1(void);
void test_ping_build_response_buffer_too_small(void);
void test_ping_build_response_null(void);
void test_ping_calc_latency_basic(void);
void test_ping_calc_latency_zero(void);
void test_ping_calc_latency_wrap(void);
void test_ping_calc_latency_wrong_type(void);
void test_ping_calc_latency_null(void);
void test_ping_full_sequence(void);
void test_ping_latency_measurement(void);

/* CWNet Client tests */
void test_client_init_basic(void);
void test_client_init_null_client(void);
void test_client_init_null_config(void);
void test_client_init_null_callbacks(void);
void test_client_init_empty_host(void);
void test_client_init_empty_username(void);
void test_client_connect_transitions_to_connecting(void);
void test_client_disconnect_from_any_state(void);
void test_client_sends_ident_on_connect(void);
void test_client_receives_welcome_transitions_to_ready(void);
void test_client_responds_to_ping_request(void);
void test_client_syncs_timer_on_ping_request(void);
void test_client_updates_latency_on_ping_response2(void);
void test_client_sends_key_down_event(void);
void test_client_sends_key_up_event(void);
void test_client_rejects_events_when_not_ready(void);
void test_client_handles_invalid_frame(void);
void test_client_handles_disconnect_during_operation(void);
void test_client_handles_fragmented_frame(void);
void test_client_handles_ping_in_fragments(void);

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

    /* CWNet Timestamp tests */
    printf("\n=== CWNet Timestamp Tests ===\n");
    /* Encoding: Linear range (0-31ms, 1ms resolution) */
    RUN_TEST(test_timestamp_encode_zero);
    RUN_TEST(test_timestamp_encode_1ms);
    RUN_TEST(test_timestamp_encode_15ms);
    RUN_TEST(test_timestamp_encode_31ms);
    /* Encoding: Medium range (32-156ms, 4ms resolution) */
    RUN_TEST(test_timestamp_encode_32ms);
    RUN_TEST(test_timestamp_encode_36ms);
    RUN_TEST(test_timestamp_encode_60ms);
    RUN_TEST(test_timestamp_encode_100ms);
    RUN_TEST(test_timestamp_encode_156ms);
    /* Encoding: Long range (157-1165ms, 16ms resolution) */
    RUN_TEST(test_timestamp_encode_157ms);
    RUN_TEST(test_timestamp_encode_173ms);
    RUN_TEST(test_timestamp_encode_500ms);
    RUN_TEST(test_timestamp_encode_1000ms);
    RUN_TEST(test_timestamp_encode_1165ms);
    /* Encoding: Edge cases */
    RUN_TEST(test_timestamp_encode_negative);
    RUN_TEST(test_timestamp_encode_negative_large);
    RUN_TEST(test_timestamp_encode_overflow);
    RUN_TEST(test_timestamp_encode_large_overflow);
    RUN_TEST(test_timestamp_encode_int_max);
    /* Encoding: Resolution boundaries */
    RUN_TEST(test_timestamp_encode_33ms);
    RUN_TEST(test_timestamp_encode_34ms);
    RUN_TEST(test_timestamp_encode_35ms);
    RUN_TEST(test_timestamp_encode_158ms);
    RUN_TEST(test_timestamp_encode_165ms);
    /* Decoding: Basic */
    RUN_TEST(test_timestamp_decode_zero);
    RUN_TEST(test_timestamp_decode_linear_max);
    RUN_TEST(test_timestamp_decode_medium_start);
    RUN_TEST(test_timestamp_decode_medium_end);
    RUN_TEST(test_timestamp_decode_long_start);
    RUN_TEST(test_timestamp_decode_long_end);
    /* Decoding: Key bit masking */
    RUN_TEST(test_timestamp_decode_with_key_bit_0x80);
    RUN_TEST(test_timestamp_decode_with_key_bit_0x9F);
    RUN_TEST(test_timestamp_decode_with_key_bit_0xBF);
    RUN_TEST(test_timestamp_decode_with_key_bit_0xFF);
    /* Decoding: Additional coverage */
    RUN_TEST(test_timestamp_decode_medium_0x27);
    RUN_TEST(test_timestamp_decode_medium_0x31);
    RUN_TEST(test_timestamp_decode_long_0x55);
    RUN_TEST(test_timestamp_decode_long_0x72);
    /* Round-trip property tests */
    RUN_TEST(test_timestamp_roundtrip_linear);
    RUN_TEST(test_timestamp_roundtrip_medium);
    RUN_TEST(test_timestamp_roundtrip_long);

    /* CWNet Frame Parser tests */
    printf("\n=== CWNet Frame Parser Tests ===\n");
    /* Category detection */
    RUN_TEST(test_frame_category_no_payload);
    RUN_TEST(test_frame_category_short);
    RUN_TEST(test_frame_category_long);
    RUN_TEST(test_frame_category_reserved);
    /* Command extraction */
    RUN_TEST(test_cmd_type_connect);
    RUN_TEST(test_cmd_type_disconnect);
    RUN_TEST(test_cmd_type_ping);
    RUN_TEST(test_cmd_type_morse);
    RUN_TEST(test_cmd_type_audio);
    /* Complete frame parsing */
    RUN_TEST(test_parse_disconnect_frame);
    RUN_TEST(test_parse_ping_frame);
    RUN_TEST(test_parse_connect_frame);
    RUN_TEST(test_parse_morse_frame_5bytes);
    RUN_TEST(test_parse_audio_frame_320);
    RUN_TEST(test_parse_audio_frame_256);
    /* Streaming parser */
    RUN_TEST(test_stream_parse_disconnect_byte_by_byte);
    RUN_TEST(test_stream_parse_ping_byte_by_byte);
    RUN_TEST(test_stream_parse_partial_header);
    RUN_TEST(test_stream_parse_partial_payload);
    RUN_TEST(test_stream_parse_two_disconnects);
    RUN_TEST(test_stream_parse_disconnect_then_ping);
    RUN_TEST(test_stream_parse_long_block_partial_length);
    /* Error handling */
    RUN_TEST(test_parse_reserved_category);
    RUN_TEST(test_parse_incomplete_returns_need_more);
    RUN_TEST(test_parse_zero_length_short_block);
    RUN_TEST(test_parse_null_buffer);
    RUN_TEST(test_parse_empty_buffer);
    /* Parser state */
    RUN_TEST(test_parser_reset_clears_state);
    RUN_TEST(test_parser_init_state);
    RUN_TEST(test_parser_null_safety);

    /* CWNet PING/Timer tests */
    printf("\n=== CWNet PING/Timer Tests ===\n");
    /* Timer sync */
    RUN_TEST(test_timer_sync_init);
    RUN_TEST(test_timer_sync_no_drift);
    RUN_TEST(test_timer_sync_client_ahead);
    RUN_TEST(test_timer_sync_client_behind);
    RUN_TEST(test_timer_sync_cumulative);
    RUN_TEST(test_timer_sync_drift_correction);
    RUN_TEST(test_timer_null_safety);
    /* PING parsing */
    RUN_TEST(test_ping_parse_request);
    RUN_TEST(test_ping_parse_response_1);
    RUN_TEST(test_ping_parse_response_2);
    RUN_TEST(test_ping_parse_invalid_length);
    RUN_TEST(test_ping_parse_null);
    RUN_TEST(test_ping_parse_negative_timestamps);
    /* PING building */
    RUN_TEST(test_ping_build_response_1);
    RUN_TEST(test_ping_build_response_buffer_too_small);
    RUN_TEST(test_ping_build_response_null);
    /* Latency calculation */
    RUN_TEST(test_ping_calc_latency_basic);
    RUN_TEST(test_ping_calc_latency_zero);
    RUN_TEST(test_ping_calc_latency_wrap);
    RUN_TEST(test_ping_calc_latency_wrong_type);
    RUN_TEST(test_ping_calc_latency_null);
    /* Integration */
    RUN_TEST(test_ping_full_sequence);
    RUN_TEST(test_ping_latency_measurement);

    /* CWNet Client tests */
    printf("\n=== CWNet Client Tests ===\n");
    /* Initialization */
    RUN_TEST(test_client_init_basic);
    RUN_TEST(test_client_init_null_client);
    RUN_TEST(test_client_init_null_config);
    RUN_TEST(test_client_init_null_callbacks);
    RUN_TEST(test_client_init_empty_host);
    RUN_TEST(test_client_init_empty_username);
    /* State Transitions */
    RUN_TEST(test_client_connect_transitions_to_connecting);
    RUN_TEST(test_client_disconnect_from_any_state);
    /* Protocol Handshake */
    RUN_TEST(test_client_sends_ident_on_connect);
    RUN_TEST(test_client_receives_welcome_transitions_to_ready);
    /* PING Handling */
    RUN_TEST(test_client_responds_to_ping_request);
    RUN_TEST(test_client_syncs_timer_on_ping_request);
    RUN_TEST(test_client_updates_latency_on_ping_response2);
    /* CW Events */
    RUN_TEST(test_client_sends_key_down_event);
    RUN_TEST(test_client_sends_key_up_event);
    RUN_TEST(test_client_rejects_events_when_not_ready);
    /* Error Handling */
    RUN_TEST(test_client_handles_invalid_frame);
    RUN_TEST(test_client_handles_disconnect_during_operation);
    /* Fragmentation */
    RUN_TEST(test_client_handles_fragmented_frame);
    RUN_TEST(test_client_handles_ping_in_fragments);

    return UNITY_END();
}
