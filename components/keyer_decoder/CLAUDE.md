<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_decoder — CW-to-text decoder, adaptive timing, morse table (Core 1 consumer)

Responsibility: Owns best-effort decoding of transmitted CW back into text. Registers as a best-effort consumer on the keying stream, measures mark/space durations, adaptively learns the operator's speed, matches accumulated dit/dah patterns to characters, and buffers decoded text for display. It must NOT be on the RT path and must never stall the stream — dropped samples are acceptable (counted in stats), corrupted timing is not.

Key abstractions:
- `decoder_init()` / `decoder_process()` — init registers the consumer; process drains all pending samples, call ~10ms from bg_task.
- `decoder_pop_char()` / `decoder_get_text()` — FIFO read of decoded characters.
- `timing_classifier_t` — EMA-based (alpha ~0.3) dit/dah averages, 25% tolerance, warmup period; classifies KEY_EVENT_DIT/DAH/INTRA_GAP/CHAR_GAP/WORD_GAP; exposes detected WPM (1200000/dit_avg).
- `morse_table_lookup` / `_reverse` / `morse_match_prosign` / `morse_get_prosign_tag` — static ITU table, linear search (~50 entries), also used by keyer_text.
- `decoder_stats_t`, `decoder_handle_event()` (test injection).

Depends on: keyer_core (the stream + sample/consumer types), esp_timer (timestamps).

Used by: main/bg_task.c calls `decoder_process`; keyer_console, keyer_text (morse table), and keyer_webui consume decoded output / lookups.

External deps of note: esp_timer for `esp_timer_get_time()` on target; on host it is stubbed with `decoder_set_mock_time()` and `decoder_set_test_stream()` so the whole module runs without hardware.

Conventions: Built with -Wconversion -Wshadow -Wstrict-prototypes. Pure logic, host-testable. `ESP_PLATFORM` guards the timer/stream source vs. host stubs.

Gotchas:
- Best-effort by design: if process falls behind, samples are dropped (samples_dropped) — never add blocking or backpressure to protect the RT producer.
- Timing classifier reports WPM 0 until warmup completes; check `timing_classifier_is_calibrated()` before trusting it.
- Inactivity timeout (7 dit units of silence) force-finalizes the in-progress character.
- All decoder state is single-threaded on Core 1; readers (console/webui) call the getters from the same task context.
<!-- END treecode (auto) -->
