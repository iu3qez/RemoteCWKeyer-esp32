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
source /home/sf/esp/esp-idf/export.sh

# Frontend deps (first time only)
cd components/keyer_webui/frontend && npm install && cd -

# Build
idf.py build

# Flash and monitor
idf.py flash monitor
```

## Code Generation

Configuration is generated from [parameters.yaml](parameters.yaml). **Never edit files in `keyer_config/` directly.**

```bash
python scripts/gen_config_c.py parameters.yaml components/keyer_config
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

## Documentation Lookup

When looking up API docs for ESP-IDF, FreeRTOS, or any library, **always use Context7 first** (`resolve-library-id` then `query-docs`) before falling back to web search.

---

## Session Memory

Persistent notes live in `.claude/` and are tracked in git:

- **[.claude/MEMORY.md](.claude/MEMORY.md)** — Memory index (loaded into every conversation)
- **[.claude/feature-status.md](.claude/feature-status.md)** — Feature checklist by priority
- **[.claude/code-quality.md](.claude/code-quality.md)** — Cleanup items, stubs, recent fixes

Update these files as features are completed or new issues are found. Keep MEMORY.md concise (under 200 lines).

---

## Detailed References

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
