# Migration Guide: Fixing ESP32-S3 Rust Build Issues

## Problem Summary

The current devcontainer setup has a conflict between two GCC toolchains:
1. ESP-IDF v5.5.1 toolchain (gcc v14.2) from `espressif/idf` base image
2. Rust ESP toolchain (gcc v15.2) installed via `espup`

This causes linking failures because `esp-idf-sys` expects libraries from one toolchain but finds another.

## Solution: Use Official esp-rs Template Approach

The official [esp-idf-template](https://github.com/esp-rs/esp-idf-template) uses a cleaner approach:
- Start from Debian base (not espressif/idf)
- Let `espup` install Rust toolchains (Xtensa and RISC-V)
- Let `esp-idf-sys` download and manage ESP-IDF during build time
- Use released crate versions compatible with ESP-IDF v5.3

## Multi-Target Support

The new configuration supports both:
- **ESP32-S3** (Xtensa architecture) - Default target
- **ESP32-P4** (RISC-V architecture)

Both toolchains are installed automatically by espup.

## Migration Steps

### ✅ COMPLETED

The migration has been applied automatically. The following changes were made:

1. **Backup created:**
   - `.devcontainer/Dockerfile.old` - Original Dockerfile
   - `.cargo/config.toml.old` - Original cargo config

2. **New configuration applied:**
   - [`.devcontainer/Dockerfile`](.devcontainer/Dockerfile) - Official esp-rs template Dockerfile
   - [`.devcontainer/devcontainer.json`](.devcontainer/devcontainer.json) - Official esp-rs devcontainer config
   - [`.cargo/config.toml`](.cargo/config.toml) - Cargo config with ESP32-S3 and ESP32-P4 support
   - [`Cargo.toml`](Cargo.toml) - Using esp-idf-svc 0.51.0 (compatible with ESP-IDF v5.3)

3. **Removed:**
   - `build.sh` - No longer needed, use `cargo build` directly
   - Git dependency patches - Using released versions instead

### 3. Update devcontainer.json (if needed)

The current devcontainer.json should work, but verify the user is set to `vscode`:

```json
{
  "build": {
    "dockerfile": "Dockerfile",
    "args": {
      "CONTAINER_USER": "vscode",
      "CONTAINER_GROUP": "vscode",
      "ESP_BOARD": "esp32s3"
    }
  }
}
```

### 4. Rebuild DevContainer

1. Close VSCode
2. Delete the devcontainer volume (optional but recommended for clean start)
3. Reopen in container: Command Palette → "Dev Containers: Rebuild Container"

### 5. Verify Installation

After the container rebuilds, run:

```bash
# Check Rust toolchain
rustup show

# Check ESP tools
espup --version
cargo-espflash --version

# Try building
cargo build
```

## Key Changes

### Dockerfile Changes

**Before:**
- Based on `espressif/idf:v5.5.1`
- Manually installed Rust + espup on top
- Two conflicting GCC toolchains

**After:**
- Based on `debian:bookworm-slim`
- Clean installation via espup
- Single coordinated toolchain

### ESP-IDF Version

**Important:** The new setup uses ESP-IDF v5.3.

How it works:
- `espup` installs only Rust toolchains (not ESP-IDF)
- ESP-IDF v5.3 is downloaded by `esp-idf-sys` during the first build
- Version is specified in [`.cargo/config.toml`](.cargo/config.toml) via `ESP_IDF_VERSION = { value = "v5.3", force = true }`
- Released crate versions (esp-idf-svc 0.51.0) are compatible with ESP-IDF v5.3

**Note:** `espup` does NOT support `--esp-idf-version` parameter. ESP-IDF must be managed by `esp-idf-sys` at build time.

### Cargo.toml Changes

**Before:**
```toml
esp-idf-svc = { git = "https://github.com/esp-rs/esp-idf-svc.git", features = ["binstart"] }

[patch.crates-io]
esp-idf-sys = { git = "https://github.com/esp-rs/esp-idf-sys.git" }
esp-idf-hal = { git = "https://github.com/esp-rs/esp-idf-hal.git" }
```

**After:**
```toml
esp-idf-svc = { version = "0.51.0", features = ["binstart"] }
# No patches needed - using released versions
```

**Why:** Git HEAD versions were incompatible with ESP-IDF v5.3. Version 0.51.0 is the stable release for ESP-IDF v5.3.

## Troubleshooting

### Build still fails with linking errors

1. Make sure you're using `cargo build` directly, not the old `build.sh` script
2. Run `cargo clean` to remove old build artifacts
3. Verify `source ~/export-esp.sh` is in your shell: `echo $PATH` should include `~/.rustup/toolchains/esp/`
4. Check ESP-IDF will be downloaded: First build will take longer as `esp-idf-sys` downloads ESP-IDF v5.3

### Wrong ESP-IDF version

ESP-IDF version is controlled by [`.cargo/config.toml`](.cargo/config.toml):

```toml
[env]
ESP_IDF_VERSION = { value = "v5.3", force = true }
```

To change version:
1. Edit the version in `.cargo/config.toml`
2. Verify your crate versions are compatible (check esp-idf-svc releases)
3. Run `cargo clean` and rebuild

### espup installation fails

This usually means GitHub rate limiting. Solutions:
1. Wait an hour and retry
2. Set GITHUB_TOKEN in devcontainer.json build args

## Build Commands

The old `build.sh` has been removed. Use standard cargo commands:

### For ESP32-S3 (Xtensa - Default)
```bash
# Build debug
cargo build

# Build release
cargo build --release

# Flash (requires hardware connected)
cargo espflash flash --monitor

# Flash release
cargo espflash flash --release --monitor
```

### For ESP32-P4 (RISC-V)
```bash
# Build debug
cargo build --target riscv32imafc-esp-espidf

# Build release
cargo build --release --target riscv32imafc-esp-espidf

# Flash (requires hardware connected)
cargo espflash flash --target riscv32imafc-esp-espidf --monitor
```

### Clean Build
```bash
cargo clean
```

## References

- [ESP-IDF Template](https://github.com/esp-rs/esp-idf-template)
- [Rust on ESP Book](https://esp-rs.github.io/book/)
- [espup Documentation](https://github.com/esp-rs/espup)
