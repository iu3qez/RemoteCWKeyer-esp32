# TDD Test Cases for CWNet Protocol Implementation

Generated: 2026-01-12

## Goal

Design comprehensive TDD test cases for the CWNet protocol implementation covering:
1. CW stream timestamp encoding (7-bit non-linear)
2. CW stream timestamp decoding
3. Frame parser (streaming, handles fragmentation)

This document provides test specifications only - NO implementation code.

---

## 1. Timestamp Encoding Test Cases

### 1.1 Test File: `test_cwnet_timestamp.c`

#### Range 1: Linear Range (0-31ms, 1ms resolution)

| Test Name | Input (ms) | Expected Output | Rationale |
|-----------|------------|-----------------|-----------|
| `test_timestamp_encode_zero` | 0 | 0x00 | Minimum value boundary |
| `test_timestamp_encode_1ms` | 1 | 0x01 | Basic linear encoding |
| `test_timestamp_encode_15ms` | 15 | 0x0F | Mid-range linear |
| `test_timestamp_encode_31ms` | 31 | 0x1F | Upper boundary of linear range |

#### Range 2: Medium Range (32-156ms, 4ms resolution)

| Test Name | Input (ms) | Expected Output | Rationale |
|-----------|------------|-----------------|-----------|
| `test_timestamp_encode_32ms` | 32 | 0x20 | Transition boundary from linear |
| `test_timestamp_encode_36ms` | 36 | 0x21 | First 4ms step |
| `test_timestamp_encode_60ms` | 60 | 0x27 | (60-32)/4 = 7, 0x20+7 = 0x27 |
| `test_timestamp_encode_100ms` | 100 | 0x31 | (100-32)/4 = 17, 0x20+17 = 0x31 |
| `test_timestamp_encode_156ms` | 156 | 0x3F | Upper boundary of medium range |

#### Range 3: Long Range (157-1165ms, 16ms resolution)

| Test Name | Input (ms) | Expected Output | Rationale |
|-----------|------------|-----------------|-----------|
| `test_timestamp_encode_157ms` | 157 | 0x40 | Transition boundary to long range |
| `test_timestamp_encode_173ms` | 173 | 0x41 | First 16ms step |
| `test_timestamp_encode_500ms` | 500 | 0x55 | (500-157)/16 = 21, 0x40+21 = 0x55 |
| `test_timestamp_encode_1000ms` | 1000 | 0x72 | (1000-157)/16 = 52, 0x40+52 = 0x72 |
| `test_timestamp_encode_1165ms` | 1165 | 0x7F | Maximum encodable value |

#### Edge Cases and Error Handling

| Test Name | Input (ms) | Expected Output | Rationale |
|-----------|------------|-----------------|-----------|
| `test_timestamp_encode_negative` | -1 | 0x00 | Negative clamps to 0 |
| `test_timestamp_encode_negative_large` | -100 | 0x00 | Large negative clamps to 0 |
| `test_timestamp_encode_overflow` | 1166 | 0x7F | Above max clamps to 0x7F |
| `test_timestamp_encode_large_overflow` | 5000 | 0x7F | Very large clamps to 0x7F |
| `test_timestamp_encode_int_max` | INT32_MAX | 0x7F | INT32_MAX clamps to 0x7F |

#### Resolution Boundary Tests

| Test Name | Input (ms) | Expected Output | Rationale |
|-----------|------------|-----------------|-----------|
| `test_timestamp_encode_33ms` | 33 | 0x20 | 33 rounds to 32 (4ms resolution) |
| `test_timestamp_encode_34ms` | 34 | 0x20 | 34 rounds to 32 |
| `test_timestamp_encode_35ms` | 35 | 0x20 | 35 rounds to 32 |
| `test_timestamp_encode_158ms` | 158 | 0x40 | 158 rounds to 157 (16ms resolution) |
| `test_timestamp_encode_165ms` | 165 | 0x40 | 165 rounds to 157 |

---

## 2. Timestamp Decoding Test Cases

### 2.1 Inverse of Encoding (Round-Trip Validation)

| Test Name | Input (encoded) | Expected (ms) | Rationale |
|-----------|-----------------|---------------|-----------|
| `test_timestamp_decode_zero` | 0x00 | 0 | Zero decodes to zero |
| `test_timestamp_decode_linear_max` | 0x1F | 31 | Linear range maximum |
| `test_timestamp_decode_medium_start` | 0x20 | 32 | Medium range start |
| `test_timestamp_decode_medium_end` | 0x3F | 152 | 32 + 4*(0x3F-0x20) = 32 + 124 = 156 |
| `test_timestamp_decode_long_start` | 0x40 | 157 | Long range start |
| `test_timestamp_decode_long_end` | 0x7F | 1165 | 157 + 16*(0x7F-0x40) = 157 + 1008 = 1165 |

### 2.2 Bit 7 Masking

| Test Name | Input (encoded) | Expected (ms) | Rationale |
|-----------|-----------------|---------------|-----------|
| `test_timestamp_decode_with_key_bit_0x80` | 0x80 | 0 | Key bit should be masked off |
| `test_timestamp_decode_with_key_bit_0x9F` | 0x9F | 31 | 0x1F with key bit |
| `test_timestamp_decode_with_key_bit_0xBF` | 0xBF | 152 | 0x3F with key bit |
| `test_timestamp_decode_with_key_bit_0xFF` | 0xFF | 1165 | 0x7F with key bit |

### 2.3 Round-Trip Property Tests

| Test Name | Description |
|-----------|-------------|
| `test_timestamp_roundtrip_linear` | For all ms in [0,31]: decode(encode(ms)) == ms |
| `test_timestamp_roundtrip_medium` | For ms in {32,36,40,...,156}: decode(encode(ms)) within 4ms |
| `test_timestamp_roundtrip_long` | For ms in {157,173,...,1165}: decode(encode(ms)) within 16ms |

---

## 3. Frame Parser Test Cases

### 3.1 Test File: `test_cwnet_frame_parser.c`

### 3.1.1 Frame Category Detection

| Test Name | Input Byte | Expected Category | Rationale |
|-----------|------------|-------------------|-----------|
| `test_frame_category_no_payload` | 0x02 | FRAME_CAT_NO_PAYLOAD | bits 7-6 = 00 (DISCONNECT) |
| `test_frame_category_short` | 0x41 | FRAME_CAT_SHORT_PAYLOAD | bits 7-6 = 01 (CONNECT) |
| `test_frame_category_long` | 0x91 | FRAME_CAT_LONG_PAYLOAD | bits 7-6 = 10 (AUDIO) |
| `test_frame_category_reserved` | 0xC1 | FRAME_CAT_RESERVED | bits 7-6 = 11 |

### 3.1.2 Command Type Extraction

| Test Name | Input Byte | Expected Command | Rationale |
|-----------|------------|------------------|-----------|
| `test_cmd_type_connect` | 0x41 | 0x01 | CONNECT |
| `test_cmd_type_disconnect` | 0x02 | 0x02 | DISCONNECT |
| `test_cmd_type_ping` | 0x43 | 0x03 | PING |
| `test_cmd_type_morse` | 0x50 | 0x10 | MORSE (short block) |
| `test_cmd_type_audio` | 0x91 | 0x11 | AUDIO (long block) |

### 3.2 Complete Frame Parsing

#### No-Payload Frames

| Test Name | Input Bytes | Expected | Rationale |
|-----------|-------------|----------|-----------|
| `test_parse_disconnect_frame` | `[0x02]` | cmd=0x02, len=0 | DISCONNECT (no payload) |

#### Short-Block Frames (1-byte length)

| Test Name | Input Bytes | Expected | Rationale |
|-----------|-------------|----------|-----------|
| `test_parse_ping_frame` | `[0x43, 0x10, <16 bytes>]` | cmd=0x03, len=16 | PING with 16-byte payload |
| `test_parse_connect_frame` | `[0x41, 0x5C, <92 bytes>]` | cmd=0x01, len=92 | CONNECT with 92-byte payload |
| `test_parse_morse_frame_5bytes` | `[0x50, 0x05, <5 bytes>]` | cmd=0x10, len=5 | MORSE with 5 CW bytes |

#### Long-Block Frames (2-byte length, little-endian)

| Test Name | Input Bytes | Expected | Rationale |
|-----------|-------------|----------|-----------|
| `test_parse_audio_frame_320` | `[0x91, 0x40, 0x01, <320 bytes>]` | cmd=0x11, len=320 | AUDIO 40ms @ 8kHz (0x0140 = 320 LE) |
| `test_parse_audio_frame_256` | `[0x91, 0x00, 0x01, <256 bytes>]` | cmd=0x11, len=256 | AUDIO (0x0100 = 256 LE) |
| `test_parse_long_frame_max` | `[0x91, 0xFF, 0xFF, <65535 bytes>]` | cmd=0x11, len=65535 | Maximum long block size |

### 3.3 Streaming Parser (Fragmentation Handling)

#### Single-Byte-At-A-Time Parsing

| Test Name | Description |
|-----------|-------------|
| `test_stream_parse_disconnect_byte_by_byte` | Feed DISCONNECT frame one byte at a time |
| `test_stream_parse_ping_byte_by_byte` | Feed PING frame one byte at a time |
| `test_stream_parse_connect_byte_by_byte` | Feed CONNECT frame (94 bytes) one at a time |

#### Partial Reception

| Test Name | Description |
|-----------|-------------|
| `test_stream_parse_partial_header` | Receive only command byte, then rest |
| `test_stream_parse_partial_length_short` | Receive cmd+partial length for short block |
| `test_stream_parse_partial_length_long` | Receive cmd+1 length byte, then 2nd |
| `test_stream_parse_partial_payload` | Receive header+half payload, then rest |

#### Multiple Frames in Single Buffer

| Test Name | Description |
|-----------|-------------|
| `test_stream_parse_two_pings` | Parse two consecutive PING frames |
| `test_stream_parse_ping_then_morse` | Parse PING followed by MORSE frame |
| `test_stream_parse_three_frames` | Parse DISCONNECT + PING + MORSE in one buffer |

#### Chunked Reception

| Test Name | Description |
|-----------|-------------|
| `test_stream_parse_connect_in_3_chunks` | CONNECT frame split: [cmd+len], [44 bytes], [48 bytes] |
| `test_stream_parse_audio_in_random_chunks` | AUDIO frame split into random-sized chunks |

### 3.4 Error Handling

| Test Name | Description | Expected |
|-----------|-------------|----------|
| `test_parse_reserved_category` | Frame with bits 7-6 = 11 | Return error, advance past byte |
| `test_parse_incomplete_never_completes` | Only send half a frame, then ask for result | Return NEED_MORE_DATA |
| `test_parse_zero_length_short_block` | `[0x50, 0x00]` (MORSE with len=0) | Valid frame with empty payload |
| `test_parse_unknown_command` | `[0x7F]` (bits 7-6 = 01, cmd = 0x3F) | Parse succeeds, unknown cmd handled upstream |

### 3.5 Parser State Reset

| Test Name | Description |
|-----------|-------------|
| `test_parser_reset_clears_state` | Parse partial frame, reset, parse new frame |
| `test_parser_reset_multiple_times` | Multiple reset calls don't corrupt state |
| `test_parser_init_state` | Fresh parser has clean initial state |

---

## 4. Edge Cases Summary

### 4.1 Timestamp Encoding Edge Cases

1. **Boundary transitions** - Values at 31/32 and 156/157 transitions
2. **Quantization loss** - Values between resolution steps (e.g., 33, 34, 35 all map to 32)
3. **Overflow protection** - Values > 1165ms clamp to 0x7F
4. **Negative protection** - Negative values clamp to 0x00
5. **Integer limits** - INT32_MAX, INT32_MIN handling

### 4.2 Timestamp Decoding Edge Cases

1. **Key bit masking** - Bit 7 must be stripped before decode
2. **Range boundaries** - 0x1F/0x20 and 0x3F/0x40 transitions
3. **Maximum value** - 0x7F (with and without key bit)
4. **Invalid high bits** - 0x80 alone (key bit only, ts=0)

### 4.3 Frame Parser Edge Cases

1. **Empty payloads** - Short block with length=0
2. **Maximum payloads** - Long block with length=65535
3. **Fragmentation at every position** - Split at cmd, length, payload boundaries
4. **Multiple frames back-to-back** - No gaps between frames
5. **Reserved category** - bits 7-6 = 11 (graceful error)
6. **Endianness** - Long block length is little-endian

---

## 5. Suggested Test File Structure

```
test_host/
├── test_cwnet_timestamp.c      # Timestamp encode/decode tests
├── test_cwnet_frame_parser.c   # Frame parsing tests
├── test_cwnet_event_encode.c   # Full CW event encoding (future)
└── test_main.c                 # Add CWNet test declarations

components/keyer_cwnet/
├── include/
│   ├── cwnet_timestamp.h       # Timestamp encode/decode API
│   └── cwnet_frame.h           # Frame parser API
└── src/
    ├── cwnet_timestamp.c       # Timestamp implementation
    └── cwnet_frame.c           # Frame parser implementation
```

### 5.1 Test Registration in test_main.c

```c
/* CWNet Timestamp tests */
void test_timestamp_encode_zero(void);
void test_timestamp_encode_31ms(void);
void test_timestamp_encode_32ms(void);
void test_timestamp_encode_156ms(void);
void test_timestamp_encode_157ms(void);
void test_timestamp_encode_1165ms(void);
void test_timestamp_encode_negative(void);
void test_timestamp_encode_overflow(void);
void test_timestamp_decode_zero(void);
void test_timestamp_decode_linear_max(void);
void test_timestamp_decode_medium_start(void);
void test_timestamp_decode_long_end(void);
void test_timestamp_decode_with_key_bit_0xFF(void);
void test_timestamp_roundtrip_linear(void);

/* CWNet Frame Parser tests */
void test_frame_category_no_payload(void);
void test_frame_category_short(void);
void test_frame_category_long(void);
void test_parse_disconnect_frame(void);
void test_parse_ping_frame(void);
void test_parse_connect_frame(void);
void test_parse_morse_frame_5bytes(void);
void test_parse_audio_frame_320(void);
void test_stream_parse_ping_byte_by_byte(void);
void test_stream_parse_partial_payload(void);
void test_stream_parse_two_pings(void);
void test_parser_reset_clears_state(void);
```

### 5.2 CMakeLists.txt Updates

```cmake
# Add to CWNET_SOURCES
set(CWNET_SOURCES
    ${COMPONENT_DIR}/keyer_cwnet/src/cwnet_timestamp.c
    ${COMPONENT_DIR}/keyer_cwnet/src/cwnet_frame.c
)

# Add test files
set(TEST_SOURCES
    ...
    test_cwnet_timestamp.c
    test_cwnet_frame_parser.c
)

# Add include path
include_directories(
    ...
    ${COMPONENT_DIR}/keyer_cwnet/include
)
```

---

## 6. Test Implementation Patterns

### 6.1 Timestamp Test Pattern

```c
void test_timestamp_encode_32ms(void) {
    // Arrange
    int input_ms = 32;

    // Act
    uint8_t encoded = cwstream_encode_timestamp(input_ms);

    // Assert
    TEST_ASSERT_EQUAL_HEX8(0x20, encoded);
}

void test_timestamp_roundtrip_linear(void) {
    // Test all values in linear range
    for (int ms = 0; ms <= 31; ms++) {
        uint8_t encoded = cwstream_encode_timestamp(ms);
        int decoded = cwstream_decode_timestamp(encoded);
        TEST_ASSERT_EQUAL_INT(ms, decoded);
    }
}
```

### 6.2 Frame Parser Test Pattern

```c
void test_parse_ping_frame(void) {
    // Arrange
    uint8_t frame[] = {
        0x43,                   // PING with short block
        0x10,                   // Length = 16
        0x00, 0x01, 0x00, 0x00, // type=0, id=1, reserved
        0x00, 0x00, 0x01, 0x00, // t0 = 256 (LE)
        0x00, 0x00, 0x00, 0x00, // t1 = 0
        0x00, 0x00, 0x00, 0x00  // t2 = 0
    };
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    // Act
    cwnet_parse_result_t result = cwnet_frame_parse(&parser, frame, sizeof(frame));

    // Assert
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, result.status);
    TEST_ASSERT_EQUAL_HEX8(0x03, result.command);
    TEST_ASSERT_EQUAL(16, result.payload_len);
    TEST_ASSERT_EQUAL(18, result.bytes_consumed);
}

void test_stream_parse_ping_byte_by_byte(void) {
    // Arrange
    uint8_t frame[] = {0x43, 0x10, /* 16 bytes payload */};
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    // Act - feed bytes one at a time
    for (size_t i = 0; i < sizeof(frame) - 1; i++) {
        cwnet_parse_result_t result = cwnet_frame_parse(&parser, &frame[i], 1);
        TEST_ASSERT_EQUAL(CWNET_PARSE_NEED_MORE, result.status);
    }

    // Final byte completes frame
    cwnet_parse_result_t result = cwnet_frame_parse(&parser, &frame[sizeof(frame)-1], 1);
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, result.status);
}
```

---

## 7. Test Dependencies

### Required Headers

```c
#include "unity.h"
#include "cwnet_timestamp.h"
#include "cwnet_frame.h"
#include "stubs/esp_stubs.h"  // For HOST_TEST builds
```

### No Mocking Required

Per ARCHITECTURE.md, components are tested via their public interfaces (stream-based). No mocking is needed for these pure algorithmic functions:
- `cwstream_encode_timestamp()` - pure function
- `cwstream_decode_timestamp()` - pure function
- `cwnet_frame_parser_*()` - stateful but deterministic

---

## 8. Coverage Goals

| Component | Line Coverage Target | Branch Coverage Target |
|-----------|---------------------|------------------------|
| cwnet_timestamp.c | 100% | 100% |
| cwnet_frame.c | 100% | 100% |

All boundary conditions and error paths must be exercised.

---

**End of Test Case Design Document**