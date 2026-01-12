# Roundtrip Test Design: Text Keyer → Decoder

**Date:** 2025-01-09
**Status:** Draft
**Type:** Host Test

## Overview

End-to-end test that validates both the text keyer (encoder) and decoder by sending text through the keyer and verifying the decoder output matches the input. This test runs on the host without hardware.

## Goals

- Validate text keyer produces correct morse timing
- Validate decoder correctly interprets morse patterns
- Verify encoder/decoder symmetry (morse_table consistency)
- Test adaptive WPM detection accuracy
- Provide regression testing for timing changes
- Pure C implementation, runs on host

## Architecture

```
┌────────────────────────────────────────────────────────────────────────┐
│                          Host Test Environment                          │
│                                                                        │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────────────┐   │
│  │  text_keyer  │────▶│ test_rt_loop │────▶│  keying_stream_t     │   │
│  │  "CQ TEST"   │     │ (simulated)  │     │  (mark/space samples)│   │
│  └──────────────┘     └──────────────┘     └──────────┬───────────┘   │
│         │                                             │               │
│         │ text_keyer_tick()                           │               │
│         │ text_keyer_is_key_down()                    ▼               │
│         │                                    ┌────────────────┐       │
│         │                                    │    decoder     │       │
│         │                                    │  (consumer)    │       │
│         │                                    └───────┬────────┘       │
│         │                                            │                │
│         ▼                                            ▼                │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │                      TEST ASSERTIONS                             │  │
│  │  - decoded_text == input_text                                   │  │
│  │  - detected_wpm ≈ configured_wpm (±10%)                         │  │
│  │  - no dropped samples                                           │  │
│  └─────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────┘
```

## Data Flow

1. **Setup**: Initialize stream, text_keyer, decoder with shared stream
2. **Send**: `text_keyer_send("CQ TEST")` queues text for transmission
3. **Simulate RT loop**:
   - Call `text_keyer_tick(now_us)` at 10ms intervals (bg_task simulation)
   - Poll `text_keyer_is_key_down()` at 1ms intervals (rt_task simulation)
   - Push samples to stream based on key state
4. **Decode**: `decoder_process()` consumes samples and produces text
5. **Assert**: Compare decoded output with original input

## Test Cases

### TC-01: Basic Character Roundtrip
```c
void test_single_char_roundtrip(void) {
    text_keyer_send("E");  // Shortest: single dit
    simulate_until_idle();
    TEST_ASSERT_EQUAL_STRING("E", decoder_get_text());
}
```

### TC-02: Full Message Roundtrip
```c
void test_message_roundtrip(void) {
    text_keyer_send("CQ CQ DE IU4APC");
    simulate_until_idle();
    TEST_ASSERT_EQUAL_STRING("CQ CQ DE IU4APC", decoder_get_text());
}
```

### TC-03: Prosigns
```c
void test_prosign_roundtrip(void) {
    text_keyer_send("<SK>");  // End of contact
    simulate_until_idle();
    TEST_ASSERT_EQUAL_STRING("<SK>", decoder_get_text());
}
```

### TC-04: WPM Detection Accuracy
```c
void test_wpm_detection(void) {
    CONFIG_SET_WPM(25);
    text_keyer_send("PARIS PARIS PARIS");  // Standard word
    simulate_until_idle();

    uint32_t detected = decoder_get_wpm();
    TEST_ASSERT_INT_WITHIN(3, 25, detected);  // ±3 WPM tolerance
}
```

### TC-05: Various WPM Speeds
```c
void test_wpm_range(void) {
    uint32_t speeds[] = {10, 15, 20, 25, 30, 35, 40};
    for (int i = 0; i < ARRAY_SIZE(speeds); i++) {
        CONFIG_SET_WPM(speeds[i]);
        decoder_reset();
        text_keyer_send("TEST");
        simulate_until_idle();
        TEST_ASSERT_EQUAL_STRING("TEST", decoder_get_text());
    }
}
```

### TC-06: All Characters
```c
void test_all_characters(void) {
    // Letters
    text_keyer_send("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    simulate_until_idle();
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLMNOPQRSTUVWXYZ", decoder_get_text());

    decoder_reset();

    // Numbers
    text_keyer_send("0123456789");
    simulate_until_idle();
    TEST_ASSERT_EQUAL_STRING("0123456789", decoder_get_text());
}
```

## Implementation

### File Structure
```
test_host/
├── CMakeLists.txt
├── test_runner.c
├── test_text_keyer.c       # Existing text keyer tests
├── test_decoder.c          # Existing decoder tests
└── test_roundtrip.c        # NEW: roundtrip tests
```

### Simulated RT Loop
```c
/* Simulation parameters */
#define SIM_RT_TICK_US      1000    /* 1ms RT task tick */
#define SIM_BG_TICK_US     10000    /* 10ms bg task tick */
#define SIM_MAX_DURATION_US 30000000 /* 30 seconds max */

/* Simulated RT loop - mimics rt_task + bg_task interaction */
static void simulate_until_idle(void) {
    int64_t now_us = 0;
    int64_t last_bg_tick = 0;

    while (text_keyer_get_state() != TEXT_KEYER_IDLE &&
           now_us < SIM_MAX_DURATION_US) {

        /* Simulate bg_task tick (10ms) */
        if (now_us - last_bg_tick >= SIM_BG_TICK_US) {
            text_keyer_tick(now_us);
            last_bg_tick = now_us;
        }

        /* Simulate rt_task tick (1ms) */
        stream_sample_t sample = STREAM_SAMPLE_EMPTY;
        sample.local_key = text_keyer_is_key_down() ? 1 : 0;
        stream_push(&s_test_stream, sample);

        /* Process decoder */
        decoder_process();

        now_us += SIM_RT_TICK_US;
    }

    /* Flush remaining decoder state */
    for (int i = 0; i < 1000; i++) {
        stream_sample_t sample = STREAM_SAMPLE_EMPTY;
        stream_push(&s_test_stream, sample);
        decoder_process();
    }
}
```

### Test Fixture
```c
static keying_stream_t s_test_stream;
static stream_sample_t s_test_buffer[1024];

void setUp_roundtrip(void) {
    /* Initialize stream */
    stream_init(&s_test_stream, s_test_buffer, 1024);

    /* Initialize text keyer (no paddle abort for tests) */
    text_keyer_config_t tk_cfg = { .paddle_abort = NULL };
    text_keyer_init(&tk_cfg);

    /* Initialize decoder with test stream */
    decoder_init_with_stream(&s_test_stream);  /* Need to add this API */

    /* Set default WPM */
    CONFIG_SET_WPM(20);
}

void tearDown_roundtrip(void) {
    text_keyer_abort();
    decoder_reset();
}
```

## Dependencies

### Required Modifications

1. **decoder.h/decoder.c**: Add `decoder_init_with_stream()` for test injection
   ```c
   /* For testing: initialize with custom stream */
   void decoder_init_with_stream(keying_stream_t *stream);
   ```

2. **decoder.h/decoder.c**: Add `decoder_get_text()` to retrieve decoded buffer
   ```c
   /* Get decoded text buffer */
   const char* decoder_get_text(void);
   ```

3. **config.h**: Ensure `CONFIG_SET_WPM()` / `CONFIG_GET_WPM()` work in host tests

### Existing Components (No Changes)
- `text_keyer.c` - Uses `text_keyer_is_key_down()` atomic interface
- `stream.c` - Lock-free stream
- `timing_classifier.c` - Adaptive WPM detection
- `morse_table.c` - Character encoding/decoding

## Success Criteria

| Metric | Target |
|--------|--------|
| All roundtrip tests pass | 100% |
| WPM detection accuracy | ±10% of configured |
| Character accuracy | 100% (no errors) |
| Test execution time | < 5 seconds |

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Decoder WPM warmup period | Test may fail on first chars | Send warmup pattern before test |
| Timing edge cases | Missed char boundaries | Use standard PARIS timing |
| Prosign encoding mismatch | Encoder/decoder out of sync | Share morse_table between both |

## Future Extensions

- Fuzz testing with random character sequences
- Timing jitter injection (simulate real-world variations)
- Performance benchmarks (decoding throughput)
- Integration with CI pipeline

## References

- [text_keyer.h](../../components/keyer_text/include/text_keyer.h)
- [decoder.h](../../components/keyer_decoder/include/decoder.h)
- [timing_classifier.h](../../components/keyer_decoder/include/timing_classifier.h)
- [morse_table.h](../../components/keyer_decoder/include/morse_table.h)
