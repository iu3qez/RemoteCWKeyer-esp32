<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_text — Text-to-CW playback and NVS message memories (Core 1 producer)

Responsibility: Owns converting ASCII text (letters, digits, punctuation, `<..>` prosigns, spaces) into CW element timing and exposing the resulting key-down state to the RT task. Also owns 8 persistent message slots in NVS. It must NOT write the keying stream or touch GPIO/audio itself — it only publishes a key-down boolean that Core 0 samples.

Key abstractions:
- `text_keyer_send(text)` / `_abort` / `_pause` / `_resume` — single active transmission; state machine IDLE/SENDING/PAUSED.
- `text_keyer_tick(now_us)` — advances element timing; call from bg_task (~10ms) on Core 1.
- `text_keyer_is_key_down()` — atomic load, called from Core 0 RT task (~1ms) to fold text keying into the sidetone/TX path.
- `text_keyer_config_t.paddle_abort` — non-owning `atomic_bool*`; a paddle press aborts playback.
- `text_memory_*` — 8 slots (`TEXT_MEMORY_SLOTS`), text + label, load/get/set/save via NVS.

Depends on: keyer_core, keyer_decoder (for morse_table reverse lookup + prosigns), keyer_config (global WPM), nvs_flash.

Used by: main/bg_task.c ticks it; main/rt_task.c polls `is_key_down`; keyer_console and keyer_webui drive send/memory commands.

External deps of note: nvs_flash for message persistence; reuses keyer_decoder's `morse_table_reverse`/`morse_match_prosign` rather than carrying its own table.

Conventions: Built with -Wconversion -Wshadow -Wstrict-prototypes. WPM comes from `CONFIG_GET_WPM()`, clamped 5–60; dit = 1200000/WPM µs (PARIS).

Gotchas:
- Timing state (`s_send`, `s_state`) is mutated only on Core 1; only `s_key_down` crosses cores — it is atomic (release on write, acquire on read). Do not read other fields from the RT task.
- `element_end_us == 0` is the "start next element / just resumed" sentinel, not a real timestamp.
- Every abort/pause/done path must force key-down false, or a stuck key leaves the TX keyed — the code releases the key on all exits deliberately.
- Single transmission only: `send` returns -1 if not IDLE.
<!-- END treecode (auto) -->
