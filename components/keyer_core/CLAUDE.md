<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_core — Lock-free stream, sample, consumer, and fault primitives (the heart of the keyer)

Responsibility: Owns the ONE interface every keying event flows through — the lock-free SPMC `keying_stream_t`. Defines the 6-byte `stream_sample_t` event unit, the consumer handles that read it, and the atomic fault state. It must NOT touch hardware, allocate, log via ESP_LOGx, or depend on any other keyer component — it is pure C + stdatomic.h with zero ESP-IDF REQUIRES.

Key abstractions:
- `keying_stream_t`: single-producer/multi-consumer ring buffer. The producer is the only writer of `write_idx`; consumers each hold their own thread-local `read_idx`. `stream_push` does silence/RLE compression via `idle_ticks`; `stream_push_raw` records unconditionally. Both — and `stream_flush` — are producer-task-only; nothing else may call them.
- Publish order (fixed by #57, `stream_write_slot`): the producer reads `write_idx` relaxed, stores the sample into the slot, *then* publishes `write_idx + 1` with `memory_order_release`. Publishing the index before the store (the old fetch_add version) let a consumer observe the new index and read a slot mid-write; that ordering is gone and must not come back.
- `stream_read`: consumer acquire-loads `write_idx`, copies the slot, then re-checks `write_idx` behind an `atomic_thread_fence(acquire)` — if the producer reached the slot again meanwhile, the copy is discarded (seqlock-reader pattern). The slot capacity behind `write_idx` is the producer's next slot, so `write - idx >= capacity` (`overrun_at`, wrapping subtraction) is the overrun test used by `stream_read`, `stream_is_overrun` and `consumer_resync` alike.
- `consumer_resync`: lands the consumer exactly `capacity - 1` behind the current write position — the oldest still-readable slot, not `capacity`.
- `stream_sample_t` (packed 6B): gpio paddle state, local_key (iambic output), audio_level, flags (edge/silence/tx/rx markers), config_gen (also reused as silence tick count).
- Consumers: `stream_consumer_t` (basic), `hard_rt_consumer_t` (MUST keep up — FAULTs via `fault_set` on lag > max_lag or on overrun), `best_effort_consumer_t` (skips to `write_pos - 2` and counts drops, never FAULTs).
- `fault_state_t`: all-atomic active/code/data/count; `fault_set` stores code/data/count before setting `active` (release) last; `fault_clear` clears `active` (release) first. Codes: OVERRUN, LATENCY_EXCEEDED, PRODUCER_OVERRUN, HARDWARE.

Depends on: nothing (REQUIRES ""). Pure standard C.
Used by, in-tree: keyer_hal, keyer_logging, and main/rt_task + bg_task link against this component directly. keyer_iambic is **not** an in-tree dependent as of 2026-09-06: it is the git submodule `iu3qez/Esp32KeyerTest`, built and tested in its own repo. It does not `REQUIRES` keyer_core; instead it mirrors `sample.h` verbatim under `test_host/interface/sample.h` (past a five-line banner) as a frozen interface contract. CI in both repos checks that mirror stays byte-identical to `components/keyer_core/include/sample.h` — a change here that touches `sample.h` is a pin-bump-and-mirror-update in the submodule, not a build dependency.
External deps of note: none. Intentionally free of ESP-IDF so it builds and runs under the host Unity tests in test_host/.

Conventions:
- Built with -Wconversion -Wshadow -Wstrict-prototypes; keep it warning-clean.
- Buffer capacity MUST be a power of 2 and at least 2 (asserted in stream_init); `mask` gives fast modulo.
- Single-producer discipline is a calling convention, not enforced by the type: `stream_push`/`stream_push_raw`/`stream_flush` must only ever be called from the one RT producer task. Everything else (`stream_read`, lag/overrun queries, all consumer_*) is safe from any number of reader threads.
- Backing buffer is caller-provided (PSRAM on target); this module never allocates it.

Gotchas:
- This is the hard-RT data path (Core 0, 100µs ceiling). No malloc, no locks, no logging here — atomics only.
- Publish-after-store, not before: see `stream_write_slot`. If you're touching stream.c, re-read ARCHITECTURE.md rules 2.1.4 / 3.1.2 / 3.1.3 and the `#57` comment in stream.c before changing ordering — this exact bug (index published before the sample was fully written) shipped once already.
- `config_gen` is overloaded: normal samples carry a config generation, silence markers carry tick count (see sample_silence*). Don't conflate them.
- Hard-RT consumer overrun is not recoverable in place — it sets FAULT and stops; recovery is via explicit resync (`hard_rt_consumer_resync` jumps to current write position, not to the oldest slot). Plain `stream_consumer_t` recovery via `consumer_resync` instead lands `capacity - 1` behind, i.e. the oldest slot still guaranteed live — don't assume the two resync semantics are interchangeable.
- Silence compression means unchanged samples are NOT written — consumers must expand silence markers, and call stream_flush before shutdown to emit pending idle ticks.
<!-- END treecode (auto) -->
