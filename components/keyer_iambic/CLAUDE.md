<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_iambic — Iambic keyer finite state machine (paddle GPIO → keying output)

Responsibility: Owns the pure keying logic that converts paddle state into dit/dah element timing. Takes a timestamp + `gpio_state_t` per tick and emits a `stream_sample_t` with `local_key` set. It must NOT do any I/O, allocation, or hardware access, and must NOT read the clock itself — the caller passes `now_us`. Fully testable on host.

Key abstractions:
- `iambic_processor_t`: the FSM. States IDLE / SEND_DIT / SEND_DAH / GAP, plus element start/end/duration timestamps, per-paddle press/release timestamps, dit/dah memory flags, squeeze tracking, and key_down output.
- `iambic_tick(proc, now_us, gpio)`: the single entrypoint — advances the FSM and returns the output sample. `iambic_init/set_config/reset`.
- `iambic_config_t`: wpm, mode (A/B), memory_mode, squeeze_mode, memory-window start/end percentages. Inline PARIS-timing helpers (`iambic_dit_duration_us` = 1200000/wpm, dah = 3×, gap = 1×).
- `iambic_preset.h`: 10-slot atomic preset store (`g_iambic_presets`) with RT-safe (<200ns) accessors. NOTE: preset store is deprecated in favor of unified g_config, but its enum types (iambic_mode_t, memory_mode_t, squeeze_mode_t) remain the canonical definitions.

Depends on: keyer_core (for sample.h / gpio_state_t / stream_sample_t).
Used by: main/rt_task (Core 0 RT loop) which feeds it paddle reads from keyer_hal and pushes its output samples into the stream.
External deps of note: none — no ESP-IDF, builds under host tests.

Conventions:
- Built with -Wconversion -Wshadow -Wstrict-prototypes.
- All timing is int64 microseconds; caller supplies `now_us` (esp_timer on target, synthetic in tests). Never call a clock in here.
- Preset fields are atomic and edited off the RT path; the RT path only reads them.

Gotchas:
- Runs on the hard-RT path — keep `iambic_tick` allocation-free, lock-free, and bounded.
- Mode A stops immediately on paddle release; Mode B completes the current element plus one bonus element — do not "simplify" this asymmetry away.
- Memory window: paddle presses are only latched between start%/end% of the element; presses before start% (debounce) or after end% are dropped by design.
- Release debounce: `IAMBIC_DEBOUNCE_RELEASE_US` (5ms) blanks re-presses of the same paddle after release to kill contact bounce.
- squeeze_mode LATCH_OFF = live paddle sampling, LATCH_ON = snapshot at element start — different feel, both intentional.
<!-- END treecode (auto) -->
