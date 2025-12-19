# RustRemoteCWKeyer - Implementation TODO

**Status:** Phase 1 (Core Stream + RT Loop) - IN PROGRESS
**Last Updated:** 2025-01-19

---

## Completed ✅

### Design & Planning
- [x] Architecture document (ARCHITECTURE.md)
- [x] Core implementation design (docs/plans/2025-01-19-core-implementation-design.md)
- [x] Code style guidelines (.claude/CODE_STYLE.md)
- [x] Multi-platform support planning (ESP32-S3/P4)

### Configuration System
- [x] parameters.yaml schema (14 parameters, i18n en/it)
- [x] Python code generator (scripts/gen_config.py)
- [x] build.rs integration
- [x] Generated code: config.rs, config_meta.rs, config_nvs.rs
- [x] Config module integration in lib.rs

### Development Environment
- [x] Devcontainer with Rust + ESP-IDF
- [x] Rust toolchain (rustup, espup, cargo-espflash)
- [x] VSCode extensions configured

### Core Types
- [x] StreamSample implementation (src/sample.rs)
  - 6-byte structure with gpio, key, audio_level, flags, config_gen
  - GpioState with DIT/DAH paddle support
  - Complete unit tests (8 tests)

---

## Phase 1: Core Stream + RT Loop (IN PROGRESS)

### Priority 1: Lock-Free Stream (CRITICAL PATH)

**File:** `src/stream.rs`

Implement KeyingStream - the heart of the system (ARCHITECTURE.md Section 1).

**Requirements:**
- Lock-free SPMC ring buffer in PSRAM
- Single producer (RT thread) writes with atomic `write_idx`
- Multiple consumers (RT + best-effort) read with independent `read_idx`
- NO mutexes, NO locks (ARCHITECTURE.md Rule 3.1)
- Buffer size: 300,000 samples (30 seconds @ 10kHz)
- Silence compression (RLE) support

**API to implement:**
```rust
pub struct KeyingStream {
    buffer: &'static [UnsafeCell<StreamSample>],  // PSRAM allocation
    write_idx: AtomicUsize,                        // Producer index
    capacity: usize,                               // Buffer size
}

impl KeyingStream {
    /// Create stream with static PSRAM buffer
    pub fn new(buffer: &'static [UnsafeCell<StreamSample>]) -> Self;

    /// Push sample (producer only, called from RT thread)
    /// Returns false if buffer full (should trigger FAULT)
    pub fn push(&self, sample: StreamSample) -> bool;

    /// Get current write position (for consumers to check lag)
    pub fn write_position(&self) -> usize;
}

/// Consumer handle (each consumer has its own)
pub struct StreamConsumer {
    stream: &'static KeyingStream,
    read_idx: usize,  // Thread-local, no atomic needed
}

impl StreamConsumer {
    /// Create consumer at current stream position
    pub fn new(stream: &'static KeyingStream) -> Self;

    /// Consume next sample (non-blocking)
    /// Returns None if caught up with producer
    pub fn next(&mut self) -> Option<StreamSample>;

    /// Get lag behind producer (in samples)
    pub fn lag(&self) -> usize;

    /// Skip forward (for best-effort consumers that fall behind)
    pub fn skip_to_latest(&mut self);
}
```

**Safety considerations:**
- All `unsafe` blocks need `// SAFETY:` comments
- Ring buffer wrap logic must be proven correct
- Memory ordering: `Ordering::AcqRel` for producer, `Ordering::Acquire` for consumers

**Tests:**
- Single producer, single consumer
- Single producer, multiple consumers
- Buffer wrap-around
- Lag detection
- Full buffer behavior

---

### Priority 2: Iambic FSM

**File:** `src/iambic.rs`

Implement Iambic keyer finite state machine (Mode A and Mode B).

**Requirements:**
- Pure logic, no I/O (ARCHITECTURE.md Rule 2.1)
- Mode A: release priority
- Mode B: last contact priority
- Memory window support
- Weight/ratio support
- Takes: `GpioState` + `IambicConfig` → Returns: `bool` (key state)

**API:**
```rust
pub enum IambicMode {
    ModeA,
    ModeB,
}

pub struct IambicConfig {
    pub wpm: u16,
    pub mode: IambicMode,
    pub memory_window_us: u32,
    pub weight: u8,
}

impl IambicConfig {
    pub fn dit_duration_us(&self) -> u32;
    pub fn dah_duration_us(&self) -> u32;
}

pub struct IambicFsm {
    state: IambicState,
    timer_us: u32,
    // ... internal state
}

impl IambicFsm {
    pub fn new() -> Self;

    /// Tick FSM with current paddle state and config
    /// Called every tick (100µs @ 10kHz)
    /// Returns: true if key should be down
    pub fn tick(&mut self, paddles: GpioState, config: &IambicConfig) -> bool;
}
```

**Tests:**
- Dit only
- Dah only
- Alternating dit-dah (Mode A vs Mode B behavior)
- Squeeze (both paddles)
- Memory window behavior
- Timing accuracy

**Reference:** Use C++ prototype for golden test cases

---

### Priority 3: Audio Envelope

**File:** `src/audio_envelope.rs` (new)

Implement fade in/out to eliminate clicks.

**Requirements:**
- Target: 0 → 255 in configurable duration (4-10ms)
- At 10kHz: 40-100 ticks for fade
- Simple linear ramp (good enough)

**API:**
```rust
pub struct AudioEnvelope {
    target: u8,
    current: u8,
    fade_step: u8,
}

impl AudioEnvelope {
    pub fn new(fade_duration_ms: u8) -> Self;

    /// Tick envelope (called every 100µs)
    /// Returns current audio level (0-255)
    pub fn tick(&mut self) -> u8;

    /// Set target (key on = 255, key off = 0)
    pub fn set_key(&mut self, key: bool);
}
```

**Tests:**
- Fade in timing
- Fade out timing
- Multiple key transitions

---

### Priority 4: RT Producer-Consumer Task (Stub)

**File:** `src/main.rs` (update)

Create minimal RT task that ties everything together.

**Pseudo-code:**
```rust
fn main() {
    // Allocate stream buffer in PSRAM
    static STREAM_BUFFER: [UnsafeCell<StreamSample>; 300_000] = ...;
    let stream = KeyingStream::new(&STREAM_BUFFER);

    // Create RT task on Core 0
    let rt_handle = std::thread::Builder::new()
        .name("rt_producer".into())
        .spawn(|| rt_producer_consumer_task(&stream));

    // ... other setup
}

fn rt_producer_consumer_task(stream: &'static KeyingStream) {
    let mut config = CONFIG.load_snapshot();
    let mut iambic = IambicFsm::new();
    let mut envelope = AudioEnvelope::new(config.fade_duration_ms);

    loop {
        let start_us = esp_timer_get_time();

        // 1. Read GPIO (stub for now - hardcode idle)
        let paddles = GpioState::IDLE;

        // 2. Tick Iambic
        let key = iambic.tick(paddles, &config);

        // 3. Tick envelope
        envelope.set_key(key);
        let audio_level = envelope.tick();

        // 4. Push to stream
        let sample = StreamSample {
            gpio: paddles,
            key,
            audio_level,
            flags: 0,
            config_gen: CONFIG.generation.load(Ordering::Relaxed) as u16,
        };

        if !stream.push(sample) {
            // Buffer full - FAULT!
            panic!("Stream buffer overflow");
        }

        // 5. Spin-wait to next tick
        while esp_timer_get_time() - start_us < 100 {
            core::hint::spin_loop();
        }
    }
}
```

---

## Phase 1.5: RT-Safe Logging

**File:** `src/logging.rs` (update existing)

Implement non-blocking logging per ARCHITECTURE.md Rule 11.

**Components:**
- LogEntry structure
- LogStream (lock-free SPSC ring buffer)
- rt_log!() macro family
- Log consumer thread (Core 1)

**Defer until Phase 1 core works.**

---

## Phase 2: Audio + TX/RX State Machine

### Audio HAL
- I2S configuration for ESP32-S3
- Sidetone generation (sine wave)
- set_level() implementation

### TX/RX State Machine
- GPIO ISR one-shot wakeup
- PTT tail timeout
- Stream FLAG_TX_START / FLAG_RX_START markers

### Integration Tests
- Record GPIO from hardware → replay test

---

## Phase 3: Best-Effort Consumers

### Remote TX Consumer
- WiFi stub (placeholder)
- Edge detection (key down/up events)
- Timestamp forwarding

### Decoder Consumer
- Timing-based Morse decoder
- Dit/dah/space duration measurement
- Character recognition

### Diagnostics Consumer
- Stream statistics
- FAULT tracking
- Timeline recording

---

## Phase 4: WiFi Integration

- Remote CW protocol implementation (use provided C++ reference)
- µ-law audio codec
- Network discovery/pairing

---

## Phase 5: Polish

- Configuration persistence (NVS implementation in config_nvs.rs)
- CLI / Web UI / Display
- Performance tuning
- Documentation

---

## Known Issues / Tech Debt

- [ ] NVS persistence is stubbed out (config_nvs.rs)
- [ ] GPIO HAL not implemented (hal/gpio.rs is empty)
- [ ] Audio HAL not implemented (hal/audio.rs is empty)
- [ ] Remote key support deferred (future personality)

---

## Testing Strategy

### Unit Tests (Phase 1)
- [x] StreamSample (done)
- [ ] KeyingStream (ring buffer, wrap, lag)
- [ ] IambicFsm (Mode A/B, timing)
- [ ] AudioEnvelope (fade timing)

### Integration Tests (Phase 2+)
- [ ] Stream-based replay with recorded GPIO
- [ ] Golden reference comparison (C++ prototype output)
- [ ] Timing accuracy measurement

### Hardware Tests (Phase 2+)
- [ ] Actual latency measurement (GPIO → Audio)
- [ ] CPU utilization (Core 0 @ 10kHz)
- [ ] FAULT conditions (buffer overflow, lag)

---

## Documentation TODO

- [ ] Update README with build instructions
- [ ] Add examples/ directory with usage examples
- [ ] Document NVS schema for persistence
- [ ] Hardware wiring guide (GPIO pinout)

---

## Next Session Plan

1. **Implement `src/stream.rs`** (lock-free SPMC ring buffer)
   - Most critical component
   - Requires careful unsafe code with SAFETY comments
   - ~300-400 lines with tests

2. **Implement `src/iambic.rs`** (FSM logic)
   - Pure logic, easier than stream
   - Can use C++ prototype for test cases
   - ~200-300 lines with tests

3. **Implement `src/audio_envelope.rs`** (fade logic)
   - Simple linear ramp
   - ~100 lines with tests

4. **Wire up in `src/main.rs`** (RT task stub)
   - Integration point
   - ~150 lines

**Target:** Complete Phase 1 MVP (no hardware, but core logic proven)

---

## Commands Reference

```bash
# Run code generator
python3 scripts/gen_config.py

# Build (triggers codegen via build.rs)
cargo build --release --target xtensa-esp32s3-espidf

# Run tests (host)
cargo test

# Flash to device (when ready)
cargo espflash flash --release --monitor
```

---

**End of TODO**
