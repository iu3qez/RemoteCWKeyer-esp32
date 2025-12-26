# CW Decoder Design

**Date:** 2025-12-26
**Status:** Approved
**Branch:** decoder

## Overview

Local CW decoder that shows what you're transmitting on console and future WebUI. Consumes from `keying_stream_t` as a best-effort consumer on Core 1.

## Goals

- Decode transmitted CW to text in real-time
- Adaptive timing (learns operator's speed via EMA)
- Console commands for control and display
- API ready for future WebUI integration
- Pure C implementation, static allocation

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Core 0 (RT)                             │
│  ┌──────────┐    ┌──────────┐    ┌──────────────────┐          │
│  │ GPIO Poll│───▶│ Iambic   │───▶│ keying_stream_t  │          │
│  └──────────┘    │ FSM      │    │ (mark/space      │          │
│                  └──────────┘    │  samples)        │          │
│                                  └────────┬─────────┘          │
└───────────────────────────────────────────┼────────────────────┘
                                            │
                    ┌───────────────────────┼───────────────────┐
                    │                       ▼         Core 1    │
                    │              ┌─────────────────┐          │
                    │              │ decoder_consumer│          │
                    │              │ (best-effort)   │          │
                    │              └────────┬────────┘          │
                    │                       │                   │
                    │    ┌──────────────────┼──────────────────┐│
                    │    │           keyer_decoder             ││
                    │    │  ┌────────────────────────────────┐ ││
                    │    │  │ timing_classifier              │ ││
                    │    │  │ - EMA dit/dah averages         │ ││
                    │    │  │ - gap classification           │ ││
                    │    │  └────────────────────────────────┘ ││
                    │    │  ┌────────────────────────────────┐ ││
                    │    │  │ pattern_decoder                │ ││
                    │    │  │ - state machine                │ ││
                    │    │  │ - morse_table lookup           │ ││
                    │    │  │ - circular text buffer (128)   │ ││
                    │    │  └────────────────────────────────┘ ││
                    │    └─────────────────────────────────────┘│
                    │                       │                   │
                    │              ┌────────▼────────┐          │
                    │              │ Console/WebUI   │          │
                    │              │ decoder text    │          │
                    │              └─────────────────┘          │
                    └───────────────────────────────────────────┘
```

**Data Flow:**
1. RT task produces mark/space samples in `keying_stream_t`
2. `decoder_consumer` reads samples (best-effort, Core 1)
3. Measures mark/space durations in microseconds
4. `timing_classifier` classifies: dit/dah/intra-gap/char-gap/word-gap
5. `pattern_decoder` accumulates patterns and decodes characters
6. Text available via API for console/WebUI

## Component Structure

```
components/keyer_decoder/
├── CMakeLists.txt
├── include/
│   ├── decoder.h              # Public API
│   ├── timing_classifier.h    # Timing classifier
│   └── morse_table.h          # Pattern→char lookup
└── src/
    ├── decoder.c              # Consumer + state machine
    ├── timing_classifier.c    # EMA + classification
    └── morse_table.c          # Static ITU table
```

## Public API (decoder.h)

```c
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/** Decoded character with timestamp */
typedef struct {
    char character;         /** ASCII character */
    int64_t timestamp_us;   /** When it was decoded */
} decoded_char_t;

/** Initialize decoder (call from main before bg_task) */
void decoder_init(void);

/** Process samples from stream (call from bg_task) */
void decoder_process(void);

/** Enable/disable decoder */
void decoder_set_enabled(bool enabled);
bool decoder_is_enabled(void);

/** Get decoded text with timestamps */
size_t decoder_get_text_with_timestamps(decoded_char_t *buf, size_t max_count);

/** Get decoded text (without timestamps, for console) */
size_t decoder_get_text(char *buf, size_t max_len);

/** Get last decoded character with timestamp */
decoded_char_t decoder_get_last_char(void);

/** Get detected WPM (0 if not calibrated) */
uint32_t decoder_get_wpm(void);

/** Reset state (clears buffer and timing) */
void decoder_reset(void);

/** Current pattern being accumulated (for debug) */
size_t decoder_get_current_pattern(char *buf, size_t max_len);
```

## Timing Classifier

### State

```c
typedef struct {
    int64_t dit_avg_us;      /** Dit average in microseconds */
    int64_t dah_avg_us;      /** Dah average in microseconds */
    uint32_t dit_count;      /** Dit samples seen */
    uint32_t dah_count;      /** Dah samples seen */
    float tolerance_pct;     /** Tolerance ±% (default 25) */
} timing_classifier_t;
```

### Events

```c
typedef enum {
    KEY_EVENT_DIT = 0,       /** Short mark (.) */
    KEY_EVENT_DAH = 1,       /** Long mark (-) */
    KEY_EVENT_INTRA_GAP = 2, /** Intra-character space (~1 dit) */
    KEY_EVENT_CHAR_GAP = 3,  /** Inter-character space (~3 dit) */
    KEY_EVENT_WORD_GAP = 4,  /** Inter-word space (~7 dit) */
    KEY_EVENT_UNKNOWN = 255  /** Warm-up, not classifiable */
} key_event_t;
```

### Classification Logic

```c
// For mark (key-on):
threshold = dit_avg_us * 1.5 * (1 + tolerance/100)
if (duration < threshold) → DIT, update dit_avg
else → DAH, update dah_avg

// For space (key-off):
if (duration < dit_avg * 2) → INTRA_GAP
else if (duration < dit_avg * 5) → CHAR_GAP
else → WORD_GAP

// EMA update (α = 0.3):
new_avg = 0.3 * measured + 0.7 * old_avg

// WPM from dit average:
wpm = 1,200,000 / dit_avg_us  // PARIS standard
```

**Warm-up:** First 3 classifications use default 20 WPM, then adapts.

## Pattern Decoder (State Machine)

### States

```c
typedef enum {
    DECODER_STATE_IDLE = 0,      /** Waiting for first dit/dah */
    DECODER_STATE_RECEIVING = 1, /** Accumulating pattern */
} decoder_state_t;
```

### Transitions

```
IDLE + DIT/DAH → RECEIVING (start pattern)
RECEIVING + DIT → append '.', stay RECEIVING
RECEIVING + DAH → append '-', stay RECEIVING
RECEIVING + INTRA_GAP → ignore (within character)
RECEIVING + CHAR_GAP → finalize pattern → lookup → buffer → IDLE
RECEIVING + WORD_GAP → finalize + add ' ' → IDLE
```

### Pattern Buffer

```c
#define MAX_PATTERN_LEN 6  /** Maximum ITU (e.g., "------" = 0) */

static char s_pattern[MAX_PATTERN_LEN + 1];
static size_t s_pattern_len;
```

### Inactivity Timeout

If in RECEIVING for >7 dit units without events, force finalization with space.

## Morse Table

Static array with linear search (~50 entries, <5μs lookup):

```c
typedef struct {
    const char *pattern;
    char character;
} morse_entry_t;

static const morse_entry_t MORSE_TABLE[] = {
    // Letters A-Z
    { ".-",    'A' },
    { "-...",  'B' },
    { "-.-.",  'C' },
    // ... (26 letters)

    // Numbers 0-9
    { "-----", '0' },
    { ".----", '1' },
    // ... (10 numbers)

    // Punctuation
    { ".-.-.-", '.' },
    { "--..--", ',' },
    { "..--..", '?' },
    // ...

    // Prosigns
    { ".-.-.",  '+' },  // AR
    { "-...-",  '=' },  // BT
    { "...-.-", '*' },  // SK
};
```

## Decoded Text Buffer

```c
#define DECODER_BUFFER_SIZE 128

static decoded_char_t s_decoded_buffer[DECODER_BUFFER_SIZE];
static size_t s_buffer_head;   // Next write position
static size_t s_buffer_count;  // Characters in buffer
```

Circular buffer: when full, oldest characters are overwritten.

## Console Commands

```
decoder           Show status and last text
decoder on|off    Enable/disable decoder
decoder text      Show full buffer with timestamps
decoder clear     Clear buffer and reset timing
decoder stats     Show timing statistics
```

### Examples

```
> decoder
Decoder: ON, WPM: 25, buffer: 47/128 chars
Last: "CQ CQ DE IK1ABC"

> decoder stats
WPM: 25 (dit: 48ms, dah: 142ms, ratio: 2.96)
Samples: dit=127, dah=84
Buffer: 47/128 chars
State: IDLE

> decoder text
[14:32:05.123] C
[14:32:05.456] Q
[14:32:05.890] (space)
...
```

## Integration

### bg_task.c

```c
#include "decoder.h"

void bg_task(void *arg) {
    decoder_init();

    for (;;) {
        // ... existing log drain ...
        decoder_process();
        vTaskDelay(pdMS_TO_TICKS(10));  // 100Hz
    }
}
```

### Stream Consumer

```c
static consumer_t s_consumer;
static int64_t s_last_edge_us;
static bool s_last_was_mark;

void decoder_init(void) {
    consumer_init(&s_consumer, &g_keying_stream, CONSUMER_BEST_EFFORT);
    s_last_edge_us = 0;
    s_last_was_mark = false;
}

void decoder_process(void) {
    if (!s_enabled) return;

    stream_sample_t sample;
    while (consumer_pop(&s_consumer, &sample)) {
        bool is_mark = sample_is_mark(&sample);
        int64_t now_us = sample.timestamp_us;

        if (s_last_edge_us > 0 && is_mark != s_last_was_mark) {
            int64_t duration_us = now_us - s_last_edge_us;
            key_event_t event = timing_classify(duration_us, s_last_was_mark);
            decoder_handle_event(event, now_us);
        }

        s_last_edge_us = now_us;
        s_last_was_mark = is_mark;
    }
}
```

## Testing

### Host Tests

```c
// test_morse_table.c
void test_lookup_letters(void) {
    TEST_ASSERT_EQUAL_CHAR('A', morse_table_lookup(".-"));
    TEST_ASSERT_EQUAL_CHAR('S', morse_table_lookup("..."));
    TEST_ASSERT_EQUAL_CHAR('\0', morse_table_lookup("invalid"));
}

// test_timing_classifier.c
void test_classify_dit_dah(void) {
    timing_classifier_t tc;
    timing_classifier_init(&tc, 25.0f);

    // Simulate dit at 20 WPM (60ms)
    key_event_t e = timing_classifier_classify(&tc, 60000, true);
    TEST_ASSERT_EQUAL(KEY_EVENT_DIT, e);
}

// test_decoder.c
void test_decode_letter_a(void) {
    decoder_reset();
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 2000);
    decoder_handle_event(KEY_EVENT_DAH, 3000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 4000);

    TEST_ASSERT_EQUAL_CHAR('A', decoder_get_last_char().character);
}
```

## Files Summary

| File | Action |
|------|--------|
| `components/keyer_decoder/CMakeLists.txt` | Create |
| `components/keyer_decoder/include/decoder.h` | Create |
| `components/keyer_decoder/include/timing_classifier.h` | Create |
| `components/keyer_decoder/include/morse_table.h` | Create |
| `components/keyer_decoder/src/decoder.c` | Create |
| `components/keyer_decoder/src/timing_classifier.c` | Create |
| `components/keyer_decoder/src/morse_table.c` | Create |
| `test_host/test_decoder.c` | Create |
| `test_host/test_timing_classifier.c` | Create |
| `test_host/test_morse_table.c` | Create |
| `components/keyer_console/src/commands.c` | Modify |
| `main/bg_task.c` | Modify |

## Dependencies

- `keyer_decoder` → `keyer_core`
- `keyer_console` → `keyer_decoder`
