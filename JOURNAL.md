# Development Journal

This document holds critical information about the project's development: architectural decisions, solved problems, and points that can cause breakage if changed.

---

## 2025-12-19: Build System Configuration - CRITICAL

### Solved Problem: Build Failure with ldproxy

**Symptom:**
```
error: linking with `ldproxy` failed: exit status: 101
thread 'main' panicked at 'Cannot locate argument '--ldproxy-linker <linker>''
```

**Root Cause:**
The custom `build.rs` file did not call `embuild::espidf::sysenv::output()`. This function is **ABSOLUTELY CRITICAL** because:

1. It emits the `cargo:rustc-link-arg` instructions needed to configure ldproxy
2. It configures the ESP-IDF environment (variables, paths, compiler)
3. It passes the GCC linker parameters to cargo

**⚠️ BREAKING POINT - DO NOT MODIFY:**

```rust
fn main() {
    // MUST be the FIRST call in build.rs!
    embuild::espidf::sysenv::output();

    // ... rest of the configuration generation code
}
```

**Why it is critical:**
- Without this call, cargo does not receive the `--ldproxy-linker` parameters
- ldproxy does not know which ESP-IDF GCC compiler to use
- Linking always fails, even if everything else is configured correctly

### ESP-IDF Build System Configuration

**Critical Versions:**
- `embuild = "0.33"` in `[build-dependencies]` (Cargo.toml)
- `ESP_IDF_VERSION = "v5.3.3"` in `.cargo/config.toml`
- `esp-idf-svc = "0.51.0"` compatible with ESP-IDF v5.3.x

**Why these versions:**
- embuild 0.33 is compatible with the latest precompiled ldproxy binary
- ESP-IDF v5.3.3 is stable and tested with esp-idf-svc 0.51.0
- Different versions can cause incompatibilities in the build system

### DevContainer Setup

**Critical Volume Mounts:**
```json
{
  "mounts": [
    "source=rust-remote-cw-keyer-embuild-cache,target=/workspaces/RemoteCWKeyerV3/.embuild,type=volume",
    "source=rust-remote-cw-keyer-target-cache,target=/workspaces/RemoteCWKeyerV3/target,type=volume"
  ]
}
```

**Why they matter:**
- `.embuild/` holds the downloaded ESP-IDF (~500MB)
- Without a volume mount, ESP-IDF is downloaded again on every container rebuild
- The first build after a container rebuild takes 10-15 minutes to download ESP-IDF
- Later builds are much faster thanks to the cache

### ldproxy Binary

**Source:** Precompiled binaries from the esp-rs/embuild GitHub releases

**⚠️ DO NOT install ldproxy via `cargo install`:**
- The versions on crates.io may not be compatible
- The precompiled binaries are tested with the official esp-rs Dockerfile
- If there are problems, reinstall from:
  ```bash
  curl -L "https://github.com/esp-rs/embuild/releases/latest/download/ldproxy-x86_64-unknown-linux-gnu.zip" -o /tmp/ldproxy.zip
  unzip -o /tmp/ldproxy.zip -d ~/.cargo/bin/
  chmod u+x ~/.cargo/bin/ldproxy
  ```

### Multi-Target Support (ESP32-S3 and ESP32-P4)

**Configuration:**
- Default target: `xtensa-esp32s3-espidf` (Xtensa)
- Alternative: `riscv32imafc-esp-espidf` (RISC-V for ESP32-P4)
- Build for ESP32-P4: `cargo build --target riscv32imafc-esp-espidf`

**Toolchain:**
- Default: `esp` (Xtensa, for ESP32-S3)
- RISC-V toolchain installed automatically by espup
- Both configured in the devcontainer with `ESP_BOARD=esp32s3,esp32p4`

---

## Architectural Decisions

### Build Script (build.rs)

**CRITICAL execution order:**
1. `embuild::espidf::sysenv::output()` - ALWAYS FIRST
2. Code generation from parameters.yaml (Python)
3. Cargo rerun-if-changed directives

**Rationale:**
The embuild output must be emitted BEFORE any other operation, to guarantee that all environment variables and configuration are available while the project compiles.

### DevContainer vs Host Build

**Choice:** DevContainer with Debian bookworm-slim + espup

**Why:**
- Avoids conflicts between GCC toolchains (problem with the espressif/idf images)
- Clean and reproducible installation
- Compatible with the official esp-rs template
- Multi-target support out of the box

**Rejected alternative:**
- Base image `espressif/idf:v5.5.1` caused conflicts between GCC 14.2 (ESP-IDF) and GCC 15.2 (Rust ESP)

---

## Known Problems and Solutions

### Problem: "GLIBC 2.39 not found"
**Cause:** Build artifacts from a previous container
**Solution:** `cargo clean` after rebuilding the container

### Problem: ESP-IDF not downloaded on the first build
**Cause:** `ESP_IDF_VERSION` variable not set or in the wrong format
**Solution:** Use the plain string format `"v5.3.3"` instead of `{ value = "v5.3", force = true }`

### Problem: Slow build (10-15 minutes) after a container rebuild
**Cause:** ESP-IDF is downloaded again (~500MB)
**Solution:** Volume mounts for `.embuild/` and `target/` (already configured)

---

## References

### Official Template
- Repository: https://github.com/esp-rs/esp-idf-template
- Used as the reference for the build system configuration

### Documentation
- ESP-RS Book: https://esp-rs.github.io/book/
- embuild: https://github.com/esp-rs/embuild
- esp-idf-svc releases: https://github.com/esp-rs/esp-idf-svc/releases

### Critical Configuration Files
1. `build.rs` - Build script with embuild setup
2. `Cargo.toml` - Dependencies and embuild versions
3. `.cargo/config.toml` - Target and ESP_IDF_VERSION
4. `.devcontainer/Dockerfile` - Toolchain installation
5. `.devcontainer/devcontainer.json` - Volume mounts

---

## Notes for the Future

### Before Modifying build.rs
- ⚠️ DO NOT remove `embuild::espidf::sysenv::output()`
- ⚠️ It MUST remain the first call in `fn main()`
- Always test with `cargo clean && cargo build` after changes

### Before Updating Dependencies
- Check compatibility between esp-idf-svc and the ESP-IDF version
- Check the embuild changelog for breaking changes
- Test a full build before committing

### Before Rebuilding the DevContainer
- The first build after a rebuild will take 10-15 minutes
- ESP-IDF will be downloaded again from scratch if the volume mounts do not work
- Check that the Docker volumes are preserved

---

## 2025-12-20: Console Commands Implementation

### Implemented

UART serial console for configuration and diagnostics:
- Command parser with tokenization
- History ring buffer (4 entries, 64 bytes each)
- Tab completion with cycling
- Line buffer with escape sequences (arrows, backspace, Ctrl+C/U)
- Commands: help, set, show, debug, save, reboot, factory-reset, flash, stats
- Parameter registry generated from parameters.yaml

### Known Limitation: Log Level Filtering

The `debug <level>` command is a placeholder. The custom logging system (`rt_log!` macro, `LogStream`) **does not yet support filtering by level**.

**To implement filtering:**
1. Add a global `AtomicU8` for the current log level in `log_globals.rs`
2. Change the `rt_log!`, `rt_info!`, etc. macros to check the level before pushing
3. Implement level parsing in `cmd_debug()`: none, error, warn, info, debug, trace

**Example of the change needed in `logging.rs`:**
```rust
// In log_globals.rs
pub static LOG_LEVEL: AtomicU8 = AtomicU8::new(LogLevel::Info as u8);

// In rt_log! macro
#[macro_export]
macro_rules! rt_log {
    ($level:expr, $stream:expr, $timestamp:expr, $($arg:tt)*) => {{
        if ($level as u8) <= $crate::log_globals::LOG_LEVEL.load(Ordering::Relaxed) {
            // ... push to stream
        }
    }};
}
```

This is a separate task because it requires changes to the core logging system.

---

## Checklist: Symptoms of a Broken Build System

- [ ] Error "Cannot locate argument '--ldproxy-linker'"
  → Check that build.rs calls embuild::espidf::sysenv::output()

- [ ] Very slow build (>15 min) even after the first build
  → Check the volume mounts in devcontainer.json

- [ ] "GLIBC not found" or strange linking errors
  → Run `cargo clean` after rebuilding the container

- [ ] ESP-IDF version mismatch
  → Check ESP_IDF_VERSION in .cargo/config.toml

- [ ] ldproxy panic even with a correct configuration
  → Reinstall ldproxy from the precompiled GitHub binaries

---

## 2025-12-21: Migration to Pure C (ESP-IDF)

### Motivation

The project was migrated from Rust (esp-rs) to pure C (native ESP-IDF) to:
- Remove the complexity of the Rust/ESP-IDF build system
- Simplify debugging and maintenance
- Use the ESP-IDF APIs directly, without wrappers

### Project Structure

```
RemoteCWKeyerV3/
├── CMakeLists.txt          # ESP-IDF project root
├── sdkconfig.defaults      # ESP-IDF configuration
├── partitions.csv          # 16MB flash layout
├── parameters.yaml         # Configuration parameters (source of truth)
├── components/
│   ├── keyer_core/         # Stream, sample, consumer, fault
│   ├── keyer_iambic/       # Iambic FSM (Mode A/B)
│   ├── keyer_audio/        # Sidetone, buffer, PTT
│   ├── keyer_logging/      # RT-safe logging
│   ├── keyer_console/      # Serial console
│   ├── keyer_config/       # Generated config (from parameters.yaml)
│   └── keyer_hal/          # GPIO, I2S, ES8311
├── main/
│   ├── main.c              # Entry point
│   ├── rt_task.c           # Core 0 RT task
│   └── bg_task.c           # Core 1 background task
└── scripts/
    └── gen_config_c.py     # C config generator from parameters.yaml
```

### Build Commands

```bash
# Build
idf.py build

# Flash and monitor
idf.py flash monitor

# Clean build
idf.py fullclean && idf.py build
```

### Code Generator

`scripts/gen_config_c.py` generates from `parameters.yaml`:
- `components/keyer_config/include/config.h` - Atomic struct with accessor macros
- `components/keyer_config/include/config_nvs.h` - NVS keys
- `components/keyer_config/include/config_meta.h` - GUI metadata
- `components/keyer_config/include/config_console.h` - Console registry
- `components/keyer_config/src/config.c` - Defaults initialization

**⚠️ DO NOT modify the generated files** - modify `parameters.yaml` instead.

### Architectural Principles Preserved

1. **Stream-only communication** - `keying_stream_t` SPMC lock-free
2. **Hard RT path** - Core 0, no malloc, no mutex, no blocking I/O
3. **FAULT semantics** - Corrupted timing → silence
4. **Atomic config** - C11 `stdatomic.h` for lock-free access
5. **RT-safe logging** - `RT_*()` non-blocking macros

### Differences from Rust

| Aspect | Rust | C |
|---------|------|---|
| Atomics | `std::sync::atomic` | C11 `stdatomic.h` |
| Logging | `rt_log!` macro | `RT_INFO()` macro |
| Config | `AtomicConfig` struct | `keyer_config_t` with atomics |
| Build | `cargo build` | `idf.py build` |
| ESP-IDF | v5.3.3 via embuild | v5.5.1 native |

### Format Specifiers on Xtensa

On ESP32 (Xtensa), `uint32_t` is `unsigned long`, so:
- Use `PRIu32` from `<inttypes.h>` instead of `%u`
- Example: `"count=%" PRIu32` instead of `"count=%u"`

### Critical Files

1. **parameters.yaml** - Source of truth for configuration
2. **scripts/gen_config_c.py** - Code generator
3. **CMakeLists.txt** (root) - Project entry point
4. **sdkconfig.defaults** - ESP-IDF configuration
5. **components/*/CMakeLists.txt** - Component build

### Tests

```bash
# Host tests (Unity)
cd  /test_host
cmake -B build && cmake --build build
./build/test_runner
```

### Old Rust Code

The original Rust code was moved to `old/` for reference.
