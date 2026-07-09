<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_core — Lock-free stream, sample, consumer, and fault primitives (the heart of the keyer)

Responsibility: Owns the ONE interface every keying event flows through — the lock-free SPMC `keying_stream_t`. Defines the 6-byte `stream_sample_t` event unit, the consumer handles that read it, and the atomic fault state. It must NOT touch hardware, allocate, log via ESP_LOGx, or depend on any other keyer component — it is pure C + stdatomic.h with zero ESP-IDF REQUIRES.

Key abstractions:
- `keying_stream_t`: single-producer/multi-consumer ring buffer. Producer owns `write_idx` (atomic, monotonic); consumers each hold their own thread-local `read_idx`. `stream_push` does silence/RLE compression via `idle_ticks`; `stream_push_raw` records unconditionally. Returns false when buffer full (a FAULT condition).
- `stream_sample_t` (packed 6B): gpio paddle state, local_key (iambic output), audio_level, flags (edge/silence/tx/rx markers), config_gen (also reused as silence tick count).
- Consumers: `stream_consumer_t` (basic), `hard_rt_consumer_t` (MUST keep up — FAULTs on lag > max_lag), `best_effort_consumer_t` (skips + counts drops, never FAULTs).
- `fault_state_t`: all-atomic active/code/data/count; `fault_set/clear`, inline `fault_is_active`. Codes: OVERRUN, LATENCY_EXCEEDED, PRODUCER_OVERRUN, HARDWARE.

Depends on: nothing (REQUIRES ""). Pure standard C.
Used by: keyer_iambic, keyer_hal, keyer_logging, and main/rt_task + bg_task — essentially every component depends on keyer_core.
External deps of note: none. Intentionally free of ESP-IDF so it builds and runs under the host Unity tests in test_host/.

Conventions:
- Built with -Wconversion -Wshadow -Wstrict-prototypes; keep it warning-clean.
- Buffer capacity MUST be power of 2 (asserted in stream_init); `mask` gives fast modulo.
- Producer uses memory_order_acq_rel on write_idx; consumers use acquire on load. Preserve these orderings.
- Backing buffer is caller-provided (PSRAM on target); this module never allocates it.

Gotchas:
- This is the hard-RT data path (Core 0, 100µs ceiling). No malloc, no locks, no logging here — atomics only.
- `config_gen` is overloaded: normal samples carry a config generation, silence markers carry tick count (see sample_silence*). Don't conflate them.
- Hard-RT consumer overrun is not recoverable in place — it sets FAULT and stops; recovery is via explicit resync. "Corrupted CW timing is worse than silence."
- Silence compression means unchanged samples are NOT written — consumers must expand silence markers, and call stream_flush before shutdown to emit pending idle ticks.
<!-- END treecode (auto) -->
