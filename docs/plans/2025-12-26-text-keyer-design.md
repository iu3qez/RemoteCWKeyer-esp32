# Text Keyer Design

**Date**: 2025-12-26
**Status**: Draft
**Branch**: `text_keyer`

## Overview

Text-to-Morse keyer that converts text strings to CW and transmits them. Supports stored messages (memory slots) and free-form text input via console.

## Requirements

1. **Macro messages** - 8 memory slots saved in NVS, callable with `m1`-`m8`
2. **Keyboard keying** - Free-form text via `send <text>` command
3. **Paddle priority** - Paddle input immediately aborts text transmission
4. **Shared WPM** - Uses global `g_config.keyer.wpm` setting
5. **Prosigns** - Support for `<SK>`, `<AR>`, `<BT>`, `<KN>`, `<AS>`, `<SN>`
6. **Web GUI ready** - API designed for future web interface

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Core 1 (Background)                      │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────────┐  │
│  │   Console    │───▶│  Text Keyer  │───▶│  keying_stream_t │  │
│  │  "send CQ"   │    │   (producer) │    │   (lock-free)    │  │
│  │  "m1", "m2"  │    └──────────────┘    └────────┬─────────┘  │
│  └──────────────┘           │                     │             │
│                             │                     │             │
│  ┌──────────────┐           │                     │             │
│  │  NVS Memory  │───────────┘                     │             │
│  │  (8 slots)   │                                 │             │
│  └──────────────┘                                 │             │
└───────────────────────────────────────────────────│─────────────┘
                                                    │
┌───────────────────────────────────────────────────│─────────────┐
│                         Core 0 (RT)               ▼             │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────────┐  │
│  │  Paddle GPIO │───▶│  Iambic FSM  │───▶│  keying_stream_t │  │
│  │   (polling)  │    │  (producer)  │    │    (consumer)    │  │
│  └──────────────┘    └──────────────┘    └────────┬─────────┘  │
│         │                                         │             │
│         │ paddle_active ──────────────────────────│─────────┐   │
│         │    (atomic)                             ▼         │   │
│         │                                 ┌─────────────┐   │   │
│         └────────────────────────────────▶│ Audio + TX  │   │   │
│                                           │ (consumers) │   │   │
│                                           └─────────────┘   │   │
└─────────────────────────────────────────────────────────────────┘
```

### Key Principles

1. **Stream is the only interface** - Text keyer produces samples, consumers consume them
2. **Paddle abort via atomic** - `atomic_bool g_paddle_active` read by text keyer for immediate abort
3. **No mutex in RT path** - Text keyer uses mutex internally (Core 1), but stream is lock-free
4. **NVS for persistence** - 8 memory slots saved in flash

## Components

```
components/
├── keyer_morse/                    # NEW - shared morse table
│   ├── include/
│   │   └── morse_table.h           # Lookup table + prosigns
│   └── src/
│       └── morse_table.c
│
├── keyer_text/                     # NEW - text keyer
│   ├── include/
│   │   ├── text_keyer.h            # Public API
│   │   └── text_memory.h           # NVS memory slots
│   └── src/
│       ├── text_keyer.c            # State machine + timing
│       └── text_memory.c           # NVS load/save
│
└── keyer_console/                  # EXISTING - new commands
    └── src/
        └── cmd_text.c              # send, m1-m8, abort, mem
```

### Responsibilities

| Component | Responsibility |
|-----------|----------------|
| `keyer_morse` | Encode char→pattern, decode pattern→char, prosigns |
| `keyer_text` | State machine, timing, produce samples on stream |
| `text_memory` | Load/save 8 NVS slots, validation |
| `cmd_text` | Console commands: `send`, `m1`-`m8`, `abort`, `mem` |

### Dependencies

- `keyer_text` → `keyer_morse`, `keyer_core` (stream), `keyer_config` (wpm)
- `keyer_morse` → none (standalone)
- `keyer_console` → `keyer_text`, `keyer_morse`

## Morse Table API

```c
// morse_table.h

#define MORSE_MAX_PATTERN_LEN 8  // Longest: some prosigns

typedef struct {
    char character;           // 'A', '0', or '\x01' for prosigns
    const char *pattern;      // ".-", "-...", etc.
    const char *prosign_tag;  // NULL or "<SK>", "<AR>", etc.
} morse_entry_t;

// Encode: character → pattern
// Returns NULL if character not found
const char *morse_encode_char(char c);

// Encode prosign: "<SK>" → "...-.-"
// Returns NULL if prosign not found
const char *morse_encode_prosign(const char *tag);

// Decode: pattern → character (for decoder)
// Returns '\0' if pattern not found
char morse_decode_pattern(const char *pattern);

// Check if string starts with prosign tag
// Returns tag length if found, 0 otherwise
size_t morse_match_prosign(const char *text, const char **pattern_out);
```

### Supported Characters

| Group | Characters |
|-------|------------|
| Letters | A-Z (case insensitive) |
| Numbers | 0-9 |
| Punctuation | `.` `,` `?` `!` `/` `=` `+` `-` `@` |
| Prosigns | `<SK>` `<AR>` `<BT>` `<KN>` `<AS>` `<SN>` |

### Encoding Example

```
Input:  "CQ DE IU3QEZ <SK>"
Output: ["-.-.","--.-"," ","-..","."," ","..","..---","...--","--.-",".","--..","...-.-"]
                       ↑ space = word gap                                    ↑ prosign
```

## Text Keyer API

```c
// text_keyer.h

typedef enum {
    TEXT_KEYER_IDLE = 0,      // Ready for new text
    TEXT_KEYER_SENDING,       // Transmitting
    TEXT_KEYER_PAUSED,        // Paused (can resume)
    TEXT_KEYER_ABORTED        // Interrupted (by paddle or command)
} text_keyer_state_t;

typedef struct {
    keying_stream_t *stream;          // Output stream (non-owning)
    const atomic_bool *paddle_active; // Abort trigger (non-owning)
} text_keyer_config_t;

// Lifecycle
esp_err_t text_keyer_init(const text_keyer_config_t *config);

// Operations
esp_err_t text_keyer_send(const char *text);  // Start sending
void text_keyer_abort(void);                   // Stop immediately
void text_keyer_pause(void);                   // Pause (can resume)
void text_keyer_resume(void);                  // Resume after pause

// Status
text_keyer_state_t text_keyer_get_state(void);
void text_keyer_get_progress(size_t *sent, size_t *total);

// Must be called periodically from bg_task (~10ms)
void text_keyer_tick(int64_t now_us);
```

### State Machine

```
         send()           tick() complete
  ┌─────────────────┐    ┌─────────────────┐
  │                 ▼    │                 ▼
┌──────┐         ┌─────────┐          ┌──────┐
│ IDLE │◀────────│ SENDING │──────────│ IDLE │
└──────┘         └─────────┘          └──────┘
  ▲                │     │
  │    abort()     │     │ pause()
  │   or paddle    │     ▼
  │                │  ┌────────┐
  └────────────────┘  │ PAUSED │
         abort()      └────────┘
                          │ resume()
                          ▼
                      ┌─────────┐
                      │ SENDING │
                      └─────────┘
```

## Timing (PARIS Standard)

```c
// ITU standard timing
// WPM = 1200 / dit_ms  →  dit_us = 1,200,000 / WPM

static inline int64_t dit_duration_us(uint32_t wpm) {
    return 1200000 / wpm;
}

// Element durations (multiples of dit):
//   dit       = 1× dit
//   dah       = 3× dit
//   intra_gap = 1× dit (between elements in same character)
//   char_gap  = 3× dit (between characters)
//   word_gap  = 7× dit (between words / space)

// At 20 WPM:
//   dit       =  60,000 µs ( 60 ms)
//   dah       = 180,000 µs (180 ms)
//   char_gap  = 180,000 µs (180 ms)
//   word_gap  = 420,000 µs (420 ms)
```

### Internal State

```c
typedef enum {
    ELEMENT_DIT,
    ELEMENT_DAH,
    ELEMENT_INTRA_GAP,   // 1 dit - between elements in same character
    ELEMENT_CHAR_GAP,    // 3 dit - between characters
    ELEMENT_WORD_GAP,    // 7 dit - between words (space)
} element_type_t;

typedef struct {
    char *patterns;              // Array of encoded patterns
    size_t pattern_count;        // Number of patterns (characters)
    size_t current_pattern;      // Current pattern index
    size_t current_element;      // Element index within current pattern
    element_type_t element_type; // Current element type
    int64_t element_end_us;      // When current element should finish
    bool key_down;               // Current key state
} send_state_t;
```

### Sample Production

```c
static void emit_sample(bool key_down, int64_t now_us) {
    stream_sample_t sample = {
        .timestamp_us = now_us,
        .key_state = key_down ? KEY_STATE_DOWN : KEY_STATE_UP,
        .source = SAMPLE_SOURCE_TEXT,  // Distinguishes from paddle
    };
    keying_stream_push(g_stream, &sample);
}
```

## Memory Slots API

```c
// text_memory.h

#define TEXT_MEMORY_SLOTS     8
#define TEXT_MEMORY_MAX_LEN   128  // Max characters per slot

typedef struct {
    char text[TEXT_MEMORY_MAX_LEN];
    char label[16];               // e.g., "CQ", "73", "CONTEST"
} text_memory_slot_t;

// Lifecycle
esp_err_t text_memory_init(void);     // Load from NVS

// Access
esp_err_t text_memory_get(uint8_t slot, text_memory_slot_t *out);
esp_err_t text_memory_set(uint8_t slot, const char *text, const char *label);
esp_err_t text_memory_clear(uint8_t slot);

// Persistence
esp_err_t text_memory_save(void);     // Write all to NVS
esp_err_t text_memory_load(void);     // Read all from NVS
```

### NVS Storage

```
Namespace: "text_mem"
Keys:
  "slot0_text"   "slot0_label"
  "slot1_text"   "slot1_label"
  ...
  "slot7_text"   "slot7_label"
```

### Default Values

| Slot | Label | Text |
|------|-------|------|
| 0 | CQ | `CQ CQ CQ DE IU3QEZ IU3QEZ K` |
| 1 | 73 | `73 TU DE IU3QEZ <SK>` |
| 2 | RST | `UR RST 599 599` |
| 3 | QTH | `QTH THIENE THIENE` |
| 4-7 | (empty) | |

## Console Commands

```
send <text>         Send text as CW
                    e.g., send CQ CQ DE IU3QEZ K

m<N>                Send memory slot N (1-8)
                    e.g., m1  →  sends slot 0

abort               Abort transmission in progress

pause               Pause transmission (can resume)
resume              Resume after pause

mem                 List all memory slots
mem <N>             Show slot N contents
mem <N> <text>      Save text to slot N
mem <N> clear       Clear slot N
mem <N> label <lbl> Set label for slot N
```

### Tab Completion

- `send ` → no completion (free-form text)
- `m` → `m1` `m2` ... `m8`
- `mem ` → `1` `2` ... `8`
- `mem 1 ` → `clear` `label`

### Example Session

```
> mem
Slot 1 [CQ]:     CQ CQ CQ DE IU3QEZ IU3QEZ K
Slot 2 [73]:     73 TU DE IU3QEZ <SK>
Slot 3 [RST]:    UR RST 599 599
Slot 4 [QTH]:    QTH THIENE THIENE
Slot 5: (empty)
Slot 6: (empty)
Slot 7: (empty)
Slot 8: (empty)

> m1
Sending: CQ CQ CQ DE IU3QEZ IU3QEZ K
[===>      ] 4/10

> abort
Aborted.

> send TEST 123
Sending: TEST 123
[==========] 8/8 Done.

> mem 5 CQ CONTEST DE IU3QEZ
Slot 5 saved.

> mem 5 label CONTEST
Slot 5 label set to "CONTEST"
```

## Integration

### bg_task.c

```c
void bg_task(void *arg) {
    text_keyer_init(&(text_keyer_config_t){
        .stream = &g_keying_stream,
        .paddle_active = &g_paddle_active,
    });
    text_memory_init();  // Load NVS slots

    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        int64_t now_us = esp_timer_get_time();

        // Text keyer tick (~10ms resolution OK for CW timing)
        text_keyer_tick(now_us);

        // Other background work: decoder, remote, etc.

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}
```

### rt_task.c - Paddle Signal

```c
// Already exists in iambic FSM
atomic_bool g_paddle_active = ATOMIC_VAR_INIT(false);

void rt_task(void *arg) {
    for (;;) {
        bool dit = hal_gpio_get_dit();
        bool dah = hal_gpio_get_dah();

        // Update atomic flag for text keyer abort
        bool active = dit || dah;
        atomic_store_explicit(&g_paddle_active, active,
                              memory_order_release);

        // Iambic FSM processing...
        iambic_tick(&g_iambic, dit, dah, now_us);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1));
    }
}
```

### Abort Latency

- RT task updates `g_paddle_active` every 1ms
- bg_task reads every 10ms
- Worst case: 11ms for abort (acceptable for CW)

## Estimates

| Component | Files | LOC (estimated) |
|-----------|-------|-----------------|
| `keyer_morse` | morse_table.h/c | ~200 |
| `keyer_text` | text_keyer.h/c, text_memory.h/c | ~400 |
| `cmd_text.c` | console commands | ~150 |
| **Total** | | **~750** |

## Not Included (YAGNI)

- Farnsworth spacing
- Weighting
- Live keyboard (character-by-character) - only complete strings
- Web GUI (API ready but not implemented now)
