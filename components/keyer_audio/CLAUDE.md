<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_audio — Sidetone synthesis, audio ring buffer, and PTT control (RT/TX path, Core 0)

Responsibility: Owns turning a key-down boolean into audio samples and PTT state on the hard RT path. Generates sidetone via phase accumulator, buffers samples SPSC, tracks PTT with a tail timeout, and selects between local sidetone and remote audio. It must NOT touch the keying stream directly, allocate, log, or block — the RT task feeds it the already-resolved key state each sample tick.

Key abstractions:
- `sidetone_gen_t` + `sidetone_next_sample(gen, key_down)` — 256-entry sine LUT, 32-bit phase accumulator, fade envelope (FADE_SILENT/IN/SUSTAIN/OUT) to kill key clicks.
- `audio_ring_buffer_t` — lock-free SPSC ring over caller-supplied power-of-2 storage; `push` overwrites oldest when full, `pop` non-blocking.
- `ptt_controller_t` — PTT_ON on audio activity, PTT_OFF after `tail_ms`; optional `ptt_pa_callback_t` for PA keying; `ptt_force_off()` for fault recovery.
- `audio_source_selector_t` — priority sidetone > remote > none.

Depends on: keyer_core (REQUIRES). Consumes no stream type itself — pure sample/state logic.

Used by: main/rt_task.c drives `sidetone_next_sample` and `ptt_tick` every sample/tick on Core 0. Host tests exercise sidetone, buffer, and PTT directly (test_host).

External deps of note: none — no ESP-IDF driver deps; I2S/codec output lives in keyer_hal, which the RT task feeds from this buffer.

Conventions: Built with -Wconversion -Wshadow. Caller owns all storage (ring buffer storage array, generator struct). Fixed-point phase: upper 8 bits index the LUT (PHASE_SHIFT 24).

Gotchas:
- Everything here runs on the 100µs RT path — keep it allocation-free, lock-free, log-free. `sidetone_next_sample` is the per-sample hot loop.
- Ring buffer is strictly one producer / one consumer; a full buffer silently drops the oldest sample rather than blocking.
- The fade state machine, not `key_down`, decides when output is truly silent — check `sidetone_is_active()` (not the raw key) before deciding PTT/idle.
- PTT holds ON for the whole tail window after the last audio; `ptt_tick` must be called even when idle for the timeout to expire.
<!-- END treecode (auto) -->
