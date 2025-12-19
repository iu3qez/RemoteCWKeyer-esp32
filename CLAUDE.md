# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

RustRemoteCWKeyer is a professional-grade CW (Morse code) keyer with remote operation capability, built in Rust for ESP32-S3/P4 platforms. The project implements hard real-time iambic keying with a lock-free architecture.

**CRITICAL**: Before making any code changes, read [ARCHITECTURE.md](ARCHITECTURE.md). It defines immutable design principles that MUST be followed. Non-compliant code will not be merged.

## Build System (ESP32)

This project uses the esp-rs ecosystem with a **critical build system configuration** that can break if modified incorrectly.

### Building

```bash
# Standard build for ESP32-S3 (default target)
cargo build

# Release build
cargo build --release

# Build for ESP32-P4 (RISC-V)
cargo build --target riscv32imafc-esp-espidf
```

### Flashing

```bash
# Flash and monitor (default target)
cargo run --release

# Flash specific target
espflash flash --monitor target/xtensa-esp32s3-espidf/release/keyer
```

### Testing

```bash
# Run all tests (no hardware needed - runs on host)
cargo test

# Run specific test
cargo test test_iambic_mode_b

# Run with output
cargo test -- --nocapture
```

### Clean Build

```bash
# Clean after DevContainer rebuild or major changes
cargo clean
cargo build
```

## Critical Build System Configuration

**⚠️ READ [JOURNAL.md](JOURNAL.md) BEFORE MODIFYING BUILD SYSTEM**

The build system has several components that can break compilation if changed:

1. **[build.rs](build.rs)** - MUST call `embuild::espidf::sysenv::output()` as FIRST line in `main()`
   - This emits critical cargo instructions for ldproxy linker configuration
   - Without it: `"Cannot locate argument '--ldproxy-linker <linker>'"` error
   - Also runs Python code generator for configuration

2. **[Cargo.toml](Cargo.toml)** - Version compatibility
   - `embuild = "0.33"` in `[build-dependencies]`
   - `esp-idf-svc = "0.51.0"` compatible with ESP-IDF v5.3.x
   - Do NOT change versions without checking compatibility

3. **[.cargo/config.toml](.cargo/config.toml)** - Target configuration
   - `ESP_IDF_VERSION = "v5.3.3"` - must be simple string format
   - Multi-target support for ESP32-S3 (Xtensa) and ESP32-P4 (RISC-V)

4. **DevContainer** - Volume mounts for caching
   - `.embuild/` volume: ~500MB ESP-IDF installation
   - `target/` volume: compiled artifacts
   - First build after container rebuild takes 10-15 minutes

## Code Generation System

Configuration parameters are NOT hardcoded - they're defined in [parameters.yaml](parameters.yaml) and code is auto-generated:

### Workflow

1. Edit [parameters.yaml](parameters.yaml) to add/modify configuration parameters
2. Build automatically runs [scripts/gen_config.py](scripts/gen_config.py)
3. Generated files appear in `src/generated/`:
   - `config.rs` - Atomic configuration struct with global `CONFIG` instance
   - `config_meta.rs` - GUI metadata (labels, descriptions, widgets)
   - `config_nvs.rs` - NVS persistence stubs

**NEVER edit generated files directly** - changes will be overwritten on next build.

### Adding a New Parameter

1. Add entry to [parameters.yaml](parameters.yaml) following the schema format
2. Run `cargo build` to regenerate code
3. Access in code via `CONFIG.your_parameter.load(Ordering::Relaxed)`

## Architecture Principles

From [ARCHITECTURE.md](ARCHITECTURE.md) - **these are immutable**:

### The Stream is Truth

All keying events flow through `KeyingStream` - the single source of truth. No other shared state exists.

```
Producer → KeyingStream → Consumers
           (lock-free)
```

- **Producers**: Push samples (GPIO polling, iambic FSM, remote CW receiver)
- **Consumers**: Read samples at their own pace (audio/TX, remote forwarder, decoder)
- **No communication** between components except through stream

### Hard Real-Time Path

```
GPIO Poll → Iambic FSM → Stream Push → Audio/TX Consumer
           (Core 0, highest priority, ZERO context switches)
```

**Latency ceiling: 100 microseconds** (absolute maximum, must minimize)

**Forbidden in RT path**:
- Heap allocation
- Blocking I/O
- Mutexes/locks
- `ESP_LOGx`, `println!`, `print!` (use `rt_log!()` macro instead)
- Context switches (producer and Hard RT consumer co-located in same task)

**Required**:
- Static allocation only
- Lock-free atomics for synchronization
- RT-safe logging via `rt_log!()` macro (non-blocking, ~100-200ns)
- FAULT on timing corruption (silence is better than wrong CW)

### Threading Model

- **Core 0**: RT task (GPIO, iambic, stream push, audio/TX consume)
- **Core 1**: Best-effort tasks (remote, decoder, diagnostics, WiFi/BT stack)

### FAULT Philosophy

> Corrupted CW timing is worse than silence. If in doubt, FAULT and stop.

Hard RT consumers have a maximum lag limit. Exceeding it triggers FAULT:
- Audio off, TX off immediately
- FAULT cause logged with timestamp
- Stream history preserved for diagnostics

## Key Components

### Core Stream System

- **[src/stream.rs](src/stream.rs)** - `KeyingStream` lock-free SPMC ring buffer
- **[src/sample.rs](src/sample.rs)** - `StreamSample` type (GPIO, local_key, remote_key, flags)
- **[src/consumer.rs](src/consumer.rs)** - `HardRtConsumer` and `BestEffortConsumer` traits
- **[src/fault.rs](src/fault.rs)** - `FaultState` for RT-safe fault tracking
- **[src/logging.rs](src/logging.rs)** - `LogStream` for RT-safe logging (`rt_log!()` macros)

### Keying Logic

- **[src/iambic.rs](src/iambic.rs)** - Iambic FSM (pure logic, no I/O or allocation)
- Implements Mode A and Mode B with configurable memory window

### Hardware Abstraction

- **[src/hal/gpio.rs](src/hal/gpio.rs)** - Paddle input
- **[src/hal/audio.rs](src/hal/audio.rs)** - I2S sidetone output

### Configuration

- **[src/config/mod.rs](src/config/mod.rs)** - Re-exports generated config
- **[src/generated/](src/generated/)** - Auto-generated from [parameters.yaml](parameters.yaml)

## Development Workflow

### Typical Change Flow

1. **Read [ARCHITECTURE.md](ARCHITECTURE.md)** to ensure compliance
2. Make changes following immutable principles
3. Run tests: `cargo test`
4. Build for target: `cargo build --release`
5. Flash and verify: `espflash flash --monitor`

### When Modifying Configuration

1. Edit [parameters.yaml](parameters.yaml)
2. Build regenerates code automatically
3. Test parameter access in relevant components

### When Adding a Consumer

1. Decide: Hard RT (MUST keep up, will FAULT) or Best Effort (skips if behind)
2. Implement consumer trait in [src/consumer.rs](src/consumer.rs)
3. Add to appropriate thread (Core 0 for Hard RT, Core 1 for Best Effort)
4. Write stream-based tests (see Testing Principles below)

### Testing Principles

From [ARCHITECTURE.md](ARCHITECTURE.md) Section 7:

- Components are tested by providing streams (fake, recorded, or synthesized)
- No mocking needed (the stream is the only interface)
- Tests run on host without hardware
- Recorded hardware captures are test fixtures in `tests/captures/`
- Expected outputs are golden files in `tests/golden/`

**If you need a mock, the design is wrong.**

## Common Pitfalls

1. **Don't bypass the stream** - all communication goes through `KeyingStream`
2. **Don't add mutexes/locks** - use atomic operations only
3. **Don't use `ESP_LOGx` in RT path** - use `rt_log!()` macro instead
4. **Don't allocate in RT path** - static allocation only
5. **Don't modify build.rs without reading [JOURNAL.md](JOURNAL.md)** - critical ordering requirements
6. **Don't edit generated files** - edit [parameters.yaml](parameters.yaml) instead
7. **Don't add callbacks** - produces and consumers are isolated
8. **Don't share state** - only the stream is shared

## Target Platforms

- **Primary**: ESP32-S3 (Xtensa architecture) - default target
- **Secondary**: ESP32-P4 (RISC-V architecture) - use `--target riscv32imafc-esp-espidf`
- **Testing**: Host (x86/ARM) - core logic compiles and tests on host

## External Documentation

- **ESP-RS Book**: https://esp-rs.github.io/book/
- **embuild**: https://github.com/esp-rs/embuild
- **esp-idf-svc**: https://github.com/esp-rs/esp-idf-svc
- **ESP-IDF**: https://docs.espressif.com/projects/esp-idf/

## Commit Guidelines

When committing changes that touch the build system or critical configuration:
- Reference [JOURNAL.md](JOURNAL.md) in commit message if modifying build system
- Explain why changes comply with [ARCHITECTURE.md](ARCHITECTURE.md)
- Include test results for timing-critical changes

## Code Review Checklist

Every change must verify (from [ARCHITECTURE.md](ARCHITECTURE.md)):

1. Does this change communicate through the stream only?
2. Does this change avoid forbidden patterns?
3. Does this change maintain Hard RT latency budget?
4. Does this change have stream-based tests?
5. Does this change preserve FAULT semantics?
6. Does this change use only `rt_log!()` in RT path (no blocking logs)?

Non-compliant code shall not be merged.
