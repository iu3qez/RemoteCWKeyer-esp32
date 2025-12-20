# Audio System Design

**Data**: 2025-12-20
**Stato**: Approvato

## Overview

Sistema audio per sidetone locale e audio remoto, basato su ES8311 codec via I2S a 8 kHz. Architettura con buffer separati per sidetone (bassa latenza) e audio remoto (jitter buffer). PA sempre abilitato, switching automatico basato su stato PTT.

## Hardware

### Componenti

| Chip | Funzione | Bus |
|------|----------|-----|
| ES8311 | Mono DAC + ADC codec | I2S (audio) + I2C (config) |
| TCA95xx | GPIO expander | I2C |
| PA | Power Amplifier | Enable via TCA95xx |

### Connessioni ES8311

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

- TCA95xx GPIO imposta PA enable all'avvio
- **Sempre ON** durante funzionamento normale
- Zero I2C durante operazione audio

## Architettura

### Flusso Audio

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

Sidetone e audio remoto si escludono a vicenda. Selezione basata su stato PTT.

## PTT State Machine

### Logica

```
        first_audio_sample         last_audio_sample + tail_ms
               │                              │
               ▼                              ▼
PTT OFF ───────▶ PTT ON ──────────────────────▶ PTT OFF
                    │                              │
                    │                              ▼
                    │                    AudioSource::Remote
                    │                    (se buffer ha dati)
                    ▼
              AudioSource::Sidetone
```

### Trigger

| Evento | Azione |
|--------|--------|
| Primo sample audio locale | PTT ON, AudioSource::Sidetone |
| Ogni sample audio locale | Reset tail timer |
| Tail timeout (ptt_tail_ms) | PTT OFF, AudioSource::Remote (se disponibile) |

**Nota**: Il trigger è il sample audio emesso, non key_up. La memoria iambic può generare audio dopo key_up.

### Parametri

Da `parameters.yaml`:
- `ptt_tail_ms`: 50-500ms (default 100ms)

## Sidetone Generator

### Generazione Onda

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

### Frequenza

Phase increment calcolato da frequenza sidetone:

```rust
/// Calculate phase increment for target frequency
/// phase_inc = (freq * 2^32) / sample_rate
fn calc_phase_inc(freq_hz: u32, sample_rate: u32) -> u32 {
    ((freq_hz as u64 * (1u64 << 32)) / sample_rate as u64) as u32
}

// Esempio: 700 Hz @ 8 kHz
// phase_inc = (700 * 4294967296) / 8000 = 375809638
```

### Fade In/Out (Anti-Click)

**Rampa lineare digitale**:

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

### Parametri

Da `parameters.yaml`:
- `sidetone_freq_hz`: 400-800 Hz (default 600)
- `sidetone_volume`: 1-100% (controllo ES8311)
- `fade_duration_ms`: 1-10ms (default 5)

Fade length in samples: `fade_duration_ms * 8` (a 8 kHz)

## ES8311 Configuration

### Inizializzazione

Sequenza I2C all'avvio:

1. Reset software
2. Clock configuration (MCLK/BCLK ratio)
3. Sample rate = 8 kHz
4. DAC mode (no ADC per ora)
5. Volume iniziale da CONFIG
6. Power on DAC

### Volume Control

Volume via registro ES8311 (non digitale):

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

Cambio volume via I2C (~50-100us) accettabile - avviene solo su richiesta utente, mai durante keying.

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

- Producer: RT task (Core 0), genera da KeyingStream
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

Double buffer, callback su completamento:

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

| Component | Core | Priority | Descrizione |
|-----------|------|----------|-------------|
| Sidetone Gen | 0 | RT | Genera samples da KeyingStream |
| PTT State | 0 | RT | Gestisce PTT e source switching |
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

Tutto inline, zero context switch, < 100us.

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
│   └── audio.rs         # (esistente, da rimuovere/sostituire)
```

## Parametri (da parameters.yaml)

Già definiti:
- `sidetone_freq_hz`: u16, 400-800, default 600
- `sidetone_volume`: u8, 1-100, default 70
- `fade_duration_ms`: u8, 1-10, default 5
- `ptt_tail_ms`: u32, 50-500, default 100

Da aggiungere:
- `sidetone_buf_size`: usize, 32-64, default 64
- `remote_buf_size`: usize, 256-512, default 512

## Error Handling

| Errore | Azione |
|--------|--------|
| I2S DMA underrun | Fill con silenzio, log warning |
| Remote buffer underrun | Switch a silence, continua |
| ES8311 I2C failure | Log error, retry, non blocca RT |
| Sidetone buffer full | Drop oldest samples |

**Nessun FAULT per audio** - audio non è safety-critical come TX timing. Silenzio è accettabile.

## Testing

### Unit Tests (host)

- SidetoneGen: frequenza corretta, fade timing
- Buffer: ring buffer wrap-around, underrun handling
- PTT: state transitions, tail timing

### Integration Tests (target)

- I2S output verification (oscilloscope/analyzer)
- Latency measurement (key → audio)
- Fade smoothness (no clicks)

## Future Extensions

- **ADC path**: ES8311 ha ADC per mic input (contest keyer)
- **Audio mixing**: se necessario in futuro
- **Sample rate switching**: attualmente fisso a 8 kHz
