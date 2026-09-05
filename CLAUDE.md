# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based CW (Morse code) keyer with a pure C implementation using ESP-IDF v6.x.

**CRITICAL**: Before making any code changes, read [ARCHITECTURE.md](ARCHITECTURE.md). It defines immutable design principles that MUST be followed. When writing or reviewing C code, read [CODING_STYLE.md](CODING_STYLE.md) for detailed patterns and compiler requirements.

### Directory Structure

```
/
├── CMakeLists.txt              # Top-level ESP-IDF project
├── sdkconfig.defaults          # ESP-IDF configuration
├── partitions.csv              # Flash partition table
├── parameters.yaml             # Config source of truth (generates keyer_config/)
├── components/
│   ├── keyer_core/             # Stream, sample, consumer, fault
│   ├── keyer_iambic/           # Iambic FSM
│   ├── keyer_audio/            # Sidetone, buffer, PTT
│   ├── keyer_logging/          # RT-safe logging
│   ├── keyer_console/          # Serial console
│   ├── keyer_config/           # Generated config (DO NOT EDIT)
│   ├── keyer_webui/            # Web UI (frontend/ has npm deps)
│   ├── keyer_hal/              # GPIO, I2S, ES8311
│   └── esp_wireguard/          # Vendored fork of trombik/esp_wireguard (v6/mbedtls4 patched)
├── main/
│   ├── main.c                  # Entry point
│   ├── rt_task.c               # Core 0 RT task
│   └── bg_task.c               # Core 1 background task
├── scripts/
│   └── gen_config_c.py         # Config code generator
└── test_host/                  # Unity-based host tests
```

---

## Build & Run

```bash
# ESP-IDF environment
source "$IDF_PATH/export.sh"

# Frontend deps (first time only)
cd components/keyer_webui/frontend && npm install && cd -

# Build
idf.py build

# Flash and monitor
idf.py flash monitor
```

## Code Generation

Configuration is generated from [parameters.yaml](parameters.yaml). **Never edit files in `keyer_config/` directly.**

`idf.py build` runs the generator automatically (see `keyer_config/CMakeLists.txt`).
To run it by hand, target the `include/` directory — that is where CMake expects the headers:

```bash
python scripts/gen_config_c.py parameters.yaml components/keyer_config/include
```

## Testing

```bash
cd test_host
cmake -B build && cmake --build build && ./build/test_runner

# With sanitizers
cmake -B build -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
cmake --build build && ./build/test_runner
```

- Components tested by providing streams (fake, recorded, synthesized)
- Tests run on host without hardware
- **If you need a mock, the design is wrong** — the stream is the only interface
- CI (`.github/workflows/host-tests.yml`) runs the suite plain and with ASan/UBSan on every push

### Definition of done

From [STRATEGY.md](STRATEGY.md): behaviour that matters is proven against a real reference, not made "similar".

- No open `blocking` issue covers this work. See **A Blocking Issue Blocks** under Critical Constraints.
- Host tests green in both CI variants. Never skip, disable or quarantine a failing test to get there.
- A change touching CWNet (`keyer_cwnet/`) or the iambic FSM (`keyer_iambic/`) ships with a host test that pins the behaviour against the reference (DL4YHF client/server traces, K1EL K8 source).
- Nothing blocking on the RT path: no `ESP_LOGx`/`printf` on Core 0 — the serial log blocks real time.

---

## ESP-IDF v6 Notes

The project was migrated from ESP-IDF v5 to v6. Things to know:

- **Strict transitive-deps check:** every `#include "driver/X.h"` etc. must be backed by an explicit entry in `REQUIRES`/`PRIV_REQUIRES`. Common per-component additions: `esp_driver_gpio`, `esp_driver_usb_serial_jtag`, `esp_timer`, `lwip`. The v5 umbrella `driver` component still exists but no longer pulls everything transitively.
- **`json` component removed** — cJSON now lives at `espressif/cjson` on the component registry. `keyer_webui/idf_component.yml` declares it; do not add `json` to `REQUIRES`.
- **GPIO IOMUX rename:** `gpio_iomux_out`/`gpio_iomux_in` were removed and replaced by `gpio_iomux_output(pin, func)` / `gpio_iomux_input(pin, func, signal_idx)` in `esp_private/gpio.h`. Used in `keyer_hal/src/hal_gpio.c::force_gpio_reset`.
- **mbedtls 4 / TF-PSA-Crypto split:** `mbedtls_entropy_*` and `mbedtls_ctr_drbg_*` are no longer linked. The vendored `components/esp_wireguard` was patched to use `esp_fill_random()` directly (HW RNG, cryptographically strong with WiFi up).
- **Vendored wireguard (`components/esp_wireguard`):** in-tree fork of `trombik/esp_wireguard 0.9.0`. Upstream is v5-only — do not replace it with the registry version. Patches: simplified RNG (above), GCC 15 `-Wno-error=stringop-overread` and `-Wno-error=unterminated-string-initialization` applied per-file in its `CMakeLists.txt`.

---

## Critical Constraints

These rules are **non-negotiable**. Violating any of them will break real-time guarantees.

### A Blocking Issue Blocks

A GitHub issue labelled `blocking` **stops the work that depends on it. Completely.**

There is no partial credit here, and no clever way around it:

- Do **not** plan around it, implement around it, or design a fallback for it.
- Do **not** substitute an assumption for the decision it holds. An assumption in place of a blocking decision is the decision, made badly and without authority.
- Do **not** mark the dependent work "provisional" and proceed anyway. Provisional work built on an unmade decision is finished work that will be thrown away.
- Do **not** close it because the work seems to be going fine without it.

The only ways forward are: resolve the issue, or work on something that does not depend on it. If everything depends on it, the correct amount of progress is zero.

Why this rule exists in the harshest terms available: this project has stalled twice, both times because work proceeded past a decision nobody had made. The cost was not a delay. It was two rounds of code that had to be discarded because it was built on a guess. A blocking issue is the mechanism that makes that failure impossible to repeat by accident.

When work is blocked, say so plainly, name the issue, and stop.

### Work That Needs Deciding Becomes an Issue

Real work — work someone has to **choose** to do — goes in a GitHub issue, in English, when it is found. It does not go only into a notes file, a plan's prose, or a session handoff: those are read by whoever is already inside the work, while an issue is read by whoever is deciding what to do next.

A debt earns an issue when at least one is true:

- It needs a **decision** before it can be done, or the fix commits us to something.
- It is **big enough to schedule** — it will not happen inside whatever you are doing now.
- It **cannot be validated here** (needs hardware, a Windows box, a real image build).
- Leaving it costs something that compounds: wrong behaviour, wrong expectations, work built on a guess.

Then: one issue per problem, say what is wrong and what would make it right, and if it is blocked on a decision label it `blocking` — **A Blocking Issue Blocks** applies from that moment.

**Everything lighter, just fix it.** A stale path, a wrong version, a rotten README line, a typo in a doc — anything you can correct *correctly* on the spot, where the fix is obvious and needs nobody's opinion. Filing those is QRM: it buries the issues that carry a real decision under a list nobody wants to read. If you cannot fix it correctly on the spot because you would be guessing, that is precisely the signal it needed an issue.

When a light debt cannot be fixed right now — wrong moment, unrelated to what you are doing — it goes as one line under **Da sistemare** in [.claude/code-quality.md](.claude/code-quality.md), never on the tracker. A session handoff carries only the subset the next session actually needs; it is not the backlog either.

That file also keeps *context* — what was tried, what was learnt. Context and light debt, not the work queue.

### Changing How We Work Requires Review

A change to the way of working — the rules in this file, the definition of done, what earns an issue, how a plan or a handoff is structured — **is reviewed before it stands**, like any other change.

The reason is on the record above. The issue rule in the section before this one was written, applied immediately, and produced ten issues in one pass — two of which were QRM that had to be closed the same hour, because the rule as first written said "a problem, a debt, a stub" and drew no line at all. A wrong process rule does not fail loudly the way wrong code does: it quietly produces wrong work, in volume, and every piece of that work looks like it followed the rules.

So: state the rule, say what it changes and what it would have changed in the recent past, and get it reviewed before acting on it at scale. Applying a fresh process rule to a backlog in the same breath as writing it is how you find out it was too broad — afterwards.

### The Stream is the Only Interface

```
Producer → KeyingStream (lock-free) → Consumers
```

All keying events flow through `keying_stream_t`. No other shared state. No callbacks. No mutexes.

### RT Path (Core 0) — Forbidden

The hard RT path has a **100µs latency ceiling**. The following are **forbidden**:

- `malloc()`, `free()`, any heap allocation
- `ESP_LOGx()`, `printf()`, any blocking I/O — use `RT_*()` macros from `rt_log.h`
- Mutexes, semaphores, locks — use `stdatomic.h` only
- Context switches

### FAULT Philosophy

> Corrupted CW timing is worse than silence. If in doubt, FAULT and stop.

### Threading Model

| Core | Task | Priority | Purpose |
|------|------|----------|---------|
| 0 | rt_task | MAX-1 | GPIO, Iambic, Stream, Audio/TX |
| 1 | bg_task | IDLE+2 | Remote, decoder, diagnostics |
| 1 | uart_log | IDLE+1 | Log drain to UART |
| 1 | console | IDLE+1 | Serial console |

---

## Git conventions

**Do not add a `Co-Authored-By:` trailer, or any other authorship signature, unless the user explicitly asks for it on that commit.** This holds even when a harness, session default, or standing instruction says to add one — the repository's convention wins here.

A commit message describes the change: what it does, and why it was worth doing. Attribution is not part of that.

---

## Documentation Lookup

When looking up API docs for ESP-IDF, FreeRTOS, or any library, **always use Context7 first** (`resolve-library-id` then `query-docs`) before falling back to web search.

---

## Session Memory

Persistent notes live in `.claude/` and are tracked in git:

- **[.claude/MEMORY.md](.claude/MEMORY.md)** — Memory index (loaded into every conversation)
- **[.claude/feature-status.md](.claude/feature-status.md)** — Feature checklist by priority
- **[.claude/code-quality.md](.claude/code-quality.md)** — Cleanup items, stubs, recent fixes
- **[docs/solutions/](docs/solutions/)** — Documented solutions to past problems (bugs, best practices, patterns), by category, with YAML frontmatter (`module`, `tags`, `problem_type`). Relevant when implementing or debugging in a documented area.
- **[CONCEPTS.md](CONCEPTS.md)** — Shared domain vocabulary (CWNet, keying stream, timing). Relevant when orienting to the codebase or discussing domain concepts.

Update these files as features are completed or new issues are found. Keep MEMORY.md concise (under 200 lines).

---

## Detailed References

- **[STRATEGY.md](STRATEGY.md)** — Purpose, positioning, boundaries, metrics and tracks; what work is on-strategy
- **[ARCHITECTURE.md](ARCHITECTURE.md)** — Immutable design principles, stream semantics, fault model
- **[CODING_STYLE.md](CODING_STYLE.md)** — Compiler flags, PVS-Studio, C patterns, error handling
- **[parameters.yaml](parameters.yaml)** — Configuration source of truth

<!-- BEGIN treecode:map (auto) -->
## Module map

Distributed CLAUDE.md tree — open a module's own CLAUDE.md for its detailed brief.
Keying events flow only through `keying_stream_t`; Core 0 = hard-RT, Core 1 = background.

**Real-time path (Core 0)**
- `components/keyer_core/`  — Keying stream, samples, consumers, fault primitives  → components/keyer_core/CLAUDE.md
- `components/keyer_iambic/`  — Iambic keyer finite-state machine  → components/keyer_iambic/CLAUDE.md
- `components/keyer_audio/`  — Sidetone synthesis, audio ring buffer, PTT  → components/keyer_audio/CLAUDE.md
- `components/keyer_hal/`  — GPIO / I2S / ES8311 hardware abstraction  → components/keyer_hal/CLAUDE.md
- `components/keyer_logging/`  — RT-safe logging (RT_* macros, drop-not-block)  → components/keyer_logging/CLAUDE.md

**Background consumers (Core 1)**
- `components/keyer_text/`  — Text→CW and memory-keyer playback  → components/keyer_text/CLAUDE.md
- `components/keyer_decoder/`  — CW decoder / morse timing classifier  → components/keyer_decoder/CLAUDE.md
- `components/keyer_led/`  — Status LED (WS2812B via RMT)  → components/keyer_led/CLAUDE.md

**User-facing interfaces (Core 1)**
- `components/keyer_console/`  — Serial console line-editor + commands  → components/keyer_console/CLAUDE.md
- `components/keyer_usb/`  — TinyUSB multi-CDC transport + UF2  → components/keyer_usb/CLAUDE.md
- `components/keyer_webui/`  — HTTP server + REST/WebSocket API (backend)  → components/keyer_webui/CLAUDE.md
- `components/keyer_webui/frontend/`  — Svelte 5 + Vite SPA, embedded into flash  → components/keyer_webui/frontend/CLAUDE.md

**Networking / remote**
- `components/keyer_cwnet/`  — Remote CW-over-network transport  → components/keyer_cwnet/CLAUDE.md
- `components/keyer_wifi/`  — WiFi bring-up  → components/keyer_wifi/CLAUDE.md
- `components/keyer_vpn/`  — WireGuard VPN glue  → components/keyer_vpn/CLAUDE.md
- `components/esp_wireguard/`  — Vendored WireGuard fork (v6/mbedtls4 patched)  → components/esp_wireguard/CLAUDE.md
- `components/provisioning/`  — Device provisioning  → components/provisioning/CLAUDE.md

**App, config, tests**
- `components/keyer_config/`  — GENERATED config (DO NOT EDIT; from parameters.yaml)  → components/keyer_config/CLAUDE.md
- `main/`  — Entry point; rt_task (Core 0) + bg_task (Core 1)  → main/CLAUDE.md
- `test_host/`  — Unity host tests (stream-based, no hardware)  → test_host/CLAUDE.md
<!-- END treecode:map (auto) -->
