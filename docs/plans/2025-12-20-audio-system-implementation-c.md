# Audio System Implementation - C (ESP-IDF)

**Date**: 2025-12-21
**Status**: Implemented
**Language**: C (ESP-IDF)
**Based on**: 2025-12-20-audio-system-design.md (Rust design)

## Overview

Audio subsystem implementation in C following the original Rust design. The system provides sidetone generation with LUT + phase accumulator, digital fade envelope for click-free keying, and PTT state machine with tail timeout.

## Implementation Summary

### Files Created/Modified

| File | Description |
|------|-------------|
| `components/keyer_audio/include/sidetone.h` | Sidetone generator header |
| `components/keyer_audio/src/sidetone.c` | Phase accumulator + LUT implementation |
| `components/keyer_audio/src/sidetone_lut.c` | Pre-computed 256-entry sine table |
| `components/keyer_audio/include/ptt.h` | PTT controller header |
| `components/keyer_audio/src/ptt.c` | PTT state machine with tail timeout |
| `test_host/test_sidetone.c` | Host unit tests |

## Sine Lookup Table

### Header (sidetone.h)

```c
/** LUT size (must be power of 2) */
#define SINE_LUT_SIZE 256

/** Pre-computed 256-entry sine LUT (signed 16-bit, full scale) */
extern const int16_t SINE_LUT[SINE_LUT_SIZE];
```

### LUT Generation (sidetone_lut.c)

```c
/* Pre-computed sine table covering one full cycle
 * Values: i16 range (-32767 to +32767)
 * Index mapping: 0=0deg, 64=90deg, 128=180deg, 192=270deg
 */
const int16_t SINE_LUT[SINE_LUT_SIZE] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602,
    // ... 256 entries total ...
};
```

## Fade Envelope

### States

```c
typedef enum {
    FADE_SILENT = 0,   /* No output */
    FADE_IN = 1,       /* Ramping up */
    FADE_SUSTAIN = 2,  /* Full output */
    FADE_OUT = 3,      /* Ramping down */
} fade_state_t;
```

### State Machine

```
      key_down           key_down
         │                   │
         ▼                   │
    ┌─────────┐         ┌────┴────┐
    │ SILENT  │────────►│ FADE_IN │
    └─────────┘         └────┬────┘
         ▲                   │ complete
         │                   ▼
    ┌────┴────┐         ┌────────┐
    │FADE_OUT │◄────────│SUSTAIN │
    └─────────┘ key_up  └────────┘
```

## Sidetone Generator

### Structure

```c
typedef struct {
    uint32_t phase;        /* Current phase (32-bit fixed point) */
    uint32_t phase_inc;    /* Phase increment per sample */
    fade_state_t fade_state;
    uint16_t fade_pos;     /* Current position in fade ramp */
    uint16_t fade_len;     /* Fade ramp length in samples */
    uint32_t sample_rate;  /* Sample rate in Hz */
} sidetone_gen_t;
```

### Phase Accumulator Math

```c
/* Frequency control via phase increment:
 * phase_inc = (freq_hz * 2^32) / sample_rate
 *
 * Upper 8 bits of phase select LUT index (0-255)
 */
gen->phase_inc = (uint32_t)(((uint64_t)freq_hz << 32) / sample_rate);
```

### Sample Generation

```c
int16_t sidetone_next_sample(sidetone_gen_t *gen, bool key_down) {
    /* Update fade state machine */
    // ... state transitions ...

    if (gen->fade_state == FADE_SILENT) {
        return 0;
    }

    /* Get sine sample from LUT (upper 8 bits = index) */
    uint8_t lut_idx = (uint8_t)(gen->phase >> 24);
    int32_t raw_sample = SINE_LUT[lut_idx];

    /* Advance phase (wraps naturally at 32-bit boundary) */
    gen->phase += gen->phase_inc;

    /* Apply envelope (0-32767 scale) */
    int32_t envelope;
    switch (gen->fade_state) {
        case FADE_IN:
            envelope = ((int32_t)gen->fade_pos * 32767) / gen->fade_len;
            break;
        case FADE_OUT:
            envelope = ((int32_t)(gen->fade_len - gen->fade_pos) * 32767) / gen->fade_len;
            break;
        default:
            envelope = 32767;
            break;
    }

    return (int16_t)((raw_sample * envelope) >> 15);
}
```

## PTT Controller

### Structure

```c
typedef struct {
    ptt_state_t state;       /* Current PTT state */
    uint64_t tail_us;        /* Tail timeout in microseconds */
    uint64_t last_audio_us;  /* Timestamp of last audio activity */
    bool audio_active;       /* Audio currently active */
} ptt_controller_t;
```

### API

```c
/* Initialization */
void ptt_init(ptt_controller_t *ptt, uint32_t tail_ms);

/* Called when audio output is active */
void ptt_audio_sample(ptt_controller_t *ptt, uint64_t timestamp_us);

/* Periodic update (check tail timeout) */
void ptt_tick(ptt_controller_t *ptt, uint64_t timestamp_us);

/* Force PTT off (fault recovery) */
void ptt_force_off(ptt_controller_t *ptt);
```

### Timing

```
Audio Start     Audio Stop          Tail Expires
    │               │                    │
    ▼               ▼                    ▼
    ┌───────────────┬────────────────────┐
    │    PTT ON     │    TAIL PERIOD     │ PTT OFF
    └───────────────┴────────────────────┘
         Active keying     No keying
```

## Unit Tests

All sidetone tests pass:

1. `test_sidetone_init` - Initialization and defaults
2. `test_sidetone_keying` - Key down/up behavior
3. `test_sidetone_fade` - Fade in/out transitions

## Compliance with ARCHITECTURE.md

- **No heap allocation** - All state in static structs
- **RT-safe** - O(1) operations, no blocking
- **Deterministic** - Fixed LUT lookup, linear fade
- **Click-free** - Digital envelope prevents key clicks
- **Testable** - Unity tests run on host without hardware

## Default Values

```c
#define SIDETONE_FREQ_DEFAULT   700    /* Hz */
#define SIDETONE_SAMPLE_RATE    8000   /* Hz */
#define SIDETONE_FADE_SAMPLES   40     /* 5ms at 8kHz */
#define PTT_TAIL_DEFAULT_MS     100    /* ms */
```

## Future Work

1. **ES8311 Codec Driver** - I2C/I2S integration
2. **Audio Ring Buffer** - Lock-free buffer for I2S DMA
3. **Remote Audio** - WebSocket audio streaming
4. **Volume Control** - Atomic volume adjustment
