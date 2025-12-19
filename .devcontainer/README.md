# ESP32 Rust Development Container

This devcontainer provides a complete development environment for ESP32 projects using both **C/C++ (ESP-IDF)** and **Rust**.

## Features

### ESP-IDF (C/C++)
- ESP-IDF v5.5.1
- Python environment with `jsonschema`, `gcovr`
- VSCode ESP-IDF extension

### Rust Toolchain
- **rustup** with stable Rust
- **espup**: ESP Rust toolchain installer
- **Xtensa toolchain** for ESP32-S3 (xtensa-esp32s3-espidf)
- **RISC-V toolchain** for ESP32-P4 (riscv32imc-esp-espidf)
- **cargo-espflash**: Flash tool for ESP32
- **ldproxy**: Linker proxy for ESP32
- **rust-analyzer**: LSP for IDE support

### VSCode Extensions
- `rust-lang.rust-analyzer`: Rust language server
- `vadimcn.vscode-lldb`: Debugger
- `tamasfe.even-better-toml`: TOML file support
- ESP-IDF extensions (existing)

## Quick Start

### 1. Open in VSCode
```bash
# Open this folder in VSCode
code /path/to/RustRemoteCWKeyer

# VSCode will prompt to "Reopen in Container"
```

### 2. Build Rust Project
```bash
# Inside container terminal
source $HOME/export-esp.sh  # Load ESP Rust environment

# Build for ESP32-S3
cargo build --release --target xtensa-esp32s3-espidf

# Flash to device
cargo espflash flash --release --monitor
```

### 3. Available Targets
- `xtensa-esp32s3-espidf` (ESP32-S3, default in rust-analyzer)
- `riscv32imc-esp-espidf` (ESP32-P4, if supported by esp-rs)

## Environment Variables

The following are automatically configured in `.bashrc`:

```bash
source /opt/esp/idf/export.sh       # ESP-IDF environment
source $HOME/.cargo/env              # Rust/Cargo
source $HOME/export-esp.sh           # ESP Rust toolchains
```

## Verification

### Check Rust Installation
```bash
rustc --version
cargo --version
```

### Check ESP Toolchains
```bash
rustup toolchain list
# Should show: esp (custom toolchain)

rustup target list --installed
# Should show: xtensa-esp32s3-espidf, riscv32imc-esp-espidf, etc.
```

### Check Cargo Tools
```bash
cargo espflash --version
ldproxy --version
```

## USB Device Access

The container has `--privileged` access and mounts `/dev/ttyUSB0` by default.

If your ESP32 is on a different port, edit `devcontainer.json`:
```json
"runArgs": [
    "--privileged",
    "--device=/dev/ttyUSB1:/dev/ttyUSB1"  // Change here
],
```

## Troubleshooting

### Rust toolchain not found
```bash
# Re-run espup install
espup install
source $HOME/export-esp.sh
```

### cargo-espflash permission denied
```bash
# Ensure user is in dialout group (already done in Dockerfile)
groups
# Should show: vscode dialout ...
```

### rust-analyzer not working
```bash
# Reload VSCode window
Ctrl+Shift+P → "Developer: Reload Window"

# Check rust-analyzer output
Ctrl+Shift+P → "Output" → select "Rust Analyzer Language Server"
```

## Build Times

**First build:** 10-20 minutes (downloads dependencies, builds std for ESP32)
**Incremental builds:** 30 seconds - 2 minutes
**Container build:** 15-30 minutes (one-time setup)

## Resources

- [ESP-RS Book](https://esp-rs.github.io/book/)
- [espup Documentation](https://github.com/esp-rs/espup)
- [cargo-espflash](https://github.com/esp-rs/espflash)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/)
