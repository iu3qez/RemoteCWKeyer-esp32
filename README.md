# RustRemoteCWKeyer

Professional-grade CW (Morse code) keyer with remote operation capability, built in Rust for ESP32-S3/P4 platforms.

## Features

- **Iambic Keying**: Mode A/B with configurable memory and timing
- **Remote CW**: Network-transparent keying over WiFi
- **Lock-Free Architecture**: SPMC stream buffer, zero mutexes
- **Hard Real-Time**: Guaranteed sub-100µs latency on critical path
- **Fault-Safe**: Corrupted timing triggers FAULT, not bad CW

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for immutable design principles.

```
┌──────────┐     ┌─────────────────┐     ┌──────────────────┐
│PaddleHal │────▶│  KeyingStream   │────▶│ Audio/TX (RT)    │
│          │     │  (lock-free)    │────▶│ Remote (best-ef) │
│          │     │                 │────▶│ Decoder (best-ef)│
└──────────┘     └─────────────────┘     └──────────────────┘
```

## Hardware

- **Target**: ESP32-S3, ESP32-P4 (dual-core with PSRAM)
- **Paddle Input**: GPIO with internal pull-up
- **Audio Output**: I2S DAC for sidetone
- **TX Output**: GPIO for transmitter keying

## Building

```bash
# Install Rust ESP32 toolchain
cargo install espup
espup install
. $HOME/export-esp.sh

# Build
cargo build --release --target xtensa-esp32s3-espidf

# Flash
espflash flash --monitor target/xtensa-esp32s3-espidf/release/keyer
```

## Project Structure

```
RustRemoteCWKeyer/
├── ARCHITECTURE.md      # Immutable design principles
├── Cargo.toml
├── src/
│   ├── main.rs          # Entry point
│   ├── lib.rs           # Library root
│   ├── stream.rs        # KeyingStream (core)
│   ├── sample.rs        # StreamSample types
│   ├── consumer.rs      # HardRt + BestEffort consumers
│   ├── iambic.rs        # Iambic FSM (pure logic)
│   ├── logging.rs       # RT-safe logging
│   └── hal/
│       ├── mod.rs
│       ├── gpio.rs      # Paddle input
│       └── audio.rs     # I2S output
├── tests/
│   ├── captures/        # Recorded GPIO streams
│   └── golden/          # Expected output streams
└── README.md
```

## Testing

```bash
# Run tests on host (no hardware needed)
cargo test

# Run with specific test
cargo test test_iambic_mode_b
```

## License

Licensed under either of:
- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE))
- MIT license ([LICENSE-MIT](LICENSE-MIT))

at your option.

## Contributing

Contributions must comply with [ARCHITECTURE.md](ARCHITECTURE.md). Non-compliant PRs will not be merged.
