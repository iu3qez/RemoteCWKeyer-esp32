# Handoff: Phase 1 - Morse Lookup Table & Queue Infrastructure

**Task:** Implement ASCII-to-Morse lookup table and lock-free SPSC queue
**Status:** COMPLETED
**Date:** 2026-01-22

---

## Summary

Created the `keyer_winkeyer` component with:
1. ASCII-to-Morse lookup table (`winkeyer_morse.h/c`)
2. Lock-free SPSC morse queue (`morse_queue.h/c`)
3. Comprehensive unit tests (`test_morse.c`)

All 26 Phase 1 tests pass with sanitizers enabled (AddressSanitizer + UndefinedBehaviorSanitizer).

---

## Files Created

### Component Structure
```
components/keyer_winkeyer/
|-- CMakeLists.txt
|-- include/
|   |-- winkeyer.h         (stub)
|   |-- winkeyer_morse.h   (morse lookup API)
|   |-- morse_queue.h      (SPSC queue API)
|-- src/
    |-- winkeyer.c         (stub)
    |-- winkeyer_morse.c   (ITU morse table)
    |-- morse_queue.c      (lock-free queue)
```

### Test File
- `test_host/test_morse.c` - 26 unit tests

### Modified Files
- `test_host/CMakeLists.txt` - Added winkeyer includes and sources
- `test_host/test_main.c` - Added morse test declarations and RUN_TEST calls

---

## Implementation Details

### Morse Lookup Table (`winkeyer_morse.h/c`)

ITU Morse encoding with:
- Bit pattern storage (0=dit, 1=dah, LSB first)
- Length field (1-6 elements)
- Covers A-Z, 0-9, and punctuation (. , ? / = -)
- Case insensitive lookup
- Returns NULL for unsupported characters (including space)

```c
typedef struct {
    uint8_t pattern;  // LSB first, 0=dit, 1=dah
    uint8_t length;   // Number of elements (1-6)
} morse_char_t;

const morse_char_t *morse_lookup(char c);
```

**Example encodings:**
- 'A' (.-): pattern=0x02, length=2
- 'B' (-...): pattern=0x01, length=4
- '0' (-----): pattern=0x1F, length=5

### Lock-Free SPSC Queue (`morse_queue.h/c`)

Single Producer, Single Consumer queue for cross-core communication:
- Producer: Winkeyer controller on Core 1
- Consumer: RT task on Core 0

**Design:**
- 64-element static buffer (power of 2 for fast modulo)
- Atomic indices with appropriate memory ordering
- acquire/release semantics for visibility guarantees
- No heap allocation

```c
typedef enum {
    MORSE_ELEMENT_DIT,
    MORSE_ELEMENT_DAH,
    MORSE_ELEMENT_CHAR_SPACE,   // 3 dit gap
    MORSE_ELEMENT_WORD_SPACE,   // 7 dit gap
    MORSE_ELEMENT_KEY_DOWN,     // Immediate key
    MORSE_ELEMENT_KEY_UP,
} morse_element_type_t;

typedef struct {
    morse_element_t buffer[MORSE_QUEUE_SIZE];
    atomic_size_t   write_idx;
    atomic_size_t   read_idx;
} morse_queue_t;
```

**Memory Ordering:**
- Producer: `memory_order_release` on write_idx store
- Consumer: `memory_order_acquire` on write_idx load
- Consumer: `memory_order_release` on read_idx store
- Producer: `memory_order_acquire` on read_idx load

---

## Test Results

```
=== Morse Lookup Tests ===
test_morse_lookup_A:PASS
test_morse_lookup_B:PASS
test_morse_lookup_E:PASS
test_morse_lookup_T:PASS
test_morse_lookup_0:PASS
test_morse_lookup_5:PASS
test_morse_lookup_1:PASS
test_morse_lookup_period:PASS
test_morse_lookup_question:PASS
test_morse_lookup_slash:PASS
test_morse_lookup_equals:PASS
test_morse_lookup_comma:PASS
test_morse_lookup_lowercase:PASS
test_morse_lookup_space_returns_null:PASS
test_morse_lookup_unsupported_returns_null:PASS
test_morse_lookup_all_letters:PASS
test_morse_lookup_all_digits:PASS

=== Morse Queue Tests ===
test_morse_queue_init:PASS
test_morse_queue_push_pop:PASS
test_morse_queue_all_element_types:PASS
test_morse_queue_pop_empty_returns_false:PASS
test_morse_queue_fill_to_capacity:PASS
test_morse_queue_wraparound:PASS
test_morse_queue_clear:PASS
test_morse_queue_interleaved_ops:PASS
test_morse_queue_size_is_power_of_2:PASS

Total: 26 tests, 26 passed
Sanitizers: ASan + UBSan enabled, no issues detected
```

---

## Acceptance Criteria Status

- [x] Morse lookup covers A-Z, 0-9, common punctuation
- [x] Queue push/pop is lock-free with atomic indices
- [x] All tests pass with sanitizers enabled
- [x] Zero heap allocation in queue operations
- [x] Queue size is power of 2 (64)
- [x] Uses `stdatomic.h` for queue indices

---

## ARCHITECTURE.md Compliance

- [x] RULE 3.1.1: Only atomic operations for synchronization
- [x] RULE 3.1.4: No operation shall block
- [x] RULE 9.1.1: Statically allocated buffer
- [x] RULE 9.1.3: No heap allocation

---

## Notes for Phase 2

The morse queue is ready for the Winkeyer parser:
1. Parser can push morse elements via `morse_queue_push()`
2. RT task can consume via `morse_queue_pop()`
3. Flow control: Check `morse_queue_count()` for XOFF/XON

Parser will need to:
- Convert text characters to morse elements using `morse_lookup()`
- Insert `MORSE_ELEMENT_CHAR_SPACE` between characters
- Insert `MORSE_ELEMENT_WORD_SPACE` for spaces

---

## Pre-existing Issues

Two tests unrelated to this task failed:
- `test_stream_overrun_detection` - pre-existing issue
- `test_fault_count` - pre-existing issue (fault count not reset between tests)

These should be addressed separately.
