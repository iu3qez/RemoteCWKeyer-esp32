# RustRemoteCWKeyer - Core Implementation Design

**Date:** 2025-01-19
**Status:** Approved
**Authors:** Brainstorming session consolidation

---

## Executive Summary

This document consolidates the architectural design decisions for the RustRemoteCWKeyer core implementation. It covers the threading model, configuration system, audio architecture, state machine, and testing strategy that will guide the initial Rust implementation.

**Key Design Principles:**
- Lock-free SPMC architecture (ARCHITECTURE.md foundation)
- Minimum latency design with <100µs ceiling
- Zero context switches on RT path
- TX/RX state machine with PTT tail timeout
- Stream-based testing with recorded GPIO captures

---

## 1. Architecture Amendments

### 1.1 Latency Principle (Amendment 001)

**Change:** ARCHITECTURE.md Rule 4.3.2 updated from:
```
Total latency budget for Hard RT path: 100 microseconds
```

To:
```
Hard RT path shall achieve minimum possible latency,
with an absolute ceiling of 100 microseconds
```

**Rationale:**
- 100µs is an **absolute ceiling**, not a performance target
- Design must **minimize** latency, not just "stay under budget"
- Emphasizes aggressive optimization of critical path

**Impact:**
- Drives design choices: inline processing, zero-copy, tight loops
- Requires continuous profiling and measurement
- README.md updated for consistency

---

### 1.2 Zero Context Switch Mandate (Amendment 002)

**New Rule:** ARCHITECTURE.md Rule 4.3.6:
```
The entire Hard RT path (GPIO read → Iambic tick → Stream push → Audio/TX output)
MUST execute in a single thread with zero context switches.
Producer and Hard RT consumer are co-located in the same task.
```

**Rationale:**
- ESP32 context switch: 5-20µs typical, unpredictable
- At 10kHz tick rate, cumulative effect would violate latency ceiling
- Co-locating Producer and Audio/TX consumer eliminates this entirely
- GPIO→Audio/TX path becomes deterministic single-thread execution

**Impact:**
- Threading model simplified
- Producer and Audio/TX consumer run in same RT task on Core 0
- Reinforces minimum latency principle (Amendment 001)

---

## 2. Threading Model

### 2.1 Overview

```
┌────────────────────────────┐  ┌────────────────────────────┐
│         CORE 0             │  │         CORE 1             │
│                            │  │                            │
│  RT Task (priority MAX)    │  │  Best-Effort Tasks         │
│  ┌──────────────────────┐  │  │  ┌──────────────────────┐  │
│  │ Producer Loop        │  │  │  │ Remote TX Consumer   │  │
│  │ • GPIO poll          │  │  │  │   Priority: HIGH     │  │
│  │ • Iambic FSM tick    │  │  │  └──────────────────────┘  │
│  │ • Config check       │  │  │                            │
│  │ • Stream push        │  │  │  ┌──────────────────────┐  │
│  │                      │  │  │  │ Decoder Consumer     │  │
│  │ Audio/TX Consumer    │  │  │  │   Priority: MEDIUM   │  │
│  │ • Stream consume     │  │  │  └──────────────────────┘  │
│  │ • Audio output       │  │  │                            │
│  │ • TX GPIO            │  │  │  ┌──────────────────────┐  │
│  │                      │  │  │  │ Diagnostics Consumer │  │
│  │ (zero ctx switch)    │  │  │  │   Priority: LOW      │  │
│  └──────────────────────┘  │  │  └──────────────────────┘  │
│                            │  │                            │
│  Tick rate: 10 kHz         │  │  WiFi/BT Stack (ESP-IDF)   │
│  Stack: 8KB                │  │                            │
└────────────────────────────┘  └────────────────────────────┘
```

### 2.2 Core 0: RT Producer-Consumer Task

**Thread characteristics:**
- Priority: `configMAX_PRIORITIES - 1` (maximum)
- Core affinity: Pinned to Core 0
- Stack size: 8KB
- Tick rate: **10 kHz target** (100µs period), adjustable down if needed

**Loop structure:**
```rust
fn rt_producer_consumer_task(stream: &'static KeyingStream) {
    const TICK_RATE_HZ: u32 = 10_000;
    const TICK_PERIOD_US: u64 = 100;

    let mut local_config = CONFIG.load_snapshot();
    let mut config_gen = CONFIG.generation.load(Ordering::Acquire);
    let mut iambic = IambicFsm::new();
    let mut envelope = AudioEnvelope::new();

    loop {
        let start_us = esp_timer_get_time();

        // 1. Read GPIO paddle state (inline, no function call)
        let paddles = hal::gpio::read_paddles();

        // 2. Check config changes (only when idle - preserves timing)
        let mut flags = 0u8;
        if paddles.is_idle() {
            let new_gen = CONFIG.generation.load(Ordering::Acquire);
            if new_gen != config_gen {
                local_config = CONFIG.load_snapshot();
                config_gen = new_gen;
                flags |= FLAG_CONFIG_CHANGE;
            }
        }

        // 3. Tick Iambic FSM (pure logic, no I/O)
        let key = iambic.tick(paddles, &local_config);

        // 4. Tick audio envelope (fade in/out)
        envelope.set_key(key);
        let audio_level = envelope.tick();

        // 5. Push to stream (lock-free atomic)
        stream.push(StreamSample {
            gpio: paddles,
            key,
            audio_level,
            flags,
            config_gen,
        });

        // 6. Audio/TX consume (INLINE - zero context switch)
        if let Some(sample) = stream.consume_rt() {
            hal::audio::set_level(sample.audio_level);
            hal::gpio::set_tx(sample.key);

            if sample.flags & FLAG_CONFIG_CHANGE != 0 {
                let freq = CONFIG.sidetone_freq_hz.load(Ordering::Relaxed);
                let vol = CONFIG.sidetone_volume.load(Ordering::Relaxed);
                hal::audio::set_sidetone(freq, vol);
            }
        }

        // 7. Spin-wait to next tick
        while esp_timer_get_time() - start_us < TICK_PERIOD_US {
            core::hint::spin_loop();
        }
    }
}
```

**Key characteristics:**
- **No function calls** on hot path (GPIO read, envelope tick inlined)
- **No allocations** anywhere in loop
- **No blocking operations** (spin-wait only)
- **No logging** (use `rt_log!()` macro if absolutely necessary)

---

### 2.3 Core 1: Best-Effort Consumers

#### Thread 2: Remote TX Consumer
- **Priority:** `tskIDLE_PRIORITY + 3` (HIGH)
- **Stack:** 8KB
- **Purpose:** Forward local keying events to WiFi
- **Delay:** `vTaskDelay(pdMS_TO_TICKS(5))` (5ms acceptable latency)

```rust
fn remote_tx_consumer_task(stream: &'static KeyingStream) {
    let mut read_idx = 0;
    let mut last_key_state = false;

    loop {
        if let Some(sample) = stream.consume_best_effort(&mut read_idx) {
            if sample.key != last_key_state {
                let timestamp_us = get_timestamp();
                if sample.key {
                    remote::send_key_down(timestamp_us);
                } else {
                    remote::send_key_up(timestamp_us);
                }
                last_key_state = sample.key;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
```

#### Thread 3: Decoder Consumer
- **Priority:** `tskIDLE_PRIORITY + 2` (MEDIUM)
- **Stack:** 8KB
- **Purpose:** Decode CW timing to text (TX only for now)
- **Delay:** `vTaskDelay(pdMS_TO_TICKS(10))` (10ms OK for decoder)

```rust
fn decoder_consumer_task(stream: &'static KeyingStream) {
    let mut read_idx = 0;
    let mut decoder = MorseDecoder::new();

    loop {
        if let Some(sample) = stream.consume_best_effort(&mut read_idx) {
            if let Some(char) = decoder.process(sample.key) {
                display::append_char(char);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

#### Thread 4: Diagnostics Consumer
- **Priority:** `tskIDLE_PRIORITY + 1` (LOW)
- **Stack:** 4KB
- **Purpose:** Stream statistics, FAULT tracking, timeline recording
- **Delay:** `vTaskDelay(pdMS_TO_TICKS(100))` (100ms low priority)

```rust
fn diagnostics_consumer_task(stream: &'static KeyingStream) {
    let mut read_idx = 0;
    let mut stats = DiagStats::new();

    loop {
        if let Some(sample) = stream.consume_best_effort(&mut read_idx) {
            stats.record(sample);

            // Periodic reporting
            if stats.should_report() {
                stats.log_summary();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

---

## 3. Configuration System

### 3.1 Design Philosophy

**Problem:** How to change runtime parameters (WPM, mode, etc.) without:
- Mutex locks (forbidden)
- Callbacks (forbidden)
- Corrupting CW timing (unacceptable)

**Solution:** Atomic configuration + idle-time application + stream propagation

### 3.2 Global Configuration Structure

```rust
// src/config.rs

use core::sync::atomic::{AtomicU16, AtomicU8, AtomicU32, Ordering};

/// Global keyer configuration (atomically readable)
///
/// All parameters can be updated from any thread (UI, CLI, WiFi).
/// RT thread loads a complete snapshot when paddles are idle.
pub struct KeyerConfig {
    // Iambic parameters
    pub wpm: AtomicU16,              // 5-60 WPM
    pub mode: AtomicU8,              // 0=ModeA, 1=ModeB
    pub memory_window_us: AtomicU32, // Memory window (0-1000µs typical)
    pub weight: AtomicU8,            // 33-67 (50 = 1:3 dit:dah ratio)

    // Audio parameters
    pub sidetone_freq_hz: AtomicU16, // 400-1200 Hz
    pub sidetone_volume: AtomicU8,   // 0-100%
    pub fade_duration_ms: AtomicU8,  // 4-10ms (reboot/soft-restart to apply)

    // PTT parameters
    pub ptt_tail_ms: AtomicU32,      // 50-500ms typical

    // Generation counter (change detection)
    pub generation: AtomicU16,
}

impl KeyerConfig {
    pub const fn new() -> Self {
        Self {
            wpm: AtomicU16::new(20),
            mode: AtomicU8::new(0), // Mode A default
            memory_window_us: AtomicU32::new(500),
            weight: AtomicU8::new(50),
            sidetone_freq_hz: AtomicU16::new(700),
            sidetone_volume: AtomicU8::new(80),
            fade_duration_ms: AtomicU8::new(5),
            ptt_tail_ms: AtomicU32::new(100),
            generation: AtomicU16::new(0),
        }
    }

    /// Load complete snapshot for RT thread
    /// Called only when paddles idle to avoid corrupting timing
    pub fn load_snapshot(&self) -> IambicConfig {
        IambicConfig {
            wpm: self.wpm.load(Ordering::Relaxed),
            mode: match self.mode.load(Ordering::Relaxed) {
                0 => IambicMode::ModeA,
                _ => IambicMode::ModeB,
            },
            memory_window_us: self.memory_window_us.load(Ordering::Relaxed),
            weight: self.weight.load(Ordering::Relaxed),
        }
    }

    /// Atomically update WPM and bump generation
    pub fn set_wpm(&self, wpm: u16) {
        self.wpm.store(wpm, Ordering::Relaxed);
        self.generation.fetch_add(1, Ordering::Release);
    }

    // Similar methods for other parameters...
}

/// Static global config instance
pub static CONFIG: KeyerConfig = KeyerConfig::new();

/// Local snapshot for RT thread (no atomics in hot path)
#[derive(Clone, Copy)]
pub struct IambicConfig {
    pub wpm: u16,
    pub mode: IambicMode,
    pub memory_window_us: u32,
    pub weight: u8,
}

impl IambicConfig {
    /// Calculate dit duration from WPM
    /// PARIS timing: 1 word = 50 dits, WPM = words/min
    pub fn dit_duration_us(&self) -> u32 {
        1_200_000 / (self.wpm as u32)
    }

    pub fn dah_duration_us(&self) -> u32 {
        self.dit_duration_us() * 3
    }
}
```

### 3.3 Configuration Change Flow

```
┌─────────────┐
│ UI/CLI/WiFi │  set_wpm(25)
└──────┬──────┘
       │
       ▼
   CONFIG.wpm.store(25)
   CONFIG.generation++  ◄── Atomic increment signals change
       │
       │
       ▼
   RT Thread (polling @ 10kHz)
   ┌─────────────────────────────┐
   │ if paddles.is_idle() {      │  ◄── Only when safe (idle)
   │   if gen != cached_gen {    │
   │     load_snapshot()         │  ◄── Reload all config
   │     flags |= CONFIG_CHANGE  │  ◄── Mark in stream
   │   }                         │
   │ }                           │
   └─────────────────────────────┘
       │
       ▼
   StreamSample { config_gen: new, flags: CONFIG_CHANGE }
       │
       ▼
   Consumers see flag, reload their config (sidetone freq, etc.)
```

**Key properties:**
- ✅ No mutex
- ✅ No callbacks
- ✅ Timing never corrupted (changes only when idle)
- ✅ All consumers notified via stream
- ✅ Config changes visible in stream (testable, replay-able)

---

## 4. Audio Architecture

### 4.1 TX/RX Mode Separation

**Hard rule:** TX and RX audio are **mutually exclusive** (half-duplex).

```
┌─────────────────────────────────────────┐
│         Audio HAL (I2S output)          │
│                                         │
│  if TX_MODE {                           │
│    I2S ← Sidetone (from KeyingStream)   │
│  } else {                               │
│    I2S ← Remote Audio (from WiFi)       │
│  }                                      │
│                                         │
│  NO MIXING                              │
└─────────────────────────────────────────┘
```

### 4.2 Sidetone with Envelope (TX Mode)

**Problem:** Instant on/off creates clicks (discontinuity in waveform)

**Solution:** 8-bit audio level with fade envelope

```rust
/// Audio envelope state machine
/// Generates smooth fade in/out to eliminate clicks
struct AudioEnvelope {
    target: u8,      // Target level (0 or 255)
    current: u8,     // Current level
    fade_step: u8,   // Increment per tick
}

impl AudioEnvelope {
    /// Create envelope with configurable fade duration
    /// fade_ms: 4-10ms typical (configured at boot)
    fn new(fade_ms: u8) -> Self {
        // fade_ms @ 10kHz = fade_ms * 10 ticks
        // 0→255 in N ticks = 255/N per tick
        let fade_ticks = fade_ms as u32 * 10;
        let fade_step = (255 / fade_ticks) as u8;

        Self {
            target: 0,
            current: 0,
            fade_step,
        }
    }

    /// Tick envelope towards target (called every 100µs)
    fn tick(&mut self) -> u8 {
        if self.current < self.target {
            // Fade in
            self.current = self.current.saturating_add(self.fade_step);
            if self.current > self.target {
                self.current = self.target;
            }
        } else if self.current > self.target {
            // Fade out
            self.current = self.current.saturating_sub(self.fade_step);
            if self.current < self.target {
                self.current = self.target;
            }
        }
        self.current
    }

    fn set_key(&mut self, key: bool) {
        self.target = if key { 255 } else { 0 };
    }
}
```

**Example timing (5ms fade @ 10kHz):**
```
T0:     key=true, target=255, current=0, fade_step=5
T0.1ms: current=5
T0.2ms: current=10
...
T5.0ms: current=255 (full volume)

T10.0ms: key=false, target=0
T10.1ms: current=250
...
T15.0ms: current=0 (silent)
```

### 4.3 Remote Audio (RX Mode)

**Codec:** µ-law (now), Opus (future)

**Pipeline:**
```
WiFi RX packets
  ↓
Codec decode (µ-law → PCM)
  ↓
PCM buffer (ring buffer)
  ↓
Audio HAL reads buffer
  ↓
I2S DAC output
```

**Future consideration:** Opus decode is CPU-intensive, requires Core 0 free → KeyingStream must stop in RX mode.

### 4.4 TX/RX Switch Logic

**Switch controlled by:** PTT tail timeout (not audio fade)

```rust
static TX_ACTIVE: AtomicBool = AtomicBool::new(false);
static LAST_TX_EVENT_US: AtomicI64 = AtomicI64::new(0);

// In RT loop:
if key || !paddles.is_idle() {
    LAST_TX_EVENT_US.store(now_us, Ordering::Relaxed);
}

let ptt_tail_us = CONFIG.ptt_tail_ms.load(Ordering::Relaxed) as i64 * 1000;
if (now_us - LAST_TX_EVENT_US.load(Ordering::Relaxed)) > ptt_tail_us {
    // PTT drop
    TX_ACTIVE.store(false, Ordering::Release);
    // Stream push FLAG_RX_START
    // Exit TX loop, return to RX idle
}
```

**Audio HAL switch:**
```rust
fn audio_output_task() {
    loop {
        if TX_ACTIVE.load(Ordering::Acquire) {
            // TX mode: sidetone from stream
            if let Some(sample) = stream.consume_audio() {
                output_sidetone(sample.audio_level);
            }
        } else {
            // RX mode: PCM from WiFi
            if let Some(pcm) = wifi_pcm_buffer.read() {
                output_pcm(pcm);
            }
        }
    }
}
```

---

## 5. StreamSample Structure

### 5.1 Final Definition

```rust
//! Stream sample structure
//!
//! Represents a single time-slice (tick) of keyer state.
//! All samples are time-aligned at TICK_RATE_HZ (typically 10kHz).
//!
//! Size: 6 bytes (compact for PSRAM efficiency)

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct StreamSample {
    /// GPIO paddle state (raw hardware input)
    /// Bit 0: DIT paddle, Bit 1: DAH paddle
    pub gpio: GpioState,

    /// Keying output (post-Iambic processing)
    /// true = key down, false = key up
    pub key: bool,

    /// Audio sidetone level with envelope applied
    /// 0 = silent, 255 = full volume
    /// Contains fade in/out to eliminate clicks
    pub audio_level: u8,

    /// Sample flags (edges, config changes, state transitions)
    pub flags: u8,

    /// Configuration generation number
    /// Incremented when config changes, allows consumer detection
    pub config_gen: u16,
}

/// GPIO paddle state
#[repr(transparent)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GpioState(u8);

impl GpioState {
    pub const DIT: u8 = 0x01;
    pub const DAH: u8 = 0x02;

    pub fn dit(&self) -> bool { self.0 & Self::DIT != 0 }
    pub fn dah(&self) -> bool { self.0 & Self::DAH != 0 }
    pub fn is_idle(&self) -> bool { self.0 == 0 }
    pub fn both(&self) -> bool { self.0 == (Self::DIT | Self::DAH) }
}
```

### 5.2 Flag Definitions

```rust
// Sample flag bits
pub const FLAG_GPIO_EDGE: u8 = 0x01;      // GPIO state changed this tick
pub const FLAG_CONFIG_CHANGE: u8 = 0x02;  // Config updated (reload params)
pub const FLAG_TX_START: u8 = 0x04;       // TX mode started (ISR wakeup)
pub const FLAG_RX_START: u8 = 0x08;       // RX mode started (PTT tail)
pub const FLAG_SILENCE: u8 = 0x10;        // RLE silence marker (compression)
```

### 5.3 Size and Memory Considerations

**Sample size:** 6 bytes per sample

**Stream capacity calculation:**
```
Target: 30 seconds @ 10 kHz worst-case (no compression)
Samples: 30s * 10,000 Hz = 300,000 samples
Memory: 300,000 * 6 bytes = 1.8 MB

With silence compression (typical 90% idle time):
Effective: ~180 KB typical usage
```

**PSRAM allocation:** Stream buffer allocated in PSRAM (ESP32-S3/P4), not internal SRAM.

---

## 6. TX/RX State Machine

### 6.1 State Diagram

```
     ┌───────────────────────────────────────┐
     │                                       │
     │                RX                     │
     │   ┌─────────────────────────────┐     │
     │   │ • RT thread sleeping        │     │
     │   │ • GPIO ISR enabled one-shot │     │
     │   │ • Audio: WiFi PCM           │     │
     │   │ • KeyingStream frozen       │     │
     │   └─────────────────────────────┘     │
     │                                       │
     └───────┬───────────────────────────────┘
             │
             │ Paddle press (GPIO ISR)
             ▼
     ┌───────────────────────────────────────┐
     │              TX START                 │
     │   ┌─────────────────────────────┐     │
     │   │ • Read GPIO (first touch)   │     │
     │   │ • Disable ISR (ignore bounce)│    │
     │   │ • Push FLAG_TX_START        │     │
     │   │ • Switch audio to sidetone  │     │
     │   └─────────────────────────────┘     │
     └───────┬───────────────────────────────┘
             │
             ▼
     ┌───────────────────────────────────────┐
     │                                       │
     │                TX                     │
     │   ┌─────────────────────────────┐     │
     │   │ • RT thread @ 10kHz polling │     │
     │   │ • GPIO poll, Iambic FSM     │     │
     │   │ • Audio: sidetone           │     │
     │   │ • KeyingStream active       │     │
     │   │ • Track LAST_TX_EVENT       │     │
     │   └─────────────────────────────┘     │
     │                                       │
     └───────┬───────────────────────────────┘
             │
             │ PTT tail timeout (100ms default)
             ▼
     ┌───────────────────────────────────────┐
     │              RX START                 │
     │   ┌─────────────────────────────┐     │
     │   │ • Push FLAG_RX_START        │     │
     │   │ • Enable GPIO ISR           │     │
     │   │ • Switch audio to WiFi PCM  │     │
     │   │ • RT thread sleeps          │     │
     │   └─────────────────────────────┘     │
     └───────┬───────────────────────────────┘
             │
             │
             └────────────────────────────────▶ (back to RX)
```

### 6.2 First Touch Problem: ISR One-Shot

**Challenge:** Capture first paddle touch reliably without losing to debounce.

**Solution:** Use ISR for wakeup only, then disable immediately.

```rust
fn rt_task() {
    loop {
        // ═══════════════════════════════════════════════════════
        // RX STATE
        // ═══════════════════════════════════════════════════════

        // Enable GPIO interrupt (one-shot)
        gpio_isr_handler_add(GPIO_PADDLE_DIT, isr_wakeup_handler);
        gpio_isr_handler_add(GPIO_PADDLE_DAH, isr_wakeup_handler);

        // Wait for ISR (thread sleeps, low power)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // ISR fired! Read GPIO immediately
        let first_paddle = gpio_read_paddles();

        // Disable ISR (ignore bounce for rest of TX session)
        gpio_isr_handler_remove(GPIO_PADDLE_DIT);
        gpio_isr_handler_remove(GPIO_PADDLE_DAH);

        // Push first event to stream
        stream.push(StreamSample {
            gpio: first_paddle,
            key: false,  // Iambic hasn't processed yet
            flags: FLAG_TX_START,
            ...
        });

        // Switch audio mode
        TX_ACTIVE.store(true, Ordering::Release);

        rt_info!("TX mode started");

        // ═══════════════════════════════════════════════════════
        // TX STATE
        // ═══════════════════════════════════════════════════════

        let exit_tx = tx_polling_loop(stream, &first_paddle);

        // ═══════════════════════════════════════════════════════
        // BACK TO RX
        // ═══════════════════════════════════════════════════════

        stream.push(StreamSample {
            flags: FLAG_RX_START,
            ...
        });

        TX_ACTIVE.store(false, Ordering::Release);

        rt_info!("RX mode started");
    }
}

fn tx_polling_loop(stream: &KeyingStream, first_paddle: &GpioState) -> bool {
    let mut last_tx_event_us = esp_timer_get_time();
    let ptt_tail_us = CONFIG.ptt_tail_ms.load(Ordering::Relaxed) as i64 * 1000;

    // Use first_paddle for first tick to avoid re-reading GPIO
    let mut paddles = *first_paddle;
    let mut first_tick = true;

    loop {
        let start_us = esp_timer_get_time();

        // Poll GPIO (skip first tick, already read by ISR)
        if !first_tick {
            paddles = gpio_read_paddles();
        }
        first_tick = false;

        // ... config check, iambic tick, envelope, stream push, audio consume ...

        // Track last TX activity
        if key || !paddles.is_idle() {
            last_tx_event_us = start_us;
        }

        // Check PTT tail timeout
        if (start_us - last_tx_event_us) > ptt_tail_us {
            return true; // Exit TX mode
        }

        // Spin-wait to next tick
        while esp_timer_get_time() - start_us < TICK_PERIOD_US {
            core::hint::spin_loop();
        }
    }
}
```

**Key points:**
- ✅ ISR captures first paddle touch reliably
- ✅ ISR disabled after first event (no bounce corruption)
- ✅ First paddle state used for first Iambic tick (no loss)
- ✅ TX loop uses polling @ 10kHz (low latency, no bounce)
- ✅ PTT tail timeout returns to RX (re-enables ISR)

---

## 7. Code Style Guidelines

### 7.1 File Header Comments (MANDATORY)

Every `.rs` file must start with:

```rust
//! Module: <module_name>
//!
//! Purpose: <1-2 sentence description>
//!
//! Architecture:
//! - <Key architectural constraint 1>
//! - <Key architectural constraint 2>
//! - <Relationship to ARCHITECTURE.md principles>
//!
//! Safety: <RT-safe/Best-effort/Has unsafe blocks/etc>
```

**Example:**
```rust
//! Module: stream
//!
//! Purpose: Lock-free SPMC ring buffer for keying events. Single source of
//! truth for all GPIO, local key, and audio state.
//!
//! Architecture:
//! - Zero mutexes, atomic operations only (ARCHITECTURE.md Rule 3.1)
//! - Single producer (RT thread), multiple consumers (RT + best-effort)
//! - Silence compression via run-length encoding
//!
//! Safety: Contains unsafe blocks for lock-free ring buffer indexing.
//! All unsafe blocks have SAFETY comments explaining invariants.
```

### 7.2 Function Documentation

Use doc comments (`///`) for all public functions:

```rust
/// Tick the Iambic FSM with current paddle state
///
/// # Arguments
/// * `paddles` - Current GPIO paddle state (DIT/DAH/both/neither)
/// * `config` - Iambic configuration (WPM, mode, weight, etc.)
///
/// # Returns
/// `true` if key should be down, `false` if up
///
/// # RT-Safety
/// This function is RT-safe: no allocation, no blocking, deterministic timing.
pub fn tick(&mut self, paddles: GpioState, config: &IambicConfig) -> bool {
    // ...
}
```

### 7.3 SAFETY Comments (MANDATORY for unsafe)

Every `unsafe` block must have `// SAFETY:` comment:

```rust
// SAFETY: Index is guaranteed in-bounds by ring buffer wrap logic.
// write_idx is only incremented by producer after writing.
// read_idx is only used by this consumer (thread-local).
// Modulo arithmetic ensures idx < BUFFER_SIZE always.
unsafe {
    let sample = &self.buffer[idx % BUFFER_SIZE];
    // ...
}
```

### 7.4 Naming Conventions

- Functions, variables, modules: `snake_case`
- Types, traits: `CamelCase`
- Constants, statics: `SCREAMING_SNAKE_CASE`
- **Unit suffixes:** `_us`, `_ms`, `_hz` for clarity

```rust
const TICK_PERIOD_US: u64 = 100;         // Microseconds
const PTT_TAIL_MS: u32 = 100;            // Milliseconds
const SIDETONE_FREQ_HZ: u16 = 700;       // Hertz
fn dit_duration_us(&self) -> u32 { ... } // Returns microseconds
```

### 7.5 Comment Density

**Target:** ~1 comment per 5-10 lines of non-trivial code.

**Trivial code** (no comment needed):
```rust
let x = y + 1;
self.counter += 1;
```

**Non-trivial code** (comment required):
```rust
// Spin-wait to next tick (busy-wait acceptable on dedicated RT core)
while esp_timer_get_time() - start_us < TICK_PERIOD_US {
    core::hint::spin_loop(); // CPU hint for efficient spinning
}
```

---

## 8. Testing Strategy

### 8.1 Stream-Based Replay Testing

**Philosophy:** The stream is the truth. If we can record and replay the stream, we can test everything deterministically.

**Approach:**

1. **Record GPIO stream from real hardware**
   ```
   Format: (timestamp_us, gpio_state) pairs

   Example file: tests/captures/iambic_mode_a_20wpm.txt
   1000000, 0x01    # T=1.0s, DIT pressed
   1060000, 0x00    # T=1.06s, DIT released
   1200000, 0x02    # T=1.2s, DAH pressed
   1380000, 0x00    # T=1.38s, DAH released
   ...
   ```

2. **Generate expected output stream (golden reference)**
   ```
   Format: StreamSample serialized

   Example file: tests/golden/iambic_mode_a_20wpm_output.bin
   Binary dump of expected StreamSample sequence
   ```

3. **Replay test:**
   ```rust
   #[test]
   fn test_iambic_mode_a_20wpm() {
       // Load recorded GPIO input
       let gpio_stream = load_capture("tests/captures/iambic_mode_a_20wpm.txt");

       // Load expected output
       let expected = load_golden("tests/golden/iambic_mode_a_20wpm_output.bin");

       // Run Iambic FSM with recorded input
       let mut iambic = IambicFsm::new();
       let config = IambicConfig { wpm: 20, mode: ModeA, ... };

       let mut actual = Vec::new();
       for (timestamp_us, gpio) in gpio_stream {
           let key = iambic.tick(gpio, &config);
           actual.push(StreamSample { gpio, key, ... });
       }

       // Compare
       assert_eq!(actual, expected, "Iambic output mismatch");
   }
   ```

### 8.2 Golden Reference Generation

**Option 1: Manual verification**
- Operator keys known pattern (e.g., "PARIS")
- Record GPIO + manual verification of output
- Store as golden reference

**Option 2: Existing keyer recording**
- Use proven CW keyer (commercial or C++ prototype)
- Record GPIO input + TX output at high frequency
- Convert to test format
- Use as golden reference

**Option 3: Synthesized test cases**
- Generate GPIO patterns programmatically
- Calculate expected output mathematically
- Good for edge cases (rapid alternation, boundary conditions)

### 8.3 Test Coverage

**Unit tests:**
- Iambic FSM (mode A/B, edge cases)
- Audio envelope (fade timing)
- Configuration system (atomic updates)
- Stream operations (push/consume, wrap-around)

**Integration tests:**
- Full RT loop with recorded GPIO
- Consumer lag detection (FAULT conditions)
- TX/RX state machine transitions
- Config changes during keying

**Hardware tests (on-target only):**
- GPIO interrupt latency
- Audio output (measure clicks, timing accuracy)
- WiFi packet timing (remote TX)

### 8.4 CI/CD Requirements

**All tests must run on host** (x86/ARM Linux/macOS):
- No ESP32 hardware required for CI
- Stream-based tests are portable
- HAL mocked for host testing

**On-target testing:**
- Manual or automated with hardware-in-loop
- Measure actual latency, jitter
- Validate against <100µs ceiling

---

## 9. Multi-Platform Support: ESP32-S3 and ESP32-P4

### 9.1 Target Platforms

**Primary targets:**
- **ESP32-S3**: Dual-core Xtensa LX7, PSRAM support, proven platform
- **ESP32-P4**: Dual-core RISC-V, PSRAM support, newer platform

**Rust support status (to verify):**
- ESP32-S3: ✅ Mature support via esp-idf-hal
- ESP32-P4: ❓ Check availability in esp-rs ecosystem

### 9.2 Hardware Abstraction Layer (HAL)

**Strategy:** Platform-specific code isolated in `hal/` module.

```
src/hal/
├── mod.rs           # Common HAL traits
├── gpio.rs          # GPIO abstraction (platform-specific impl)
├── audio.rs         # I2S/audio abstraction
└── platform/
    ├── esp32s3.rs   # ESP32-S3 specific
    └── esp32p4.rs   # ESP32-P4 specific (if available)
```

**Common HAL traits:**
```rust
// src/hal/mod.rs

/// Platform-independent GPIO paddle interface
pub trait PaddleGpio {
    fn read_paddles(&self) -> GpioState;
    fn enable_isr_oneshot(&mut self, callback: fn());
    fn disable_isr(&mut self);
}

/// Platform-independent audio output interface
pub trait AudioOutput {
    fn set_level(&mut self, level: u8);
    fn set_sidetone(&mut self, freq_hz: u16, volume: u8);
}
```

**Platform selection:**
```rust
// Cargo.toml features
[features]
default = ["esp32s3"]
esp32s3 = []
esp32p4 = []

// Conditional compilation
#[cfg(feature = "esp32s3")]
pub use crate::hal::platform::esp32s3::*;

#[cfg(feature = "esp32p4")]
pub use crate::hal::platform::esp32p4::*;
```

### 9.3 Platform Differences to Handle

**ESP32-S3 vs ESP32-P4 known differences:**

| Feature | ESP32-S3 | ESP32-P4 | Impact |
|---------|----------|----------|--------|
| Core arch | Xtensa LX7 | RISC-V | Compiler target, atomics ABI |
| PSRAM | 2-8MB | 8-32MB | Buffer sizing flexibility |
| GPIO count | 45 | 54 | Pin assignment flexibility |
| I2S | I2S0, I2S1 | I2S0, I2S1, I2S2 | Audio routing options |
| Max freq | 240 MHz | 400 MHz | RT loop headroom |
| Rust support | Stable | TBD (check esp-rs) | Development priority |

**Action items:**
- [ ] Verify ESP32-P4 support in esp-idf-hal/esp-idf-svc
- [ ] Test RISC-V atomics behavior (Ordering semantics)
- [ ] Benchmark RT loop performance on both platforms
- [ ] Document pin assignments per platform

### 9.4 Testing Strategy for Multi-Platform

**CI/CD matrix:**
```yaml
# .github/workflows/ci.yml
strategy:
  matrix:
    target:
      - xtensa-esp32s3-espidf
      - riscv32imc-esp-espidf  # ESP32-P4 (if available)
```

**Platform-specific tests:**
- Core logic (Iambic, Stream): platform-independent, run on host
- HAL integration: platform-specific, requires hardware or emulator
- Performance benchmarks: run on both platforms, compare results

### 9.5 Development Priority

**Phase 1-3: ESP32-S3 only**
- Prove architecture on stable platform
- All core logic platform-independent by design

**Phase 4: Add ESP32-P4 support**
- Implement platform-specific HAL
- Validate atomics, performance
- Document differences

**Rationale:** ESP32-S3 is proven, focus on architecture first. ESP32-P4 support is additive, low risk.

---

## 10. Implementation Phases

### Phase 1: Core Stream + RT Loop (MVP)
- [ ] StreamSample types
- [ ] KeyingStream (lock-free SPMC)
- [ ] Iambic FSM (mode A/B)
- [ ] Configuration system
- [ ] RT producer-consumer task (GPIO → Stream → stub audio)
- [ ] Unit tests with synthesized GPIO

**Deliverable:** RT loop running @ 10kHz on ESP32, no audio yet, tests passing

---

### Phase 1.5: RT-Safe Logging (Non-Blocking Debug)

**Purpose:** Implement lock-free logging system per ARCHITECTURE.md Rule 11 (RT-Safe Logging).

**Problem:** Standard logging (`ESP_LOGx`, `println!`) blocks for milliseconds (UART TX), violating RT constraints.

**Solution:** Lock-free log ring buffer + separate consumer thread.

#### Tasks:
- [ ] `LogEntry` structure (timestamp, level, pre-formatted message)
- [ ] `LogStream` lock-free SPSC ring buffer (256 entries typical)
- [ ] `rt_log!()` macro family (`rt_info!`, `rt_warn!`, `rt_error!`)
- [ ] Log consumer thread (Core 1, low priority, drains to UART)
- [ ] Dropped message counter (when ring full)
- [ ] Integration with RT loop (validate <200ns push time)

#### Implementation:

```rust
// src/logging.rs

pub struct LogEntry {
    timestamp_us: i64,
    level: u8,  // INFO=0, WARN=1, ERROR=2
    len: u8,
    msg: [u8; 128],
}

pub struct LogStream {
    entries: [LogEntry; 256],
    write_idx: AtomicU32,
    read_idx: AtomicU32,
    dropped: AtomicU32,
}

// RT-safe macro (non-blocking)
#[macro_export]
macro_rules! rt_info {
    ($($arg:tt)*) => {
        let _ = $crate::logging::LOG_STREAM.push(
            LogLevel::Info,
            format_args!($($arg)*)
        );
    };
}
```

#### Validation:
- [ ] Benchmark `rt_log!()` push time (<200ns)
- [ ] Stress test: log at 10kHz from RT thread, verify no drops
- [ ] Verify RT loop timing unaffected (before/after comparison)

**Deliverable:** RT loop can log diagnostics without timing impact. Foundation for debugging Phase 2+.

**Rationale:** Place between Phase 1 and 2 because:
- Phase 1 proves RT loop works (need visibility into it)
- Phase 2+ (audio, state machine) will need debug logging
- Non-blocking requirement critical before adding complexity

---

### Phase 2: Audio + TX/RX State Machine
- [ ] Audio envelope (fade in/out)
- [ ] Audio HAL (I2S sidetone generation)
- [ ] TX/RX state machine (ISR wakeup, PTT tail)
- [ ] Integration tests with recorded GPIO

**Deliverable:** Full keyer with sidetone, state machine, tests with real captures

---

### Phase 3: Best-Effort Consumers
- [ ] Remote TX consumer (WiFi stub)
- [ ] Decoder consumer
- [ ] Diagnostics consumer
- [ ] Consumer lag detection (FAULT)

**Deliverable:** All consumers operational, FAULT detection working

---

### Phase 4: WiFi Integration
- [ ] WiFi remote TX protocol (provided doc/C++ reference)
- [ ] WiFi RX audio (µ-law decode)
- [ ] Network discovery/pairing

**Deliverable:** Full remote CW capability

---

### Phase 5: Polish
- [ ] Configuration persistence (NVS)
- [ ] UI (CLI/web/display)
- [ ] Performance tuning (measure actual latency)
- [ ] Documentation

**Deliverable:** Production-ready keyer

---

## 10. Open Questions / Future Decisions

### 10.1 Deferred to Implementation
- Exact I2S configuration (sample rate, bit depth)
- GPIO pin assignments
- WiFi channel/frequency selection
- NVS schema for config persistence

### 10.2 Future Features (Not in Scope Now)
- Remote keyer mode (slave, receive CW events from network)
  - Different personality, reboot to switch
  - Will need separate design doc
- Opus audio codec (instead of µ-law)
  - Requires CPU budget analysis
- Multi-keyer network (>2 stations)
- CW training mode

### 10.3 Performance Targets (To Be Measured)
- Actual RT loop latency (target: 20-50µs typical, <100µs worst-case)
- CPU utilization (Core 0 @ 100% expected in TX, idle in RX)
- WiFi packet loss tolerance
- Audio click suppression effectiveness

---

## 11. References

- **ARCHITECTURE.md**: Immutable architectural principles (SPMC, lock-free, FAULT semantics)
- **CODE_STYLE.md**: Code style guidelines (file headers, SAFETY comments, naming)
- **C++ Prototype**: Reference implementation (to be provided)
- **Remote CW Protocol**: Network protocol spec (to be provided)

---

## 12. Approval & Sign-Off

**Design Status:** ✅ APPROVED (2025-01-19)

**Next Steps:**
1. Commit this design document
2. Begin Phase 1 implementation
3. Create implementation plan with detailed task breakdown

**Design Review Checklist:**
- [x] Threading model defined and compliant with ARCHITECTURE.md
- [x] Configuration system avoids forbidden patterns (no mutex, callbacks)
- [x] Audio architecture handles TX/RX switching correctly
- [x] State machine handles first paddle touch reliably
- [x] StreamSample structure finalized
- [x] Testing strategy defined (stream-based replay)
- [x] Code style guidelines documented
- [x] Implementation phases outlined

---

**End of Design Document**
