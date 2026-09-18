# Audio System Design

**Date**: 2025-12-20
**Status**: Approved

## Overview

Audio system for local sidetone and remote audio, based on an ES8311 codec via I2S at 8 kHz. Architecture with separate buffers for sidetone (low latency) and remote audio (jitter buffer). PA always enabled, automatic switching based on PTT state.

## Hardware

### Components

| Chip | Function | Bus |
|------|----------|-----|
| ES8311 | Mono DAC + ADC codec | I2S (audio) + I2C (config) |
| TCA95xx | GPIO expander | I2C |
| PA | Power Amplifier | Enable via TCA95xx |

### ES8311 Connections

```
ESP32-S3                    ES8311
─────────                   ──────
GPIO xx  ──── BCLK ────────▶ SCLK
GPIO xx  ──── WS ──────────▶ LRCK
GPIO xx  ──── DOUT ────────▶ SDIN
GPIO xx  ──── I2C SDA ◀────▶ SDA
GPIO xx  ──── I2C SCL ─────▶ SCL
```

### PA Enable

- TCA95xx GPIO sets PA enable at startup
- **Always ON** during normal operation
- Zero I2C during audio operation

## Architecture

### Audio Flow

```
┌──────────────────┐      ┌──────────────────┐
│ Sidetone Gen     │      │ Remote Audio RX  │
│ (RT, Core 0)     │      │ (BE, Core 1)     │
│                  │      │                  │
│ - LUT + phase    │      │ - Network RX     │
│ - Fade in/out    │      │ - Decode         │
└────────┬─────────┘      └────────┬─────────┘
         │                         │
         ▼                         ▼
┌──────────────────┐      ┌──────────────────┐
│ Ring Buffer      │      │ Ring Buffer      │
│ 32-64 samples    │      │ 256-512 samples  │
│ (4-8ms latency)  │      │ (32-64ms jitter) │
└────────┬─────────┘      └────────┬─────────┘
         │                         │
         └───────────┬─────────────┘
                     │
              ┌──────▼──────┐
              │   Select    │
              │ (AtomicU8)  │
              └──────┬──────┘
                     │
              ┌──────▼──────┐
              │  I2S DMA    │
              │  (8 kHz)    │
              └──────┬──────┘
                     │
              ┌──────▼──────┐
              │   ES8311    │
              └──────┬──────┘
                     │
              ┌──────▼──────┐
              │     PA      │
              │ (always on) │
              └─────────────┘
```

### Source Selection

```rust
#[repr(u8)]
pub enum AudioSource {
    Silence = 0,
    Sidetone = 1,
    Remote = 2,
}

pub static AUDIO_SOURCE: AtomicU8 = AtomicU8::new(0);
```

Sidetone and remote audio are mutually exclusive. Selection is based on PTT state.

## PTT State Machine

### Logic

```
        first_audio_sample         last_audio_sample + tail_ms
               │                              │
               ▼                              ▼
PTT OFF ───────▶ PTT ON ──────────────────────▶ PTT OFF
                    │                              │
                    │                              ▼
                    │                    AudioSource::Remote
                    │                    (if buffer has data)
                    ▼
              AudioSource::Sidetone
```

### Trigger

| Event | Action |
|--------|--------|
| First local audio sample | PTT ON, AudioSource::Sidetone |
| Every local audio sample | Reset tail timer |
| Tail timeout (ptt_tail_ms) | PTT OFF, AudioSource::Remote (if available) |

**Note**: the trigger is the audio sample emitted, not key_up. The iambic memory can generate audio after key_up.

### Parameters

From `parameters.yaml`:
- `ptt_tail_ms`: 50-500ms (default 100ms)

## Sidetone Generator

### Wave Generation

**Lookup Table + Phase Accumulator**:

```rust
/// Sine wave lookup table (256 entries, i16)
static SINE_LUT: [i16; 256] = [ /* pre-computed */ ];

pub struct SidetoneGen {
    phase: u32,           // Fixed-point phase accumulator
    phase_inc: u32,       // Increment per sample (freq dependent)
    fade_state: FadeState,
    fade_pos: u16,        // Current fade position
    fade_len: u16,        // Fade length in samples
}

impl SidetoneGen {
    /// Generate next sample
    #[inline]
    pub fn next_sample(&mut self, key_down: bool) -> i16 {
        // Update fade state
        let amplitude = self.update_fade(key_down);

        // LUT lookup with phase accumulator
        let idx = (self.phase >> 24) as usize; // Top 8 bits = index
        self.phase = self.phase.wrapping_add(self.phase_inc);

        // Scale by fade amplitude
        let sample = SINE_LUT[idx];
        ((sample as i32 * amplitude as i32) >> 16) as i16
    }
}
```

### Frequency

Phase increment computed from sidetone frequency:

```rust
/// Calculate phase increment for target frequency
/// phase_inc = (freq * 2^32) / sample_rate
fn calc_phase_inc(freq_hz: u32, sample_rate: u32) -> u32 {
    ((freq_hz as u64 * (1u64 << 32)) / sample_rate as u64) as u32
}

// Example: 700 Hz @ 8 kHz
// phase_inc = (700 * 4294967296) / 8000 = 375809638
```

### Fade In/Out (Anti-Click)

**Digital linear ramp**:

```rust
pub enum FadeState {
    Silent,     // Output = 0
    FadeIn,     // Ramping up
    Sustain,    // Full amplitude
    FadeOut,    // Ramping down
}

impl SidetoneGen {
    fn update_fade(&mut self, key_down: bool) -> u16 {
        match (&self.fade_state, key_down) {
            (FadeState::Silent, true) => {
                self.fade_state = FadeState::FadeIn;
                self.fade_pos = 0;
                0
            }
            (FadeState::FadeIn, true) => {
                self.fade_pos += 1;
                if self.fade_pos >= self.fade_len {
                    self.fade_state = FadeState::Sustain;
                    0xFFFF
                } else {
                    (self.fade_pos as u32 * 0xFFFF / self.fade_len as u32) as u16
                }
            }
            (FadeState::Sustain, true) => 0xFFFF,
            (FadeState::Sustain, false) | (FadeState::FadeIn, false) => {
                self.fade_state = FadeState::FadeOut;
                self.fade_pos = self.fade_len;
                0xFFFF
            }
            (FadeState::FadeOut, _) => {
                if self.fade_pos == 0 {
                    self.fade_state = FadeState::Silent;
                    0
                } else {
                    self.fade_pos -= 1;
                    (self.fade_pos as u32 * 0xFFFF / self.fade_len as u32) as u16
                }
            }
            (FadeState::Silent, false) => 0,
        }
    }
}
```

### Parameters

From `parameters.yaml`:
- `sidetone_freq_hz`: 400-800 Hz (default 600)
- `sidetone_volume`: 1-100% (ES8311 control)
- `fade_duration_ms`: 1-10ms (default 5)

Fade length in samples: `fade_duration_ms * 8` (at 8 kHz)

## ES8311 Configuration

### Initialization

I2C sequence at startup:

1. Software reset
2. Clock configuration (MCLK/BCLK ratio)
3. Sample rate = 8 kHz
4. DAC mode (no ADC for now)
5. Initial volume from CONFIG
6. Power on DAC

### Volume Control

Volume via ES8311 register (not digital):

```rust
/// Set ES8311 DAC volume (0-100%)
pub fn set_volume(i2c: &mut I2C, volume_pct: u8) -> Result<(), Error> {
    // ES8311 DAC volume: 0x00 = +0dB, 0xC0 = -96dB
    // Map 0-100% to register value
    let reg_val = if volume_pct >= 100 {
        0x00
    } else {
        ((100 - volume_pct) as u16 * 0xC0 / 100) as u8
    };

    i2c.write(ES8311_ADDR, &[REG_DAC_VOL, reg_val])
}
```

Volume change via I2C (~50-100us) is acceptable - it only happens on user request, never during keying.

## Buffer Management

### Sidetone Buffer

```rust
const SIDETONE_BUF_SIZE: usize = 64; // 8ms @ 8kHz

pub struct SidetoneBuffer {
    buf: [i16; SIDETONE_BUF_SIZE],
    write_idx: AtomicUsize,
    read_idx: AtomicUsize,
}
```

- Producer: RT task (Core 0), generates from KeyingStream
- Consumer: I2S DMA callback

### Remote Audio Buffer

```rust
const REMOTE_BUF_SIZE: usize = 512; // 64ms @ 8kHz

pub struct RemoteAudioBuffer {
    buf: [i16; REMOTE_BUF_SIZE],
    write_idx: AtomicUsize,
    read_idx: AtomicUsize,
}
```

- Producer: Network RX task (Core 1)
- Consumer: I2S DMA callback

### I2S DMA

Double buffer, callback on completion:

```rust
fn i2s_tx_callback(buf: &mut [i16]) {
    let source = AUDIO_SOURCE.load(Ordering::Relaxed);

    match source {
        0 => {
            // Silence
            buf.fill(0);
        }
        1 => {
            // Sidetone
            sidetone_buffer.read_into(buf);
        }
        2 => {
            // Remote
            if !remote_buffer.read_into(buf) {
                // Buffer underrun, fallback to silence
                buf.fill(0);
            }
        }
        _ => buf.fill(0),
    }
}
```

## Threading Model

| Component | Core | Priority | Description |
|-----------|------|----------|-------------|
| Sidetone Gen | 0 | RT | Generates samples from KeyingStream |
| PTT State | 0 | RT | Manages PTT and source switching |
| I2S DMA | - | ISR | Hardware callback |
| ES8311 Config | 1 | Low | Volume changes, init |
| Remote Audio RX | 1 | Medium | Network → buffer |

### RT Path (Core 0)

```
KeyingStream.tick()
    → SidetoneGen.next_sample()
    → SidetoneBuffer.push()
    → PTT.update()
```

All inline, zero context switch, < 100us.

## File Structure

```
src/
├── audio/
│   ├── mod.rs           # Re-exports, AudioSource enum
│   ├── sidetone.rs      # SidetoneGen, LUT, fade
│   ├── buffer.rs        # Ring buffers (sidetone + remote)
│   ├── ptt.rs           # PTT state machine
│   └── i2s.rs           # I2S DMA setup, callback
├── hal/
│   ├── es8311.rs        # ES8311 driver (I2C)
│   └── audio.rs         # (existing, to be removed/replaced)
```

## Parameters (from parameters.yaml)

Already defined:
- `sidetone_freq_hz`: u16, 400-800, default 600
- `sidetone_volume`: u8, 1-100, default 70
- `fade_duration_ms`: u8, 1-10, default 5
- `ptt_tail_ms`: u32, 50-500, default 100

To add:
- `sidetone_buf_size`: usize, 32-64, default 64
- `remote_buf_size`: usize, 256-512, default 512

## Error Handling

| Error | Action |
|--------|--------|
| I2S DMA underrun | Fill with silence, log warning |
| Remote buffer underrun | Switch to silence, continue |
| ES8311 I2C failure | Log error, retry, does not block RT |
| Sidetone buffer full | Drop oldest samples |

**No FAULT for audio** - audio is not safety-critical like TX timing. Silence is acceptable.

## Testing

### Unit Tests (host)

- SidetoneGen: correct frequency, fade timing
- Buffer: ring buffer wrap-around, underrun handling
- PTT: state transitions, tail timing

### Integration Tests (target)

- I2S output verification (oscilloscope/analyzer)
- Latency measurement (key → audio)
- Fade smoothness (no clicks)

## Future Extensions

- **ADC path**: ES8311 has an ADC for mic input (contest keyer)
- **Audio mixing**: if needed in the future
- **Sample rate switching**: currently fixed at 8 kHz
