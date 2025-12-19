# Build Instructions - RustRemoteCWKeyer

This project supports two ESP32 targets:
- **ESP32-S3** (Xtensa architecture) - Default
- **ESP32-P4** (RISC-V architecture)

## Prerequisites

This project uses a DevContainer that includes all necessary tools:
- Rust with ESP toolchains (Xtensa and RISC-V)
- ESP-IDF v5.3
- espflash and cargo-espflash
- ldproxy

## Quick Start

### 1. Open in DevContainer

1. Install Docker and VSCode with Dev Containers extension
2. Open this project in VSCode
3. Command Palette → "Dev Containers: Reopen in Container"
4. Wait for container to build (first time takes ~10-15 minutes)

### 2. Build the Project

#### For ESP32-S3 (Default Target)

```bash
# Debug build
cargo build

# Release build
cargo build --release
```

#### For ESP32-P4

```bash
# Debug build
cargo build --target riscv32imafc-esp-espidf

# Release build
cargo build --release --target riscv32imafc-esp-espidf
```

### 3. Flash to Hardware

#### ESP32-S3

```bash
# Flash debug build
cargo espflash flash --monitor

# Flash release build
cargo espflash flash --release --monitor
```

#### ESP32-P4

```bash
# Flash debug build
cargo espflash flash --target riscv32imafc-esp-espidf --monitor

# Flash release build
cargo espflash flash --release --target riscv32imafc-esp-espidf --monitor
```

## Project Structure

```
.
├── src/
│   └── main.rs              # Main application code
├── scripts/
│   └── gen_config.py        # Parameter code generator
├── parameters.yaml          # Configuration parameters
├── .devcontainer/
│   ├── Dockerfile           # Multi-target ESP development environment
│   └── devcontainer.json    # VSCode DevContainer configuration
├── .cargo/
│   └── config.toml          # Cargo build configuration for both targets
└── build.rs                 # Build script (runs gen_config.py)
```

## Switching Between Targets

To change the default target, edit `.cargo/config.toml`:

```toml
[build]
# Change this to switch default target
target = "xtensa-esp32s3-espidf"  # For ESP32-S3
# target = "riscv32imafc-esp-espidf"  # For ESP32-P4
```

Or always specify the target explicitly:

```bash
cargo build --target <target-triple>
```

## Troubleshooting

### Build fails with linking errors

Make sure the ESP environment is activated:
```bash
source ~/export-esp.sh
```

This is done automatically in `.bashrc`, but if you're in a fresh shell, you may need to source it manually.

### Wrong toolchain selected

Check active toolchain:
```bash
rustup show
```

Should show either:
- `esp` (for Xtensa/ESP32-S3)
- `nightly` (for RISC-V/ESP32-P4, though esp should work too)

### espflash cannot find device

1. Make sure your board is connected via USB
2. Check device permissions: `ls -l /dev/ttyUSB* /dev/ttyACM*`
3. Add your user to dialout group: `sudo usermod -aG dialout $USER`
4. The DevContainer mounts `/run/udev` to handle device permissions

### Build is very slow

First build is always slow because:
1. It compiles the entire standard library (`build-std`)
2. It builds all ESP-IDF components
3. It downloads and compiles all dependencies

Subsequent builds are incremental and much faster.

Use `cargo build --release` for production builds (optimized but slower to compile).

## Environment Variables

The DevContainer sets these automatically:

- `MCU=esp32s3` - Target microcontroller
- ESP toolchain paths (via `export-esp.sh`)
- Rust toolchain paths

## Code Generation

The project uses `parameters.yaml` to generate configuration code.

The build script (`build.rs`) automatically runs `scripts/gen_config.py` before compilation.

To manually regenerate:
```bash
python3 scripts/gen_config.py
```

## Cleaning Build Artifacts

```bash
# Clean Rust build artifacts
cargo clean

# Full clean (including ESP-IDF components)
rm -rf target/
```

## Advanced: Manual Toolchain Installation

If you're not using the DevContainer, you need to:

1. Install Rust: `curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`
2. Install espup: Download from [esp-rs/espup releases](https://github.com/esp-rs/espup/releases)
3. Install ESP toolchains: `espup install --targets esp32s3,esp32p4`
4. Activate environment: `source ~/export-esp.sh`
5. Install flash tools: Download espflash and ldproxy from their respective repos

**Recommended:** Use the DevContainer instead - it handles all of this automatically.

## Additional Resources

- [Rust on ESP Book](https://esp-rs.github.io/book/)
- [esp-idf-template](https://github.com/esp-rs/esp-idf-template)
- [espup Documentation](https://github.com/esp-rs/espup)
- [espflash Documentation](https://github.com/esp-rs/espflash)
