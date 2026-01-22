# Winkeyer Server Specification

**Date:** 2026-01-22
**Session:** build-20260122-winkeyer-server

## Overview

Implement a K1EL Winkeyer v3 protocol server over USB CDC that integrates with the existing iambic keyer infrastructure.

## Requirements

### Protocol
- **Version:** Winkeyer v3 (K1EL compatible)
- **Interface:** USB CDC (serial over USB)
- **Feature Set:** Core commands only (speed, sidetone, keying)

### Integration
- **Keyer Integration:** Use existing `keying_stream_t` and iambic FSM
- **Paddle Priority:** Local paddle takes priority over Winkeyer host transmission
- **Parameter Persistence:** Session only (no NVS persistence)

### Threading Model
- **Processing Core:** Core 1 (background/best-effort)
- **No RT Path Impact:** USB processing must not affect Core 0 RT task

## Winkeyer v3 Core Commands to Implement

| Cmd | Hex | Name | Description |
|-----|-----|------|-------------|
| 0 | 0x00 | Admin | Host open/close, echo, status |
| 1 | 0x01 | Sidetone Freq | Set sidetone frequency |
| 2 | 0x02 | Speed | Set WPM speed |
| 3 | 0x03 | Weighting | Set dit/dah weighting |
| 4 | 0x04 | PTT Lead/Tail | PTT timing (lead-in, hang time) |
| 5 | 0x05 | Speed Pot Setup | Speed pot range (N/A - no pot) |
| 6 | 0x06 | Pause | Pause/resume transmission |
| 7 | 0x07 | Get Speed Pot | Return current speed |
| 8 | 0x08 | Backspace | Delete last character |
| 9 | 0x09 | Pin Config | I/O pin configuration |
| 10 | 0x0A | Clear Buffer | Clear transmit buffer |
| 11 | 0x0B | Key Immediate | Immediate key down/up |
| 12 | 0x0C | HSCW Speed | High-speed CW mode |
| 13 | 0x0D | Farnsworth | Farnsworth spacing |
| 14 | 0x0E | WinKey Mode | Mode register |
| 15 | 0x0F | Load Defaults | Reset to defaults |

### Admin Sub-Commands (Cmd 0)
| Sub | Name | Description |
|-----|------|-------------|
| 0 | Calibrate | Nop |
| 1 | Reset | Reset to power-up state |
| 2 | Host Open | Open host session, return version |
| 3 | Host Close | Close host session |
| 4 | Echo Test | Echo byte back |
| 5 | Paddle A2D | Return paddle state |
| 6 | Speed A2D | Return speed pot value |
| 7 | Get Values | Return all settings |
| 8 | Reserved | - |
| 9 | Get Cal | Return calibration |
| 10 | Set WK1 Mode | Winkeyer 1 compatibility |
| 11 | Set WK2 Mode | Winkeyer 2 compatibility |
| 12 | Dump EEPROM | Dump settings (stub) |
| 13 | Load EEPROM | Load settings (stub) |
| 14 | Send Msg | Send stored message (stub) |
| 15 | Load X1MODE | Extended mode 1 |
| 16 | Firmware Update | Nop |
| 17 | Set Low Baud | 1200 baud (nop) |
| 18 | Set High Baud | 9600 baud (nop) |
| 19 | Set K1EL Mode | Set K1EL extensions |
| 20 | Set WK3 Mode | Winkeyer 3 mode |

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      USB CDC (TinyUSB)                       │
│                         Core 1                               │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   Winkeyer Protocol Parser                   │
│  - Command state machine                                     │
│  - Response builder                                          │
│  - Character buffer (for text transmission)                  │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Winkeyer Controller                       │
│  - Parameter management (speed, sidetone, etc.)              │
│  - Character-to-morse converter                              │
│  - Atomic config updates to RT task                          │
└─────────────────────────────────────────────────────────────┘
                              │
            ┌─────────────────┴─────────────────┐
            │ atomic_store (config)              │
            ▼                                    ▼
┌───────────────────────┐          ┌───────────────────────────┐
│   keyer_config_t      │          │    keying_stream_t        │
│   (shared atomics)    │          │    (lock-free ring)       │
└───────────────────────┘          └───────────────────────────┘
            │                                    │
            └─────────────────┬─────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      RT Task (Core 0)                        │
│  - Reads config atomically                                   │
│  - Polls paddle (takes priority)                             │
│  - Iambic FSM generates samples                              │
│  - Consumers (audio, TX) process stream                      │
└─────────────────────────────────────────────────────────────┘
```

## Component Design

### 1. `keyer_winkeyer` Component

**Files:**
- `include/winkeyer.h` - Public API
- `include/winkeyer_protocol.h` - Protocol constants
- `include/winkeyer_morse.h` - Character to morse table
- `src/winkeyer.c` - Main implementation
- `src/winkeyer_parser.c` - Protocol parser
- `src/winkeyer_morse.c` - Morse lookup

**Public API:**
```c
// Initialize winkeyer server
esp_err_t winkeyer_init(const winkeyer_config_t *config);

// Process USB data (called from USB task)
void winkeyer_process_rx(const uint8_t *data, size_t len);

// Get pending TX data for USB (returns bytes written)
size_t winkeyer_get_tx(uint8_t *buf, size_t max_len);

// Check if winkeyer wants to send morse
bool winkeyer_has_pending_text(void);

// Get next morse element for RT task (dit/dah/space)
// Returns false if nothing pending or paddle active
bool winkeyer_get_next_element(morse_element_t *element);
```

### 2. Morse Element Queue

For text-to-CW transmission, use a lock-free queue:
- Producer: Winkeyer controller (Core 1)
- Consumer: RT task (Core 0) when no paddle activity

```c
typedef enum {
    MORSE_ELEMENT_DIT,
    MORSE_ELEMENT_DAH,
    MORSE_ELEMENT_CHAR_SPACE,
    MORSE_ELEMENT_WORD_SPACE,
} morse_element_type_t;

typedef struct {
    morse_element_type_t type;
} morse_element_t;
```

### 3. Integration Points

**With existing USB CDC (`keyer_usb`):**
- Register winkeyer as a CDC handler
- Route serial data to `winkeyer_process_rx()`
- Poll `winkeyer_get_tx()` for responses

**With RT task (`rt_task.c`):**
- When paddle inactive, check `winkeyer_has_pending_text()`
- Call `winkeyer_get_next_element()` to get morse elements
- Generate keying samples for element

**With config (`keyer_config`):**
- Winkeyer updates config via atomic stores
- RT task reads config via atomic loads

## Status Byte

Winkeyer sends status byte (0xC0 | flags) on changes:

| Bit | Name | Description |
|-----|------|-------------|
| 0 | XOFF | Buffer 2/3 full |
| 1 | BREAKIN | Paddle breakin detected |
| 2 | BUSY | Sending characters |
| 3 | - | Reserved |
| 4 | - | Reserved |
| 5 | - | Reserved |

## Constraints

1. **No heap allocation** in morse element queue
2. **No blocking** in winkeyer_process_rx()
3. **Atomic-only** communication with RT task
4. **Local paddle priority** - interrupt winkeyer transmission

## Test Strategy

- Host tests for protocol parser (no hardware)
- Host tests for morse lookup table
- Integration test with USB loopback
- Compatibility test with N1MM, fldigi, etc.

## Out of Scope (Phase 1)

- Message memories (commands 18-22)
- Buffered speed change
- HSCW mode
- Prosign handling
- Serial number generation
