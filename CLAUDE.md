# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based CW (Morse code) keyer with a pure C implementation using ESP-IDF v5.x.

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
│   └── keyer_hal/              # GPIO, I2S, ES8311
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

## Detailed References

- **[ARCHITECTURE.md](ARCHITECTURE.md)** — Immutable design principles, stream semantics, fault model
- **[CODING_STYLE.md](CODING_STYLE.md)** — Compiler flags, PVS-Studio, C patterns, error handling
- **[parameters.yaml](parameters.yaml)** — Configuration source of truth
