# Migration Guide: Fixing ESP32-S3 Rust Build Issues

## Problem Summary

The current devcontainer setup has a conflict between two GCC toolchains:
1. ESP-IDF v5.5.1 toolchain (gcc v14.2) from `espressif/idf` base image
2. Rust ESP toolchain (gcc v15.2) installed via `espup`

This causes linking failures because `esp-idf-sys` expects libraries from one toolchain but finds another.

## Solution: Use Official esp-rs Template Approach

The official [esp-idf-template](https://github.com/esp-rs/esp-idf-template) uses a cleaner approach:
- Start from Debian base (not espressif/idf)
- Let `espup` install EVERYTHING (Rust toolchain + ESP-IDF)
- Use coordinated versions that are known to work together

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
   - [`.devcontainer/Dockerfile`](.devcontainer/Dockerfile) - New multi-target Dockerfile
   - [`.cargo/config.toml`](.cargo/config.toml) - New cargo config with ESP32-S3 and ESP32-P4 support

3. **Removed:**
   - `build.sh` - No longer needed, use `cargo build` directly

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

**Important:** The new setup uses ESP-IDF v5.3 instead of v5.5.

This is because:
- v5.3 is the latest stable version fully compatible with espup
- v5.5 support in espup may still have issues
- All toolchain versions are coordinated by espup

If you MUST use ESP-IDF v5.5, you'll need to:
1. Wait for espup to officially support it
2. Or manually configure everything (not recommended)

### .cargo/config.toml Changes

**Removed:**
- Manual linker path specification
- ESP_IDF_VERSION environment variable

**Why:** espup handles all this automatically through export-esp.sh

## Troubleshooting

### Build still fails with linking errors

1. Make sure you're using `cargo build` directly, not the old `build.sh` script
2. Verify `source ~/export-esp.sh` is in your shell
3. Check `echo $PATH` includes `~/.rustup/toolchains/esp/`

### Wrong ESP-IDF version

The installed version is determined by espup during container build.
To change it, modify the Dockerfile line:

```dockerfile
--esp-idf-version "v5.3" \
```

Then rebuild the container.

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
