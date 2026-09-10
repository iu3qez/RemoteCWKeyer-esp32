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
void test_stream_read_rejects_the_slot_being_overwritten(void);
void test_stream_resync_lands_on_the_oldest_readable(void);
void test_stream_two_threads_never_accept_a_stale_or_torn_sample(void);



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
void test_parse_fragmented_oversized_long_frame_is_skipped_not_error(void);
void test_parse_fragmented_256_exact_still_buffers_and_copies(void);
void test_parse_long_frame_65535_single_read_no_copy(void);
void test_frame_build_empty_payload_writes_only_command_byte(void);
void test_frame_build_short_payload_92_bytes(void);
void test_frame_build_long_payload_300_bytes_little_endian_length(void);
void test_frame_build_buffer_too_small_rejects_without_writing(void);
void test_frame_build_matches_ref_tx_info_moritz(void);
void test_frame_build_matches_ref_tx_info_nobody(void);

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
void test_ping_build_request_layout(void);
void test_ping_build_request_buffer_too_small(void);
void test_ping_build_request_null_buffer(void);
void test_ping_build_response2_from_response1(void);
void test_ping_build_response2_wrong_type(void);
void test_ping_build_response2_buffer_too_small(void);
void test_ping_build_response2_null(void);
void test_ping_peak_hold_jumps_to_new_peak(void);
void test_ping_peak_hold_decays_by_tenth_of_gap(void);
void test_ping_peak_hold_null(void);
void test_ping_peak_hold_gate_rejects_over_2000ms(void);
void test_ping_peak_hold_gate_boundary(void);
void test_ping_peak_hold_gate_rejects_negative_rtt_from_wrap(void);
void test_ping_peak_hold_accepts_zero(void);

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
void test_client_reaches_ready_on_connect_echo(void);
void test_client_keeps_permissions_from_connect_echo(void);
void test_client_refuses_to_key_without_transmit_permission(void);
void test_client_responds_to_ping_request(void);
void test_client_syncs_timer_on_ping_request(void);
void test_client_updates_latency_on_ping_response2(void);
void test_client_tx_first_transition_of_an_over_waits_zero(void);
void test_client_tx_wait_is_measured_from_previous_transition(void);
void test_client_tx_first_over_matches_reference_capture(void);
void test_client_tx_advances_by_encoded_not_measured_ms(void);
void test_client_tx_splits_wait_above_1165_ms(void);
void test_client_tx_end_of_over_after_14_dot_times(void);
void test_client_tx_end_of_over_splits_at_slow_speed(void);
void test_client_tx_ignores_repeated_key_state(void);
void test_client_tx_wait_beyond_one_frame_rebases_on_the_edge(void);
void test_client_rx_decodes_every_event_of_a_morse_frame(void);
void test_client_rx_synthetic_frame_from_reference_encoder(void);
void test_client_rx_frame_in_fragments(void);
void test_client_rx_event_carries_reception_time(void);
void test_client_rx_fifo_full_drops_and_counts(void);
void test_client_rx_ignores_ci_v_and_spectrum(void);
void test_client_latency_peak_holds_and_decays_like_the_reference(void);
void test_client_ping_peak_hold_gate_rejects_out_of_range_rtt(void);
void test_client_round_trip_returns_the_edges_sent(void);
void test_client_tx_first_over_with_ptt_matches_reference_capture_whole(void);
void test_client_tx_ptt_holds_across_gaps_shorter_than_the_tail(void);
void test_client_abort_over_drops_ptt_too(void);
void test_client_rx_keeps_the_rig_result(void);
void test_client_rejects_events_when_not_ready(void);
/* LED vocabulary: the colour rule (#26) */
void test_led_green_belongs_to_on_air_and_to_nothing_else(void);
void test_led_each_situation_carries_its_own_colour(void);
void test_led_a_steady_situation_lights_the_whole_strip_dim(void);
void test_led_overlay_carries_position_and_never_a_colour(void);
void test_led_overlay_shows_the_paddle_in_every_situation(void);
void test_led_notification_flashes_in_the_colour_it_lands_on(void);
void test_led_breathing_rises_and_falls_over_the_period(void);
void test_led_render_clamps_the_strip_and_survives_null(void);
void test_client_can_transmit_only_when_the_reference_would_play_it(void);
void test_client_can_transmit_needs_transmit_permission_and_ready(void);
void test_client_key_holder_unknown_until_announced(void);
void test_client_key_holder_reads_the_three_captured_announcements(void);
void test_client_key_holder_another_client_is_other(void);
void test_client_key_holder_alternates_as_the_reference_announces(void);
void test_client_key_holder_frame_in_fragments(void);
void test_client_key_holder_ignores_malformed_tx_info(void);
void test_client_key_holder_forgotten_across_a_reconnect(void);
void test_client_callsign_goes_on_the_wire_and_decides_mine(void);
void test_client_callsign_falls_back_to_the_username(void);
void test_client_key_holder_index_zero_is_never_mine(void);
void test_client_handles_invalid_frame(void);
void test_client_handles_disconnect_during_operation(void);
void test_client_handles_fragmented_frame(void);
void test_client_handles_ping_in_fragments(void);

/* CWNet feed (stream -> client) */
void test_feed_waits_are_tick_distances_whatever_the_drain(void);
void test_feed_first_over_from_stream_matches_reference_capture(void);
void test_feed_idle_stream_ages_on_the_caller_clock(void);
void test_feed_drains_while_client_not_ready_and_does_not_replay(void);
void test_feed_overrun_closes_the_over_on_the_wire(void);
void test_feed_no_dot_time_no_end_of_over(void);
void test_feed_drain_granularity_does_not_change_the_bytes(void);
void test_feed_send_failure_closes_the_over_and_retries(void);
void test_feed_disconnect_mid_over_then_reconnect(void);
void test_feed_long_key_down_is_sent_in_full(void);

/* CWNet playback engine (station side: bytes -> key edges and PTT) */
void test_play_first_over_of_the_capture_plays_at_the_encoded_instants(void);
void test_play_a_late_tick_does_not_move_the_edges(void);
void test_play_a_late_byte_does_not_move_the_deadline_it_follows(void);
void test_play_an_over_delivered_as_it_is_keyed_plays_like_a_buffered_one(void);
void test_play_an_element_longer_than_the_buffer_is_not_an_underrun(void);
void test_play_two_event_frames_play_at_their_encoded_distances(void);
void test_play_a_split_wait_makes_one_edge_after_the_sum(void);
void test_play_a_key_down_longer_than_one_byte_is_not_an_end_of_over(void);
void test_play_a_split_wait_delivered_at_the_release_keys_the_hold(void);
void test_play_a_split_wait_that_arrives_late_makes_its_edge_on_arrival(void);
void test_play_a_key_held_down_holds_until_the_byte_that_lifts_it(void);
void test_play_force_release_lifts_a_held_key_at_the_instant_it_is_called(void);
void test_play_a_byte_past_its_deadline_makes_its_edge_on_arrival(void);
void test_play_end_of_over_does_not_wait_out_the_marker(void);
void test_play_key_ups_split_by_a_key_down_are_not_an_end_of_over(void);
void test_play_ptt_lead_raises_the_ptt_before_the_first_key_down(void);
void test_play_ptt_lead_is_clamped_to_the_buffer(void);
void test_play_a_full_fifo_drops_the_byte_and_counts_it(void);
void test_play_force_release_lifts_the_key_and_drops_the_ptt_at_the_tail(void);
void test_play_force_release_when_idle_finishes_the_over_at_once(void);
void test_play_start_over_fixes_the_buffer_and_clears_the_queue(void);
void test_play_survives_null_and_reports_nothing(void);
void test_play_only_a_held_element_leaves_the_key_down(void);

/* The receive FIFO both ends share (cwnet_rxfifo.h) */
void test_rxfifo_fills_wraps_and_keeps_order(void);
void test_rxfifo_peek_does_not_consume(void);
void test_rxfifo_buffered_ms_sums_across_the_wrap(void);
void test_rxfifo_end_of_over_seen_across_the_wrap(void);

/* CWNet server core (station side: CONNECT, key, PING, rig strings) */
void test_server_connect_echo_matches_the_capture(void);
void test_server_connect_of_the_wrong_length_closes_the_client(void);
void test_server_connect_arriving_one_byte_at_a_time_still_logs_in(void);
void test_server_an_empty_callsign_is_announced_as_nocall(void);
void test_server_connect_fields_without_a_nul_are_read_no_further(void);
void test_server_a_connection_past_the_limit_is_accepted_and_closed(void);
void test_server_a_client_that_never_logs_in_is_closed(void);
void test_server_three_unanswered_pings_close_the_client(void);
void test_server_ping_request_and_response2_carry_the_reference_layout(void);
void test_server_a_response_that_matches_no_pending_request_is_ignored(void);
void test_server_answers_a_ping_request_from_the_client(void);
void test_server_set_ptt_is_acknowledged_and_never_applied(void);
void test_server_any_other_rig_string_gets_a_negative_code(void);
void test_server_a_rig_string_without_a_nul_closes_the_client(void);
void test_server_ignores_ci_v_and_spectrum_and_closes_on_a_parse_error(void);
void test_server_plays_the_first_over_of_the_capture_and_announces_both_ends(void);
void test_server_a_byte_past_its_deadline_is_reported_with_the_running_totals(void);
void test_server_morse_from_a_client_without_the_key_is_dropped_in_silence(void);
void test_server_announces_the_holder_to_every_client(void);
void test_server_a_link_over_the_ceiling_does_not_get_the_key(void);
void test_server_a_link_under_the_ceiling_sets_the_buffer_to_its_peak(void);
void test_server_the_holder_disconnecting_frees_the_key_for_the_others(void);
void test_server_a_holder_lost_inside_the_buffer_is_still_named_in_the_fault(void);
void test_server_silence_with_the_key_up_releases_the_key_with_a_fault(void);
void test_server_a_key_held_down_survives_the_idle_net_while_the_pings_answer(void);
void test_server_the_pings_free_a_key_held_down_by_a_client_that_died(void);
void test_server_an_over_past_its_ceiling_is_cut_off(void);
void test_server_a_huge_morse_frame_fills_the_engine_and_counts_the_rest(void);
void test_server_a_failed_send_closes_that_client(void);
void test_server_survives_null_and_unknown_indices(void);
void test_server_a_send_dying_mid_announcement_leaves_no_stale_tx_info(void);
void test_server_a_link_that_never_answers_inside_the_window_is_not_fit(void);
void test_server_a_link_that_has_never_been_pinged_still_gets_the_key(void);
void test_server_an_unfit_link_is_reported_once_not_once_per_frame(void);
void test_server_next_deadline_is_the_nearest_of_the_over_and_the_pings(void);
void test_server_next_deadline_takes_an_expiring_handshake_before_the_rest(void);

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
    RUN_TEST(test_stream_read_rejects_the_slot_being_overwritten);
    RUN_TEST(test_stream_resync_lands_on_the_oldest_readable);
    RUN_TEST(test_stream_two_threads_never_accept_a_stale_or_torn_sample);

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
    RUN_TEST(test_parse_fragmented_oversized_long_frame_is_skipped_not_error);
    RUN_TEST(test_parse_fragmented_256_exact_still_buffers_and_copies);
    RUN_TEST(test_parse_long_frame_65535_single_read_no_copy);
    /* Frame builders */
    RUN_TEST(test_frame_build_empty_payload_writes_only_command_byte);
    RUN_TEST(test_frame_build_short_payload_92_bytes);
    RUN_TEST(test_frame_build_long_payload_300_bytes_little_endian_length);
    RUN_TEST(test_frame_build_buffer_too_small_rejects_without_writing);
    RUN_TEST(test_frame_build_matches_ref_tx_info_moritz);
    RUN_TEST(test_frame_build_matches_ref_tx_info_nobody);
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
    /* Initiator side: the daemon builds these (U2) */
    RUN_TEST(test_ping_build_request_layout);
    RUN_TEST(test_ping_build_request_buffer_too_small);
    RUN_TEST(test_ping_build_request_null_buffer);
    RUN_TEST(test_ping_build_response2_from_response1);
    RUN_TEST(test_ping_build_response2_wrong_type);
    RUN_TEST(test_ping_build_response2_buffer_too_small);
    RUN_TEST(test_ping_build_response2_null);
    /* Peak-hold, shared by client and server, behind the 0..2000 ms gate */
    RUN_TEST(test_ping_peak_hold_jumps_to_new_peak);
    RUN_TEST(test_ping_peak_hold_decays_by_tenth_of_gap);
    RUN_TEST(test_ping_peak_hold_null);
    RUN_TEST(test_ping_peak_hold_gate_rejects_over_2000ms);
    RUN_TEST(test_ping_peak_hold_gate_boundary);
    RUN_TEST(test_ping_peak_hold_gate_rejects_negative_rtt_from_wrap);
    RUN_TEST(test_ping_peak_hold_accepts_zero);

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
    RUN_TEST(test_client_reaches_ready_on_connect_echo);
    RUN_TEST(test_client_keeps_permissions_from_connect_echo);
    RUN_TEST(test_client_refuses_to_key_without_transmit_permission);
    /* PING Handling */
    RUN_TEST(test_client_responds_to_ping_request);
    RUN_TEST(test_client_syncs_timer_on_ping_request);
    RUN_TEST(test_client_updates_latency_on_ping_response2);
    /* Keying TX: MORSE 0x10 against the DL4YHF reference */
    RUN_TEST(test_client_tx_first_transition_of_an_over_waits_zero);
    RUN_TEST(test_client_tx_wait_is_measured_from_previous_transition);
    RUN_TEST(test_client_tx_first_over_matches_reference_capture);
    RUN_TEST(test_client_tx_advances_by_encoded_not_measured_ms);
    RUN_TEST(test_client_tx_splits_wait_above_1165_ms);
    RUN_TEST(test_client_tx_end_of_over_after_14_dot_times);
    RUN_TEST(test_client_tx_end_of_over_splits_at_slow_speed);
    RUN_TEST(test_client_tx_ignores_repeated_key_state);
    RUN_TEST(test_client_tx_wait_beyond_one_frame_rebases_on_the_edge);
    /* Received keying and latency */
    RUN_TEST(test_client_rx_decodes_every_event_of_a_morse_frame);
    RUN_TEST(test_client_rx_synthetic_frame_from_reference_encoder);
    RUN_TEST(test_client_rx_frame_in_fragments);
    RUN_TEST(test_client_rx_event_carries_reception_time);
    RUN_TEST(test_client_rx_fifo_full_drops_and_counts);
    RUN_TEST(test_client_rx_ignores_ci_v_and_spectrum);
    RUN_TEST(test_client_latency_peak_holds_and_decays_like_the_reference);
    RUN_TEST(test_client_ping_peak_hold_gate_rejects_out_of_range_rtt);
    RUN_TEST(test_client_round_trip_returns_the_edges_sent);
    RUN_TEST(test_client_tx_first_over_with_ptt_matches_reference_capture_whole);
    RUN_TEST(test_client_tx_ptt_holds_across_gaps_shorter_than_the_tail);
    RUN_TEST(test_client_abort_over_drops_ptt_too);
    RUN_TEST(test_client_rx_keeps_the_rig_result);
    RUN_TEST(test_client_rejects_events_when_not_ready);
    /* Who has the key: TX_INFO 0x05 against the 2026-09-05 capture */
    /* LED vocabulary: green means on air, and nothing else is green */
    RUN_TEST(test_led_green_belongs_to_on_air_and_to_nothing_else);
    RUN_TEST(test_led_each_situation_carries_its_own_colour);
    RUN_TEST(test_led_a_steady_situation_lights_the_whole_strip_dim);
    RUN_TEST(test_led_overlay_carries_position_and_never_a_colour);
    RUN_TEST(test_led_overlay_shows_the_paddle_in_every_situation);
    RUN_TEST(test_led_notification_flashes_in_the_colour_it_lands_on);
    RUN_TEST(test_led_breathing_rises_and_falls_over_the_period);
    RUN_TEST(test_led_render_clamps_the_strip_and_survives_null);
    RUN_TEST(test_client_can_transmit_only_when_the_reference_would_play_it);
    RUN_TEST(test_client_can_transmit_needs_transmit_permission_and_ready);
    RUN_TEST(test_client_key_holder_unknown_until_announced);
    RUN_TEST(test_client_key_holder_reads_the_three_captured_announcements);
    RUN_TEST(test_client_key_holder_another_client_is_other);
    RUN_TEST(test_client_key_holder_alternates_as_the_reference_announces);
    RUN_TEST(test_client_key_holder_frame_in_fragments);
    RUN_TEST(test_client_key_holder_ignores_malformed_tx_info);
    RUN_TEST(test_client_key_holder_forgotten_across_a_reconnect);
    RUN_TEST(test_client_callsign_goes_on_the_wire_and_decides_mine);
    RUN_TEST(test_client_callsign_falls_back_to_the_username);
    RUN_TEST(test_client_key_holder_index_zero_is_never_mine);
    /* Error Handling */
    RUN_TEST(test_client_handles_invalid_frame);
    RUN_TEST(test_client_handles_disconnect_during_operation);
    /* Fragmentation */
    RUN_TEST(test_client_handles_fragmented_frame);
    RUN_TEST(test_client_handles_ping_in_fragments);

    /* CWNet feed: from the keying stream to the wire, on stream time */
    printf("\n=== CWNet Feed Tests ===\n");
    RUN_TEST(test_feed_waits_are_tick_distances_whatever_the_drain);
    RUN_TEST(test_feed_first_over_from_stream_matches_reference_capture);
    RUN_TEST(test_feed_idle_stream_ages_on_the_caller_clock);
    RUN_TEST(test_feed_drains_while_client_not_ready_and_does_not_replay);
    RUN_TEST(test_feed_overrun_closes_the_over_on_the_wire);
    RUN_TEST(test_feed_no_dot_time_no_end_of_over);
    RUN_TEST(test_feed_drain_granularity_does_not_change_the_bytes);
    RUN_TEST(test_feed_send_failure_closes_the_over_and_retries);
    RUN_TEST(test_feed_disconnect_mid_over_then_reconnect);
    RUN_TEST(test_feed_long_key_down_is_sent_in_full);

    /* CWNet playback: the station plays the key holder's bytes and the PTT */
    printf("\n=== CWNet Playback Tests ===\n");
    RUN_TEST(test_play_first_over_of_the_capture_plays_at_the_encoded_instants);
    RUN_TEST(test_play_a_late_tick_does_not_move_the_edges);
    RUN_TEST(test_play_a_late_byte_does_not_move_the_deadline_it_follows);
    RUN_TEST(test_play_an_over_delivered_as_it_is_keyed_plays_like_a_buffered_one);
    RUN_TEST(test_play_an_element_longer_than_the_buffer_is_not_an_underrun);
    RUN_TEST(test_play_two_event_frames_play_at_their_encoded_distances);
    RUN_TEST(test_play_a_split_wait_makes_one_edge_after_the_sum);
    RUN_TEST(test_play_a_key_down_longer_than_one_byte_is_not_an_end_of_over);
    RUN_TEST(test_play_a_split_wait_delivered_at_the_release_keys_the_hold);
    RUN_TEST(test_play_a_split_wait_that_arrives_late_makes_its_edge_on_arrival);
    RUN_TEST(test_play_a_key_held_down_holds_until_the_byte_that_lifts_it);
    RUN_TEST(test_play_force_release_lifts_a_held_key_at_the_instant_it_is_called);
    RUN_TEST(test_play_a_byte_past_its_deadline_makes_its_edge_on_arrival);
    RUN_TEST(test_play_end_of_over_does_not_wait_out_the_marker);
    RUN_TEST(test_play_key_ups_split_by_a_key_down_are_not_an_end_of_over);
    RUN_TEST(test_play_ptt_lead_raises_the_ptt_before_the_first_key_down);
    RUN_TEST(test_play_ptt_lead_is_clamped_to_the_buffer);
    RUN_TEST(test_play_a_full_fifo_drops_the_byte_and_counts_it);
    RUN_TEST(test_play_force_release_lifts_the_key_and_drops_the_ptt_at_the_tail);
    RUN_TEST(test_play_force_release_when_idle_finishes_the_over_at_once);
    RUN_TEST(test_play_start_over_fixes_the_buffer_and_clears_the_queue);
    RUN_TEST(test_play_survives_null_and_reports_nothing);
    RUN_TEST(test_play_only_a_held_element_leaves_the_key_down);
    RUN_TEST(test_rxfifo_fills_wraps_and_keeps_order);
    RUN_TEST(test_rxfifo_peek_does_not_consume);
    RUN_TEST(test_rxfifo_buffered_ms_sums_across_the_wrap);
    RUN_TEST(test_rxfifo_end_of_over_seen_across_the_wrap);

    /* CWNet server core: the station takes a client in and arbitrates the key */
    printf("\n=== CWNet Server Tests ===\n");
    RUN_TEST(test_server_connect_echo_matches_the_capture);
    RUN_TEST(test_server_connect_of_the_wrong_length_closes_the_client);
    RUN_TEST(test_server_connect_arriving_one_byte_at_a_time_still_logs_in);
    RUN_TEST(test_server_an_empty_callsign_is_announced_as_nocall);
    RUN_TEST(test_server_connect_fields_without_a_nul_are_read_no_further);
    RUN_TEST(test_server_a_connection_past_the_limit_is_accepted_and_closed);
    RUN_TEST(test_server_a_client_that_never_logs_in_is_closed);
    RUN_TEST(test_server_three_unanswered_pings_close_the_client);
    RUN_TEST(test_server_ping_request_and_response2_carry_the_reference_layout);
    RUN_TEST(test_server_a_response_that_matches_no_pending_request_is_ignored);
    RUN_TEST(test_server_answers_a_ping_request_from_the_client);
    RUN_TEST(test_server_set_ptt_is_acknowledged_and_never_applied);
    RUN_TEST(test_server_any_other_rig_string_gets_a_negative_code);
    RUN_TEST(test_server_a_rig_string_without_a_nul_closes_the_client);
    RUN_TEST(test_server_ignores_ci_v_and_spectrum_and_closes_on_a_parse_error);
    RUN_TEST(test_server_plays_the_first_over_of_the_capture_and_announces_both_ends);
    RUN_TEST(test_server_a_byte_past_its_deadline_is_reported_with_the_running_totals);
    RUN_TEST(test_server_morse_from_a_client_without_the_key_is_dropped_in_silence);
    RUN_TEST(test_server_announces_the_holder_to_every_client);
    RUN_TEST(test_server_a_link_over_the_ceiling_does_not_get_the_key);
    RUN_TEST(test_server_a_link_under_the_ceiling_sets_the_buffer_to_its_peak);
    RUN_TEST(test_server_the_holder_disconnecting_frees_the_key_for_the_others);
    RUN_TEST(test_server_a_holder_lost_inside_the_buffer_is_still_named_in_the_fault);
    RUN_TEST(test_server_silence_with_the_key_up_releases_the_key_with_a_fault);
    RUN_TEST(test_server_a_key_held_down_survives_the_idle_net_while_the_pings_answer);
    RUN_TEST(test_server_the_pings_free_a_key_held_down_by_a_client_that_died);
    RUN_TEST(test_server_an_over_past_its_ceiling_is_cut_off);
    RUN_TEST(test_server_a_huge_morse_frame_fills_the_engine_and_counts_the_rest);
    RUN_TEST(test_server_a_failed_send_closes_that_client);
    RUN_TEST(test_server_survives_null_and_unknown_indices);
    RUN_TEST(test_server_a_send_dying_mid_announcement_leaves_no_stale_tx_info);
    RUN_TEST(test_server_a_link_that_never_answers_inside_the_window_is_not_fit);
    RUN_TEST(test_server_a_link_that_has_never_been_pinged_still_gets_the_key);
    RUN_TEST(test_server_an_unfit_link_is_reported_once_not_once_per_frame);
    RUN_TEST(test_server_next_deadline_is_the_nearest_of_the_over_and_the_pings);
    RUN_TEST(test_server_next_deadline_takes_an_expiring_handshake_before_the_rest);

    return UNITY_END();
}
