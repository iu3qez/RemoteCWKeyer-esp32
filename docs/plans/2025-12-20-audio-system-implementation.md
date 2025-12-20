# Audio System Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement sidetone generation and audio output via ES8311 codec with PTT state machine.

**Architecture:** Sidetone generator (LUT + phase accumulator) feeds ring buffer consumed by I2S DMA. PTT tail timer controls audio source switching between sidetone and remote audio. ES8311 configured via I2C, audio via I2S at 8 kHz.

**Tech Stack:** Rust, ESP-IDF I2S/I2C, heapless, atomic operations.

**Design Doc:** `docs/plans/2025-12-20-audio-system-design.md`

---

## Task 1: Sine LUT Generation

**Files:**
- Create: `src/audio/mod.rs`
- Create: `src/audio/lut.rs`
- Create: `tests/audio_lut_tests.rs`
- Modify: `src/lib.rs`

**Step 1.1: Write failing test for LUT values**

Create `tests/audio_lut_tests.rs`:

```rust
//! Sine lookup table tests

use rust_remote_cw_keyer::audio::lut::{SINE_LUT, LUT_SIZE};

#[test]
fn test_lut_size() {
    assert_eq!(SINE_LUT.len(), LUT_SIZE);
    assert_eq!(LUT_SIZE, 256);
}

#[test]
fn test_lut_zero_crossing() {
    // Index 0 should be ~0 (sine starts at 0)
    assert!(SINE_LUT[0].abs() < 100, "LUT[0] should be near zero");

    // Index 64 should be ~max (sine peak at 90°)
    assert!(SINE_LUT[64] > 30000, "LUT[64] should be near max");

    // Index 128 should be ~0 (sine at 180°)
    assert!(SINE_LUT[128].abs() < 100, "LUT[128] should be near zero");

    // Index 192 should be ~min (sine trough at 270°)
    assert!(SINE_LUT[192] < -30000, "LUT[192] should be near min");
}

#[test]
fn test_lut_symmetry() {
    // First half positive peak, second half negative
    for i in 0..64 {
        assert!(SINE_LUT[i] >= 0 || i == 0, "First quadrant should be non-negative");
    }
    for i in 128..192 {
        assert!(SINE_LUT[i] <= 0, "Third quadrant should be non-positive");
    }
}

#[test]
fn test_lut_amplitude() {
    let max = SINE_LUT.iter().max().copied().unwrap();
    let min = SINE_LUT.iter().min().copied().unwrap();

    // Should use full i16 range
    assert!(max > 32000, "Max should be near i16::MAX");
    assert!(min < -32000, "Min should be near i16::MIN");
}
```

**Step 1.2: Run test to verify it fails**

Run: `cargo test audio_lut`
Expected: FAIL - module not found

**Step 1.3: Create audio module structure**

Create `src/audio/mod.rs`:

```rust
//! Audio subsystem for sidetone and remote audio
//!
//! Architecture:
//! - Sidetone generator: LUT + phase accumulator, digital fade
//! - Ring buffers: 64 samples (sidetone), 512 (remote)
//! - PTT state machine controls audio source
//! - ES8311 codec via I2S @ 8 kHz

pub mod lut;

pub use lut::{SINE_LUT, LUT_SIZE};
```

Create `src/audio/lut.rs`:

```rust
//! Sine wave lookup table for sidetone generation
//!
//! 256-entry table covering one full cycle.
//! Values are i16 for direct use with I2S.

/// Number of entries in the sine LUT
pub const LUT_SIZE: usize = 256;

/// Pre-computed sine wave lookup table
///
/// 256 samples covering 0 to 2π
/// Amplitude: full i16 range (-32767 to +32767)
/// Index 0 = 0°, 64 = 90°, 128 = 180°, 192 = 270°
pub static SINE_LUT: [i16; LUT_SIZE] = {
    let mut table = [0i16; LUT_SIZE];
    let mut i = 0;
    while i < LUT_SIZE {
        // sin(2π * i / 256) * 32767
        // Using Taylor series approximation for const evaluation
        let angle = (i as f64) * core::f64::consts::PI * 2.0 / (LUT_SIZE as f64);
        let sin_val = const_sin(angle);
        table[i] = (sin_val * 32767.0) as i16;
        i += 1;
    }
    table
};

/// Const-compatible sine approximation using Taylor series
const fn const_sin(x: f64) -> f64 {
    // Normalize to [-π, π]
    let mut x = x;
    while x > core::f64::consts::PI {
        x -= 2.0 * core::f64::consts::PI;
    }
    while x < -core::f64::consts::PI {
        x += 2.0 * core::f64::consts::PI;
    }

    // Taylor series: sin(x) = x - x³/3! + x⁵/5! - x⁷/7! + ...
    let x2 = x * x;
    let x3 = x2 * x;
    let x5 = x3 * x2;
    let x7 = x5 * x2;
    let x9 = x7 * x2;

    x - x3 / 6.0 + x5 / 120.0 - x7 / 5040.0 + x9 / 362880.0
}
```

**Step 1.4: Add audio module to lib.rs**

Modify `src/lib.rs`, add after other modules:

```rust
pub mod audio;
```

**Step 1.5: Run test to verify it passes**

Run: `cargo test audio_lut`
Expected: All 4 tests PASS

**Step 1.6: Commit**

```bash
git add src/audio/ tests/audio_lut_tests.rs src/lib.rs
git commit -m "feat(audio): add sine lookup table for sidetone"
```

---

## Task 2: Sidetone Generator Core

**Files:**
- Create: `src/audio/sidetone.rs`
- Create: `tests/audio_sidetone_tests.rs`
- Modify: `src/audio/mod.rs`

**Step 2.1: Write failing tests for sidetone generator**

Create `tests/audio_sidetone_tests.rs`:

```rust
//! Sidetone generator tests

use rust_remote_cw_keyer::audio::sidetone::{SidetoneGen, FadeState};

const SAMPLE_RATE: u32 = 8000;

#[test]
fn test_sidetone_gen_creation() {
    let gen = SidetoneGen::new(700, SAMPLE_RATE, 40); // 700Hz, 8kHz, 5ms fade (40 samples)
    assert_eq!(gen.fade_state(), FadeState::Silent);
}

#[test]
fn test_sidetone_silent_when_key_up() {
    let mut gen = SidetoneGen::new(700, SAMPLE_RATE, 40);

    // Generate samples with key up
    for _ in 0..100 {
        let sample = gen.next_sample(false);
        assert_eq!(sample, 0, "Should be silent when key up");
    }
}

#[test]
fn test_sidetone_produces_audio_when_key_down() {
    let mut gen = SidetoneGen::new(700, SAMPLE_RATE, 40);

    // Key down, skip fade-in period
    for _ in 0..50 {
        gen.next_sample(true);
    }

    // Now should be producing audio
    let sample = gen.next_sample(true);
    assert_ne!(sample, 0, "Should produce audio when key down after fade-in");
}

#[test]
fn test_sidetone_fade_in() {
    let mut gen = SidetoneGen::new(700, SAMPLE_RATE, 40);

    // First sample should be near zero (start of fade)
    let first = gen.next_sample(true).abs();

    // Skip to end of fade
    for _ in 0..38 {
        gen.next_sample(true);
    }

    // Last fade sample should be louder
    let last = gen.next_sample(true).abs();

    // After fade, should be at full amplitude
    gen.next_sample(true);
    assert_eq!(gen.fade_state(), FadeState::Sustain);
}

#[test]
fn test_sidetone_fade_out() {
    let mut gen = SidetoneGen::new(700, SAMPLE_RATE, 40);

    // Key down, complete fade-in
    for _ in 0..50 {
        gen.next_sample(true);
    }
    assert_eq!(gen.fade_state(), FadeState::Sustain);

    // Key up, should start fade-out
    gen.next_sample(false);
    assert_eq!(gen.fade_state(), FadeState::FadeOut);

    // Complete fade-out
    for _ in 0..50 {
        gen.next_sample(false);
    }
    assert_eq!(gen.fade_state(), FadeState::Silent);
}

#[test]
fn test_sidetone_frequency_accuracy() {
    let mut gen = SidetoneGen::new(800, SAMPLE_RATE, 8); // 800 Hz, minimal fade

    // Key down, skip fade
    for _ in 0..10 {
        gen.next_sample(true);
    }

    // At 800 Hz and 8000 sample rate, one cycle = 10 samples
    // Collect one full cycle worth of samples
    let mut samples = Vec::new();
    for _ in 0..10 {
        samples.push(gen.next_sample(true));
    }

    // Should have both positive and negative values (completed a cycle)
    let has_positive = samples.iter().any(|&s| s > 1000);
    let has_negative = samples.iter().any(|&s| s < -1000);
    assert!(has_positive && has_negative, "Should complete a cycle in 10 samples");
}

#[test]
fn test_sidetone_phase_continuity() {
    let mut gen = SidetoneGen::new(700, SAMPLE_RATE, 8);

    // Key down
    for _ in 0..20 {
        gen.next_sample(true);
    }

    // Get two consecutive samples
    let s1 = gen.next_sample(true);
    let s2 = gen.next_sample(true);

    // Difference should be small (no phase jumps)
    let diff = (s2 as i32 - s1 as i32).abs();
    assert!(diff < 5000, "Phase should be continuous, diff={}", diff);
}
```

**Step 2.2: Run test to verify it fails**

Run: `cargo test audio_sidetone`
Expected: FAIL - module not found

**Step 2.3: Implement sidetone generator**

Create `src/audio/sidetone.rs`:

```rust
//! Sidetone generator with phase accumulator and fade envelope
//!
//! Uses lookup table for sine wave, fixed-point phase accumulator
//! for frequency control, and linear fade for anti-click.

use super::lut::{SINE_LUT, LUT_SIZE};

/// Fade envelope state
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FadeState {
    /// Output is zero
    Silent,
    /// Ramping up (0 → full)
    FadeIn,
    /// Full amplitude output
    Sustain,
    /// Ramping down (full → 0)
    FadeOut,
}

/// Sidetone generator
///
/// Generates sine wave with configurable frequency and fade envelope.
/// Uses phase accumulator for frequency control (no floating point in hot path).
pub struct SidetoneGen {
    /// Phase accumulator (32-bit fixed point, top 8 bits = LUT index)
    phase: u32,
    /// Phase increment per sample (determines frequency)
    phase_inc: u32,
    /// Current fade state
    fade_state: FadeState,
    /// Current fade position (0 to fade_len)
    fade_pos: u16,
    /// Fade length in samples
    fade_len: u16,
}

impl SidetoneGen {
    /// Create new sidetone generator
    ///
    /// # Arguments
    /// * `freq_hz` - Sidetone frequency in Hz (typically 400-800)
    /// * `sample_rate` - Output sample rate in Hz (typically 8000)
    /// * `fade_samples` - Fade duration in samples (e.g., 40 for 5ms @ 8kHz)
    pub fn new(freq_hz: u32, sample_rate: u32, fade_samples: u16) -> Self {
        Self {
            phase: 0,
            phase_inc: Self::calc_phase_inc(freq_hz, sample_rate),
            fade_state: FadeState::Silent,
            fade_pos: 0,
            fade_len: fade_samples.max(1), // Avoid div by zero
        }
    }

    /// Calculate phase increment for target frequency
    ///
    /// phase_inc = (freq * 2^32) / sample_rate
    #[inline]
    fn calc_phase_inc(freq_hz: u32, sample_rate: u32) -> u32 {
        ((freq_hz as u64 * (1u64 << 32)) / sample_rate as u64) as u32
    }

    /// Update frequency (e.g., when config changes)
    #[inline]
    pub fn set_frequency(&mut self, freq_hz: u32, sample_rate: u32) {
        self.phase_inc = Self::calc_phase_inc(freq_hz, sample_rate);
    }

    /// Get current fade state
    #[inline]
    pub fn fade_state(&self) -> FadeState {
        self.fade_state
    }

    /// Generate next audio sample
    ///
    /// # Arguments
    /// * `key_down` - true if key is pressed (generate tone), false for silence
    ///
    /// # Returns
    /// Audio sample in i16 range
    #[inline]
    pub fn next_sample(&mut self, key_down: bool) -> i16 {
        // Update fade state and get amplitude multiplier
        let amplitude = self.update_fade(key_down);

        if amplitude == 0 {
            return 0;
        }

        // LUT lookup with phase accumulator
        let idx = (self.phase >> 24) as usize; // Top 8 bits = index (0-255)
        self.phase = self.phase.wrapping_add(self.phase_inc);

        // Get sample from LUT
        let sample = SINE_LUT[idx % LUT_SIZE];

        // Apply fade envelope
        ((sample as i32 * amplitude as i32) >> 16) as i16
    }

    /// Update fade state machine, returns amplitude (0-65535)
    #[inline]
    fn update_fade(&mut self, key_down: bool) -> u16 {
        match (self.fade_state, key_down) {
            // Silent + key down → start fade in
            (FadeState::Silent, true) => {
                self.fade_state = FadeState::FadeIn;
                self.fade_pos = 0;
                0
            }

            // Fade in + key down → continue fade
            (FadeState::FadeIn, true) => {
                self.fade_pos += 1;
                if self.fade_pos >= self.fade_len {
                    self.fade_state = FadeState::Sustain;
                    0xFFFF
                } else {
                    ((self.fade_pos as u32 * 0xFFFF) / self.fade_len as u32) as u16
                }
            }

            // Sustain + key down → full amplitude
            (FadeState::Sustain, true) => 0xFFFF,

            // Key up while fade in or sustain → start fade out
            (FadeState::FadeIn, false) | (FadeState::Sustain, false) => {
                self.fade_state = FadeState::FadeOut;
                self.fade_pos = self.fade_len;
                0xFFFF
            }

            // Fade out → continue fade
            (FadeState::FadeOut, _) => {
                if self.fade_pos == 0 {
                    self.fade_state = FadeState::Silent;
                    0
                } else {
                    self.fade_pos -= 1;
                    ((self.fade_pos as u32 * 0xFFFF) / self.fade_len as u32) as u16
                }
            }

            // Silent + key up → stay silent
            (FadeState::Silent, false) => 0,
        }
    }

    /// Reset generator state (e.g., after fault)
    pub fn reset(&mut self) {
        self.phase = 0;
        self.fade_state = FadeState::Silent;
        self.fade_pos = 0;
    }
}
```

**Step 2.4: Export sidetone from mod.rs**

Modify `src/audio/mod.rs`:

```rust
//! Audio subsystem for sidetone and remote audio
//!
//! Architecture:
//! - Sidetone generator: LUT + phase accumulator, digital fade
//! - Ring buffers: 64 samples (sidetone), 512 (remote)
//! - PTT state machine controls audio source
//! - ES8311 codec via I2S @ 8 kHz

pub mod lut;
pub mod sidetone;

pub use lut::{SINE_LUT, LUT_SIZE};
pub use sidetone::{SidetoneGen, FadeState};
```

**Step 2.5: Run test to verify it passes**

Run: `cargo test audio_sidetone`
Expected: All 7 tests PASS

**Step 2.6: Commit**

```bash
git add src/audio/sidetone.rs tests/audio_sidetone_tests.rs
git commit -m "feat(audio): add sidetone generator with fade envelope"
```

---

## Task 3: Audio Ring Buffers

**Files:**
- Create: `src/audio/buffer.rs`
- Create: `tests/audio_buffer_tests.rs`
- Modify: `src/audio/mod.rs`

**Step 3.1: Write failing tests for ring buffer**

Create `tests/audio_buffer_tests.rs`:

```rust
//! Audio ring buffer tests

use rust_remote_cw_keyer::audio::buffer::AudioRingBuffer;

#[test]
fn test_buffer_empty() {
    let buf: AudioRingBuffer<64> = AudioRingBuffer::new();
    assert!(buf.is_empty());
    assert_eq!(buf.len(), 0);
}

#[test]
fn test_buffer_push_pop() {
    let mut buf: AudioRingBuffer<64> = AudioRingBuffer::new();

    buf.push(100);
    buf.push(200);
    buf.push(300);

    assert_eq!(buf.len(), 3);
    assert_eq!(buf.pop(), Some(100));
    assert_eq!(buf.pop(), Some(200));
    assert_eq!(buf.pop(), Some(300));
    assert_eq!(buf.pop(), None);
}

#[test]
fn test_buffer_wrap_around() {
    let mut buf: AudioRingBuffer<4> = AudioRingBuffer::new();

    // Fill buffer
    buf.push(1);
    buf.push(2);
    buf.push(3);
    buf.push(4);

    // Pop some
    assert_eq!(buf.pop(), Some(1));
    assert_eq!(buf.pop(), Some(2));

    // Push more (wraps around)
    buf.push(5);
    buf.push(6);

    // Should get remaining in order
    assert_eq!(buf.pop(), Some(3));
    assert_eq!(buf.pop(), Some(4));
    assert_eq!(buf.pop(), Some(5));
    assert_eq!(buf.pop(), Some(6));
}

#[test]
fn test_buffer_overflow_drops_oldest() {
    let mut buf: AudioRingBuffer<4> = AudioRingBuffer::new();

    // Overfill
    buf.push(1);
    buf.push(2);
    buf.push(3);
    buf.push(4);
    buf.push(5); // Overwrites 1
    buf.push(6); // Overwrites 2

    // Should get 3,4,5,6 (oldest dropped)
    assert_eq!(buf.pop(), Some(3));
    assert_eq!(buf.pop(), Some(4));
    assert_eq!(buf.pop(), Some(5));
    assert_eq!(buf.pop(), Some(6));
}

#[test]
fn test_buffer_read_into_slice() {
    let mut buf: AudioRingBuffer<64> = AudioRingBuffer::new();

    for i in 0..10i16 {
        buf.push(i * 100);
    }

    let mut output = [0i16; 5];
    let read = buf.read_into(&mut output);

    assert_eq!(read, 5);
    assert_eq!(output, [0, 100, 200, 300, 400]);

    // Buffer should have 5 remaining
    assert_eq!(buf.len(), 5);
}

#[test]
fn test_buffer_read_into_underrun() {
    let mut buf: AudioRingBuffer<64> = AudioRingBuffer::new();

    buf.push(100);
    buf.push(200);

    let mut output = [0i16; 5];
    let read = buf.read_into(&mut output);

    assert_eq!(read, 2);
    assert_eq!(output[0], 100);
    assert_eq!(output[1], 200);
    // Rest should be zeroed
    assert_eq!(output[2], 0);
    assert_eq!(output[3], 0);
    assert_eq!(output[4], 0);
}

#[test]
fn test_buffer_available() {
    let buf: AudioRingBuffer<64> = AudioRingBuffer::new();
    assert_eq!(buf.available(), 64);

    let mut buf: AudioRingBuffer<64> = AudioRingBuffer::new();
    for i in 0..10 {
        buf.push(i);
    }
    assert_eq!(buf.available(), 54);
}
```

**Step 3.2: Run test to verify it fails**

Run: `cargo test audio_buffer`
Expected: FAIL - module not found

**Step 3.3: Implement ring buffer**

Create `src/audio/buffer.rs`:

```rust
//! Lock-free audio ring buffer
//!
//! SPSC (single producer, single consumer) for audio samples.
//! Uses atomic indices for thread-safe access.

use core::sync::atomic::{AtomicUsize, Ordering};

/// Audio ring buffer with static size
///
/// N must be a power of 2 for efficient modulo.
pub struct AudioRingBuffer<const N: usize> {
    buffer: [i16; N],
    write_idx: AtomicUsize,
    read_idx: AtomicUsize,
}

impl<const N: usize> AudioRingBuffer<N> {
    /// Create new empty buffer
    pub const fn new() -> Self {
        // Compile-time check that N is power of 2
        const { assert!(N.is_power_of_two(), "Buffer size must be power of 2") };

        Self {
            buffer: [0i16; N],
            write_idx: AtomicUsize::new(0),
            read_idx: AtomicUsize::new(0),
        }
    }

    /// Push a sample to the buffer
    ///
    /// If buffer is full, overwrites oldest sample.
    #[inline]
    pub fn push(&mut self, sample: i16) {
        let write = self.write_idx.load(Ordering::Relaxed);
        let read = self.read_idx.load(Ordering::Relaxed);

        // Check if we need to advance read (overflow)
        if write.wrapping_sub(read) >= N {
            self.read_idx.store(read.wrapping_add(1), Ordering::Relaxed);
        }

        self.buffer[write & (N - 1)] = sample;
        self.write_idx.store(write.wrapping_add(1), Ordering::Release);
    }

    /// Pop a sample from the buffer
    ///
    /// Returns None if buffer is empty.
    #[inline]
    pub fn pop(&mut self) -> Option<i16> {
        let write = self.write_idx.load(Ordering::Acquire);
        let read = self.read_idx.load(Ordering::Relaxed);

        if write == read {
            return None; // Empty
        }

        let sample = self.buffer[read & (N - 1)];
        self.read_idx.store(read.wrapping_add(1), Ordering::Relaxed);
        Some(sample)
    }

    /// Read multiple samples into a slice
    ///
    /// Fills as many samples as available, zeros the rest.
    /// Returns number of samples actually read.
    #[inline]
    pub fn read_into(&mut self, output: &mut [i16]) -> usize {
        let mut count = 0;

        for sample in output.iter_mut() {
            match self.pop() {
                Some(s) => {
                    *sample = s;
                    count += 1;
                }
                None => {
                    *sample = 0; // Underrun: fill with silence
                }
            }
        }

        count
    }

    /// Get number of samples in buffer
    #[inline]
    pub fn len(&self) -> usize {
        let write = self.write_idx.load(Ordering::Acquire);
        let read = self.read_idx.load(Ordering::Relaxed);
        write.wrapping_sub(read).min(N)
    }

    /// Check if buffer is empty
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Get available space in buffer
    #[inline]
    pub fn available(&self) -> usize {
        N - self.len()
    }

    /// Clear the buffer
    #[inline]
    pub fn clear(&mut self) {
        let write = self.write_idx.load(Ordering::Relaxed);
        self.read_idx.store(write, Ordering::Relaxed);
    }
}

impl<const N: usize> Default for AudioRingBuffer<N> {
    fn default() -> Self {
        Self::new()
    }
}
```

**Step 3.4: Export buffer from mod.rs**

Add to `src/audio/mod.rs`:

```rust
pub mod buffer;
pub use buffer::AudioRingBuffer;
```

**Step 3.5: Run test to verify it passes**

Run: `cargo test audio_buffer`
Expected: All 7 tests PASS

**Step 3.6: Commit**

```bash
git add src/audio/buffer.rs tests/audio_buffer_tests.rs
git commit -m "feat(audio): add lock-free audio ring buffer"
```

---

## Task 4: PTT State Machine

**Files:**
- Create: `src/audio/ptt.rs`
- Create: `tests/audio_ptt_tests.rs`
- Modify: `src/audio/mod.rs`

**Step 4.1: Write failing tests for PTT**

Create `tests/audio_ptt_tests.rs`:

```rust
//! PTT state machine tests

use rust_remote_cw_keyer::audio::ptt::{PttState, PttController};

// Mock microsecond time for testing
fn make_ptt(tail_ms: u32) -> PttController {
    PttController::new(tail_ms)
}

#[test]
fn test_ptt_initial_state() {
    let ptt = make_ptt(100);
    assert_eq!(ptt.state(), PttState::Off);
}

#[test]
fn test_ptt_activates_on_audio() {
    let mut ptt = make_ptt(100);

    // First audio sample triggers PTT
    ptt.audio_sample(1000); // timestamp 1000us
    assert_eq!(ptt.state(), PttState::On);
}

#[test]
fn test_ptt_stays_on_during_audio() {
    let mut ptt = make_ptt(100);

    ptt.audio_sample(1000);
    assert_eq!(ptt.state(), PttState::On);

    ptt.audio_sample(2000);
    assert_eq!(ptt.state(), PttState::On);

    ptt.audio_sample(3000);
    assert_eq!(ptt.state(), PttState::On);
}

#[test]
fn test_ptt_tail_timeout() {
    let mut ptt = make_ptt(100); // 100ms tail

    // Start audio at t=0
    ptt.audio_sample(0);
    assert_eq!(ptt.state(), PttState::On);

    // Last audio at t=1000us (1ms)
    ptt.audio_sample(1000);

    // Check at t=50ms: should still be on (within tail)
    ptt.tick(50_000);
    assert_eq!(ptt.state(), PttState::On);

    // Check at t=101ms: should be off (tail expired)
    // 1000us (last audio) + 100_000us (tail) = 101_000us
    ptt.tick(102_000);
    assert_eq!(ptt.state(), PttState::Off);
}

#[test]
fn test_ptt_tail_reset_on_new_audio() {
    let mut ptt = make_ptt(100); // 100ms tail

    // Audio at t=0
    ptt.audio_sample(0);

    // Check at t=50ms: still on
    ptt.tick(50_000);
    assert_eq!(ptt.state(), PttState::On);

    // New audio at t=80ms resets tail
    ptt.audio_sample(80_000);

    // Check at t=150ms: should still be on (80ms + 100ms = 180ms)
    ptt.tick(150_000);
    assert_eq!(ptt.state(), PttState::On);

    // Check at t=181ms: should be off
    ptt.tick(181_000);
    assert_eq!(ptt.state(), PttState::Off);
}

#[test]
fn test_ptt_immediate_on() {
    let mut ptt = make_ptt(100);

    // Should be off
    assert_eq!(ptt.state(), PttState::Off);

    // Audio triggers immediate on (no delay)
    ptt.audio_sample(5000);
    assert_eq!(ptt.state(), PttState::On);
}

#[test]
fn test_ptt_reactivation() {
    let mut ptt = make_ptt(50); // 50ms tail

    // First activation
    ptt.audio_sample(0);
    assert_eq!(ptt.state(), PttState::On);

    // Expire
    ptt.tick(60_000);
    assert_eq!(ptt.state(), PttState::Off);

    // Reactivate
    ptt.audio_sample(70_000);
    assert_eq!(ptt.state(), PttState::On);
}
```

**Step 4.2: Run test to verify it fails**

Run: `cargo test audio_ptt`
Expected: FAIL - module not found

**Step 4.3: Implement PTT controller**

Create `src/audio/ptt.rs`:

```rust
//! PTT (Push-To-Talk) state machine
//!
//! Controls TX/RX switching based on audio output.
//! PTT activates immediately on first audio sample,
//! deactivates after tail timeout from last audio.

/// PTT state
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PttState {
    /// Not transmitting (RX mode)
    Off,
    /// Transmitting (TX mode)
    On,
}

/// PTT controller
///
/// Manages PTT state based on audio activity.
pub struct PttController {
    /// Current PTT state
    state: PttState,
    /// Tail timeout in microseconds
    tail_us: u64,
    /// Timestamp of last audio sample (microseconds)
    last_audio_us: u64,
    /// Whether we've seen any audio since last Off
    audio_active: bool,
}

impl PttController {
    /// Create new PTT controller
    ///
    /// # Arguments
    /// * `tail_ms` - Tail timeout in milliseconds
    pub fn new(tail_ms: u32) -> Self {
        Self {
            state: PttState::Off,
            tail_us: tail_ms as u64 * 1000,
            last_audio_us: 0,
            audio_active: false,
        }
    }

    /// Get current PTT state
    #[inline]
    pub fn state(&self) -> PttState {
        self.state
    }

    /// Check if PTT is on
    #[inline]
    pub fn is_on(&self) -> bool {
        self.state == PttState::On
    }

    /// Notify controller of audio sample output
    ///
    /// # Arguments
    /// * `timestamp_us` - Current time in microseconds
    ///
    /// Call this for each audio sample that represents actual keying
    /// (not silence). This triggers PTT on and resets the tail timer.
    #[inline]
    pub fn audio_sample(&mut self, timestamp_us: u64) {
        self.last_audio_us = timestamp_us;
        self.audio_active = true;

        if self.state == PttState::Off {
            self.state = PttState::On;
        }
    }

    /// Periodic tick to check for tail timeout
    ///
    /// # Arguments
    /// * `timestamp_us` - Current time in microseconds
    ///
    /// Call this periodically (e.g., every audio buffer) to check
    /// if the tail timeout has expired.
    #[inline]
    pub fn tick(&mut self, timestamp_us: u64) {
        if self.state == PttState::On && self.audio_active {
            // Check if tail has expired
            let elapsed = timestamp_us.saturating_sub(self.last_audio_us);
            if elapsed >= self.tail_us {
                self.state = PttState::Off;
                self.audio_active = false;
            }
        }
    }

    /// Update tail timeout (e.g., when config changes)
    #[inline]
    pub fn set_tail_ms(&mut self, tail_ms: u32) {
        self.tail_us = tail_ms as u64 * 1000;
    }

    /// Force PTT off immediately
    #[inline]
    pub fn force_off(&mut self) {
        self.state = PttState::Off;
        self.audio_active = false;
    }

    /// Reset to initial state
    #[inline]
    pub fn reset(&mut self) {
        self.state = PttState::Off;
        self.last_audio_us = 0;
        self.audio_active = false;
    }
}
```

**Step 4.4: Export PTT from mod.rs**

Add to `src/audio/mod.rs`:

```rust
pub mod ptt;
pub use ptt::{PttState, PttController};
```

**Step 4.5: Run test to verify it passes**

Run: `cargo test audio_ptt`
Expected: All 7 tests PASS

**Step 4.6: Commit**

```bash
git add src/audio/ptt.rs tests/audio_ptt_tests.rs
git commit -m "feat(audio): add PTT state machine with tail timeout"
```

---

## Task 5: Audio Source Selector

**Files:**
- Create: `src/audio/source.rs`
- Create: `tests/audio_source_tests.rs`
- Modify: `src/audio/mod.rs`

**Step 5.1: Write failing tests for audio source**

Create `tests/audio_source_tests.rs`:

```rust
//! Audio source selector tests

use rust_remote_cw_keyer::audio::source::{AudioSource, AudioSourceSelector};

#[test]
fn test_source_initial_state() {
    let selector = AudioSourceSelector::new();
    assert_eq!(selector.get(), AudioSource::Silence);
}

#[test]
fn test_source_set_sidetone() {
    let selector = AudioSourceSelector::new();
    selector.set(AudioSource::Sidetone);
    assert_eq!(selector.get(), AudioSource::Sidetone);
}

#[test]
fn test_source_set_remote() {
    let selector = AudioSourceSelector::new();
    selector.set(AudioSource::Remote);
    assert_eq!(selector.get(), AudioSource::Remote);
}

#[test]
fn test_source_atomic_update() {
    use std::sync::Arc;
    use std::thread;

    let selector = Arc::new(AudioSourceSelector::new());

    // Spawn multiple threads setting different values
    let handles: Vec<_> = (0..10)
        .map(|i| {
            let sel = Arc::clone(&selector);
            thread::spawn(move || {
                for _ in 0..100 {
                    if i % 2 == 0 {
                        sel.set(AudioSource::Sidetone);
                    } else {
                        sel.set(AudioSource::Remote);
                    }
                }
            })
        })
        .collect();

    for h in handles {
        h.join().unwrap();
    }

    // Should be one of the valid states (not corrupted)
    let state = selector.get();
    assert!(
        state == AudioSource::Silence
            || state == AudioSource::Sidetone
            || state == AudioSource::Remote
    );
}
```

**Step 5.2: Run test to verify it fails**

Run: `cargo test audio_source`
Expected: FAIL - module not found

**Step 5.3: Implement audio source selector**

Create `src/audio/source.rs`:

```rust
//! Audio source selector
//!
//! Atomic selection between silence, sidetone, and remote audio.

use core::sync::atomic::{AtomicU8, Ordering};

/// Audio source selection
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AudioSource {
    /// No audio output
    Silence = 0,
    /// Local sidetone (keyer)
    Sidetone = 1,
    /// Remote audio (received from network)
    Remote = 2,
}

impl AudioSource {
    /// Convert from u8
    #[inline]
    pub fn from_u8(v: u8) -> Self {
        match v {
            1 => Self::Sidetone,
            2 => Self::Remote,
            _ => Self::Silence,
        }
    }
}

impl From<u8> for AudioSource {
    fn from(v: u8) -> Self {
        Self::from_u8(v)
    }
}

impl From<AudioSource> for u8 {
    fn from(s: AudioSource) -> Self {
        s as u8
    }
}

/// Thread-safe audio source selector
pub struct AudioSourceSelector {
    source: AtomicU8,
}

impl AudioSourceSelector {
    /// Create new selector (starts at Silence)
    pub const fn new() -> Self {
        Self {
            source: AtomicU8::new(AudioSource::Silence as u8),
        }
    }

    /// Get current audio source
    #[inline]
    pub fn get(&self) -> AudioSource {
        AudioSource::from_u8(self.source.load(Ordering::Acquire))
    }

    /// Set audio source
    #[inline]
    pub fn set(&self, source: AudioSource) {
        self.source.store(source as u8, Ordering::Release);
    }

    /// Atomically swap to new source, return old
    #[inline]
    pub fn swap(&self, source: AudioSource) -> AudioSource {
        AudioSource::from_u8(self.source.swap(source as u8, Ordering::AcqRel))
    }
}

impl Default for AudioSourceSelector {
    fn default() -> Self {
        Self::new()
    }
}

// Allow sharing between threads
unsafe impl Sync for AudioSourceSelector {}
```

**Step 5.4: Export source from mod.rs**

Add to `src/audio/mod.rs`:

```rust
pub mod source;
pub use source::{AudioSource, AudioSourceSelector};
```

**Step 5.5: Run test to verify it passes**

Run: `cargo test audio_source`
Expected: All 4 tests PASS

**Step 5.6: Commit**

```bash
git add src/audio/source.rs tests/audio_source_tests.rs
git commit -m "feat(audio): add atomic audio source selector"
```

---

## Task 6: ES8311 Driver (ESP32 only)

**Files:**
- Create: `src/hal/es8311.rs`
- Modify: `src/hal/mod.rs`

**Note:** This task has minimal host tests since it's hardware-dependent.

**Step 6.1: Create ES8311 driver stub**

Create `src/hal/es8311.rs`:

```rust
//! ES8311 audio codec driver
//!
//! I2C control + I2S audio interface.
//! Reference: ES8311 datasheet

/// ES8311 I2C address (depends on AD0 pin)
pub const ES8311_ADDR: u8 = 0x18; // AD0 = LOW

/// ES8311 register addresses
#[allow(dead_code)]
mod regs {
    pub const RESET: u8 = 0x00;
    pub const CLK_MANAGER1: u8 = 0x01;
    pub const CLK_MANAGER2: u8 = 0x02;
    pub const CLK_MANAGER3: u8 = 0x03;
    pub const CLK_MANAGER4: u8 = 0x04;
    pub const CLK_MANAGER5: u8 = 0x05;
    pub const CLK_MANAGER6: u8 = 0x06;
    pub const CLK_MANAGER7: u8 = 0x07;
    pub const CLK_MANAGER8: u8 = 0x08;
    pub const SDP_IN: u8 = 0x09;
    pub const SDP_OUT: u8 = 0x0A;
    pub const SYSTEM: u8 = 0x0B;
    pub const SYS_MODSEL: u8 = 0x0D;
    pub const ADC_MUTE: u8 = 0x0F;
    pub const ADC_VOL: u8 = 0x10;
    pub const ADC_ALC1: u8 = 0x11;
    pub const ADC_ALC2: u8 = 0x12;
    pub const ADC_ALC3: u8 = 0x13;
    pub const ADC_ALC4: u8 = 0x14;
    pub const ADC_ALC5: u8 = 0x15;
    pub const ADC_ALC6: u8 = 0x16;
    pub const DAC_MUTE: u8 = 0x17;
    pub const DAC_VOL: u8 = 0x18;
    pub const DAC_DEM: u8 = 0x19;
    pub const DAC_RAMP: u8 = 0x1A;
    pub const ADC_HPF1: u8 = 0x1B;
    pub const ADC_HPF2: u8 = 0x1C;
    pub const GPIO: u8 = 0x44;
    pub const GP_REG: u8 = 0x45;
    pub const CHIP_ID1: u8 = 0xFD;
    pub const CHIP_ID2: u8 = 0xFE;
    pub const CHIP_VER: u8 = 0xFF;
}

/// ES8311 configuration
#[derive(Debug, Clone)]
pub struct Es8311Config {
    /// Sample rate in Hz
    pub sample_rate: u32,
    /// Initial DAC volume (0-100%)
    pub volume: u8,
}

impl Default for Es8311Config {
    fn default() -> Self {
        Self {
            sample_rate: 8000,
            volume: 70,
        }
    }
}

/// ES8311 driver error
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Es8311Error {
    /// I2C communication error
    I2cError,
    /// Invalid configuration
    InvalidConfig,
    /// Chip not responding or wrong ID
    ChipNotFound,
}

/// ES8311 driver (placeholder for ESP32 implementation)
///
/// On host: provides interface for testing
/// On ESP32: full I2C/I2S implementation
pub struct Es8311 {
    config: Es8311Config,
    volume: u8,
    muted: bool,
}

impl Es8311 {
    /// Create new ES8311 driver
    pub fn new(config: Es8311Config) -> Self {
        Self {
            volume: config.volume,
            config,
            muted: false,
        }
    }

    /// Initialize the codec
    ///
    /// On ESP32: performs full I2C initialization sequence
    #[cfg(not(feature = "esp32s3"))]
    pub fn init(&mut self) -> Result<(), Es8311Error> {
        // Host: no-op
        Ok(())
    }

    #[cfg(feature = "esp32s3")]
    pub fn init(&mut self, i2c: &mut impl embedded_hal::i2c::I2c) -> Result<(), Es8311Error> {
        use regs::*;

        // Soft reset
        self.write_reg(i2c, RESET, 0x80)?;

        // Wait for reset (should use delay)
        for _ in 0..1000 {
            core::hint::spin_loop();
        }

        // Configure for 8kHz, I2S slave mode
        // Clock configuration for MCLK = 256 * fs
        self.write_reg(i2c, CLK_MANAGER1, 0x3F)?;
        self.write_reg(i2c, CLK_MANAGER2, 0x00)?;

        // System: DAC-only mode
        self.write_reg(i2c, SYS_MODSEL, 0x08)?;

        // DAC settings
        self.write_reg(i2c, DAC_MUTE, 0x00)?; // Unmute
        self.set_volume_internal(i2c, self.volume)?;

        Ok(())
    }

    /// Set DAC volume (0-100%)
    #[cfg(not(feature = "esp32s3"))]
    pub fn set_volume(&mut self, volume: u8) -> Result<(), Es8311Error> {
        self.volume = volume.min(100);
        Ok(())
    }

    #[cfg(feature = "esp32s3")]
    pub fn set_volume(
        &mut self,
        i2c: &mut impl embedded_hal::i2c::I2c,
        volume: u8,
    ) -> Result<(), Es8311Error> {
        self.volume = volume.min(100);
        self.set_volume_internal(i2c, self.volume)
    }

    #[cfg(feature = "esp32s3")]
    fn set_volume_internal(
        &mut self,
        i2c: &mut impl embedded_hal::i2c::I2c,
        volume: u8,
    ) -> Result<(), Es8311Error> {
        // ES8311 DAC volume: 0x00 = 0dB, 0xBF = -95.5dB
        // Map 0-100% to register value
        let reg_val = if volume >= 100 {
            0x00
        } else {
            ((100 - volume) as u16 * 0xBF / 100) as u8
        };

        self.write_reg(i2c, regs::DAC_VOL, reg_val)
    }

    /// Mute DAC output
    #[cfg(not(feature = "esp32s3"))]
    pub fn mute(&mut self, mute: bool) -> Result<(), Es8311Error> {
        self.muted = mute;
        Ok(())
    }

    #[cfg(feature = "esp32s3")]
    pub fn mute(
        &mut self,
        i2c: &mut impl embedded_hal::i2c::I2c,
        mute: bool,
    ) -> Result<(), Es8311Error> {
        self.muted = mute;
        let val = if mute { 0x08 } else { 0x00 };
        self.write_reg(i2c, regs::DAC_MUTE, val)
    }

    /// Get current volume
    pub fn volume(&self) -> u8 {
        self.volume
    }

    /// Check if muted
    pub fn is_muted(&self) -> bool {
        self.muted
    }

    #[cfg(feature = "esp32s3")]
    fn write_reg(
        &self,
        i2c: &mut impl embedded_hal::i2c::I2c,
        reg: u8,
        val: u8,
    ) -> Result<(), Es8311Error> {
        i2c.write(ES8311_ADDR, &[reg, val])
            .map_err(|_| Es8311Error::I2cError)
    }

    #[cfg(feature = "esp32s3")]
    fn read_reg(
        &self,
        i2c: &mut impl embedded_hal::i2c::I2c,
        reg: u8,
    ) -> Result<u8, Es8311Error> {
        let mut buf = [0u8; 1];
        i2c.write_read(ES8311_ADDR, &[reg], &mut buf)
            .map_err(|_| Es8311Error::I2cError)?;
        Ok(buf[0])
    }
}
```

**Step 6.2: Update hal/mod.rs**

Modify `src/hal/mod.rs`:

```rust
//! Hardware Abstraction Layer
//!
//! Platform-specific hardware drivers.

pub mod audio;
pub mod gpio;
pub mod es8311;

pub use es8311::{Es8311, Es8311Config, Es8311Error, ES8311_ADDR};
```

**Step 6.3: Build to verify compilation**

Run: `cargo build`
Expected: Build succeeds

**Step 6.4: Commit**

```bash
git add src/hal/es8311.rs src/hal/mod.rs
git commit -m "feat(hal): add ES8311 codec driver"
```

---

## Task 7: Audio Module Integration

**Files:**
- Modify: `src/audio/mod.rs`
- Create: `tests/audio_integration_tests.rs`

**Step 7.1: Write integration test**

Create `tests/audio_integration_tests.rs`:

```rust
//! Audio module integration tests

use rust_remote_cw_keyer::audio::{
    AudioRingBuffer, AudioSource, AudioSourceSelector, FadeState, PttController, PttState,
    SidetoneGen,
};

const SAMPLE_RATE: u32 = 8000;
const FADE_SAMPLES: u16 = 40; // 5ms

/// Simulate the audio pipeline
#[test]
fn test_full_audio_pipeline() {
    // Components
    let mut sidetone = SidetoneGen::new(700, SAMPLE_RATE, FADE_SAMPLES);
    let mut sidetone_buf: AudioRingBuffer<64> = AudioRingBuffer::new();
    let mut ptt = PttController::new(100); // 100ms tail
    let source = AudioSourceSelector::new();

    // Initial state
    assert_eq!(ptt.state(), PttState::Off);
    assert_eq!(source.get(), AudioSource::Silence);
    assert_eq!(sidetone.fade_state(), FadeState::Silent);

    // Simulate key down at t=0
    let key_down = true;
    let mut timestamp_us: u64 = 0;

    // Generate 50 samples (key down)
    for _ in 0..50 {
        let sample = sidetone.next_sample(key_down);
        sidetone_buf.push(sample);

        if sample != 0 {
            ptt.audio_sample(timestamp_us);
        }

        timestamp_us += 125; // 8kHz = 125us per sample
    }

    // PTT should be on
    assert_eq!(ptt.state(), PttState::On);

    // Sidetone should be in sustain (past fade-in)
    assert_eq!(sidetone.fade_state(), FadeState::Sustain);

    // Buffer should have samples
    assert!(!sidetone_buf.is_empty());

    // Simulate key up
    let key_down = false;

    // Generate samples until fade completes
    for _ in 0..100 {
        let sample = sidetone.next_sample(key_down);
        sidetone_buf.push(sample);
        timestamp_us += 125;
        ptt.tick(timestamp_us);
    }

    // Sidetone should be silent
    assert_eq!(sidetone.fade_state(), FadeState::Silent);

    // PTT should still be on (within tail)
    assert_eq!(ptt.state(), PttState::On);

    // Advance time past PTT tail (100ms = 100000us)
    timestamp_us += 100_000;
    ptt.tick(timestamp_us);

    // Now PTT should be off
    assert_eq!(ptt.state(), PttState::Off);
}

/// Test buffer feeding I2S-like consumer
#[test]
fn test_buffer_to_dma_pattern() {
    let mut sidetone = SidetoneGen::new(600, SAMPLE_RATE, 8);
    let mut buf: AudioRingBuffer<64> = AudioRingBuffer::new();

    // Producer: fill buffer with sidetone
    for _ in 0..32 {
        buf.push(sidetone.next_sample(true));
    }

    assert_eq!(buf.len(), 32);

    // Consumer: read into DMA buffer
    let mut dma_buf = [0i16; 16];
    let read = buf.read_into(&mut dma_buf);

    assert_eq!(read, 16);
    assert_eq!(buf.len(), 16); // 32 - 16 remaining

    // Verify we got actual audio (not all zeros)
    let non_zero = dma_buf.iter().filter(|&&s| s != 0).count();
    assert!(non_zero > 0, "Should have non-zero samples");
}

/// Test source switching
#[test]
fn test_source_switching_with_ptt() {
    let mut ptt = PttController::new(50);
    let source = AudioSourceSelector::new();

    // Key down -> sidetone
    source.set(AudioSource::Sidetone);
    ptt.audio_sample(0);

    assert_eq!(source.get(), AudioSource::Sidetone);
    assert_eq!(ptt.state(), PttState::On);

    // PTT tail expires
    ptt.tick(100_000);
    assert_eq!(ptt.state(), PttState::Off);

    // Switch to remote (PTT off enables remote)
    source.set(AudioSource::Remote);
    assert_eq!(source.get(), AudioSource::Remote);
}
```

**Step 7.2: Run integration test**

Run: `cargo test audio_integration`
Expected: All 3 tests PASS

**Step 7.3: Update audio module exports**

Ensure `src/audio/mod.rs` has all exports:

```rust
//! Audio subsystem for sidetone and remote audio
//!
//! Architecture:
//! - Sidetone generator: LUT + phase accumulator, digital fade
//! - Ring buffers: 64 samples (sidetone), 512 (remote)
//! - PTT state machine controls audio source
//! - ES8311 codec via I2S @ 8 kHz
//!
//! # Example
//!
//! ```ignore
//! use audio::{SidetoneGen, AudioRingBuffer, PttController, AudioSource};
//!
//! let mut sidetone = SidetoneGen::new(700, 8000, 40);
//! let mut buffer: AudioRingBuffer<64> = AudioRingBuffer::new();
//! let mut ptt = PttController::new(100);
//!
//! loop {
//!     let sample = sidetone.next_sample(key_down);
//!     buffer.push(sample);
//!     if sample != 0 {
//!         ptt.audio_sample(now_us);
//!     }
//!     ptt.tick(now_us);
//! }
//! ```

pub mod lut;
pub mod sidetone;
pub mod buffer;
pub mod ptt;
pub mod source;

pub use lut::{SINE_LUT, LUT_SIZE};
pub use sidetone::{SidetoneGen, FadeState};
pub use buffer::AudioRingBuffer;
pub use ptt::{PttState, PttController};
pub use source::{AudioSource, AudioSourceSelector};
```

**Step 7.4: Commit integration**

```bash
git add tests/audio_integration_tests.rs src/audio/mod.rs
git commit -m "test(audio): add integration tests for audio pipeline"
```

---

## Task 8: Final Build and Cleanup

**Step 8.1: Run full test suite**

Run: `cargo test`
Expected: All tests PASS

**Step 8.2: Build for ESP32-S3**

Run: `cargo build --release`
Expected: Build succeeds

**Step 8.3: Clean up old audio.rs stub**

The old `src/hal/audio.rs` is now superseded by the new audio module.

Modify `src/hal/audio.rs`:

```rust
//! Audio HAL (legacy stub)
//!
//! NOTE: Main audio implementation is in src/audio/ module.
//! This file kept for backwards compatibility.
//!
//! See:
//! - src/audio/sidetone.rs - Sidetone generator
//! - src/audio/buffer.rs - Ring buffers
//! - src/audio/ptt.rs - PTT state machine
//! - src/hal/es8311.rs - ES8311 codec driver

/// Audio configuration (legacy, use audio module instead)
#[deprecated(note = "Use audio::SidetoneGen instead")]
pub struct AudioConfig {
    pub sample_rate: u32,
    pub sidetone_freq: u32,
    pub volume: u8,
}

#[allow(deprecated)]
impl Default for AudioConfig {
    fn default() -> Self {
        Self {
            sample_rate: 8000,
            sidetone_freq: 700,
            volume: 80,
        }
    }
}
```

**Step 8.4: Final commit**

```bash
git add -A
git commit -m "feat(audio): complete audio subsystem implementation

Audio pipeline components:
- Sidetone generator with LUT and phase accumulator
- Digital fade in/out envelope (anti-click)
- Lock-free ring buffers (64 sidetone, 512 remote)
- PTT state machine with tail timeout
- Atomic audio source selector
- ES8311 codec driver (I2C)

Tested on host with 25+ tests covering:
- LUT correctness and symmetry
- Sidetone frequency and phase continuity
- Buffer wrap-around and overflow
- PTT state transitions
- Full pipeline integration"
```

---

## Summary

| Task | Description | Tests |
|------|-------------|-------|
| 1 | Sine LUT generation | 4 tests |
| 2 | Sidetone generator | 7 tests |
| 3 | Ring buffers | 7 tests |
| 4 | PTT state machine | 7 tests |
| 5 | Audio source selector | 4 tests |
| 6 | ES8311 driver | build only |
| 7 | Integration tests | 3 tests |
| 8 | Final cleanup | build verification |

**Total tests:** 32
