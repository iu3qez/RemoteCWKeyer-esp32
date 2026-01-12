/**
 * @file test_cwnet_timestamp.c
 * @brief Unit tests for CWNet 7-bit non-linear timestamp encoding/decoding
 *
 * Test plan: thoughts/shared/plans/2026-01-12-cwnet-tdd-test-cases.md
 *
 * Encoding scheme:
 *   Range 1: 0-31ms   -> 0x00-0x1F (1ms resolution, linear)
 *   Range 2: 32-156ms -> 0x20-0x3F (4ms resolution)
 *   Range 3: 157-1165ms -> 0x40-0x7F (16ms resolution)
 */

#include "unity.h"
#include "cwnet_timestamp.h"
#include <limits.h>

/*===========================================================================*/
/* Range 1: Linear Range (0-31ms, 1ms resolution)                            */
/*===========================================================================*/

void test_timestamp_encode_zero(void) {
    /* Minimum value boundary */
    uint8_t encoded = cwstream_encode_timestamp(0);
    TEST_ASSERT_EQUAL_HEX8(0x00, encoded);
}

void test_timestamp_encode_1ms(void) {
    /* Basic linear encoding */
    uint8_t encoded = cwstream_encode_timestamp(1);
    TEST_ASSERT_EQUAL_HEX8(0x01, encoded);
}

void test_timestamp_encode_15ms(void) {
    /* Mid-range linear */
    uint8_t encoded = cwstream_encode_timestamp(15);
    TEST_ASSERT_EQUAL_HEX8(0x0F, encoded);
}

void test_timestamp_encode_31ms(void) {
    /* Upper boundary of linear range */
    uint8_t encoded = cwstream_encode_timestamp(31);
    TEST_ASSERT_EQUAL_HEX8(0x1F, encoded);
}

/*===========================================================================*/
/* Range 2: Medium Range (32-156ms, 4ms resolution)                          */
/*===========================================================================*/

void test_timestamp_encode_32ms(void) {
    /* Transition boundary from linear */
    uint8_t encoded = cwstream_encode_timestamp(32);
    TEST_ASSERT_EQUAL_HEX8(0x20, encoded);
}

void test_timestamp_encode_36ms(void) {
    /* First 4ms step: (36-32)/4 = 1, 0x20+1 = 0x21 */
    uint8_t encoded = cwstream_encode_timestamp(36);
    TEST_ASSERT_EQUAL_HEX8(0x21, encoded);
}

void test_timestamp_encode_60ms(void) {
    /* (60-32)/4 = 7, 0x20+7 = 0x27 */
    uint8_t encoded = cwstream_encode_timestamp(60);
    TEST_ASSERT_EQUAL_HEX8(0x27, encoded);
}

void test_timestamp_encode_100ms(void) {
    /* (100-32)/4 = 17, 0x20+17 = 0x31 */
    uint8_t encoded = cwstream_encode_timestamp(100);
    TEST_ASSERT_EQUAL_HEX8(0x31, encoded);
}

void test_timestamp_encode_156ms(void) {
    /* Upper boundary of medium range: (156-32)/4 = 31, 0x20+31 = 0x3F */
    uint8_t encoded = cwstream_encode_timestamp(156);
    TEST_ASSERT_EQUAL_HEX8(0x3F, encoded);
}

/*===========================================================================*/
/* Range 3: Long Range (157-1165ms, 16ms resolution)                         */
/*===========================================================================*/

void test_timestamp_encode_157ms(void) {
    /* Transition boundary to long range */
    uint8_t encoded = cwstream_encode_timestamp(157);
    TEST_ASSERT_EQUAL_HEX8(0x40, encoded);
}

void test_timestamp_encode_173ms(void) {
    /* First 16ms step: (173-157)/16 = 1, 0x40+1 = 0x41 */
    uint8_t encoded = cwstream_encode_timestamp(173);
    TEST_ASSERT_EQUAL_HEX8(0x41, encoded);
}

void test_timestamp_encode_500ms(void) {
    /* (500-157)/16 = 21, 0x40+21 = 0x55 */
    uint8_t encoded = cwstream_encode_timestamp(500);
    TEST_ASSERT_EQUAL_HEX8(0x55, encoded);
}

void test_timestamp_encode_1000ms(void) {
    /* (1000-157)/16 = 52, 0x40+52 = 0x74 (64+52=116=0x74) */
    uint8_t encoded = cwstream_encode_timestamp(1000);
    TEST_ASSERT_EQUAL_HEX8(0x74, encoded);
}

void test_timestamp_encode_1165ms(void) {
    /* Maximum encodable value: (1165-157)/16 = 63, 0x40+63 = 0x7F */
    uint8_t encoded = cwstream_encode_timestamp(1165);
    TEST_ASSERT_EQUAL_HEX8(0x7F, encoded);
}

/*===========================================================================*/
/* Edge Cases and Error Handling                                             */
/*===========================================================================*/

void test_timestamp_encode_negative(void) {
    /* Negative clamps to 0 */
    uint8_t encoded = cwstream_encode_timestamp(-1);
    TEST_ASSERT_EQUAL_HEX8(0x00, encoded);
}

void test_timestamp_encode_negative_large(void) {
    /* Large negative clamps to 0 */
    uint8_t encoded = cwstream_encode_timestamp(-100);
    TEST_ASSERT_EQUAL_HEX8(0x00, encoded);
}

void test_timestamp_encode_overflow(void) {
    /* Above max clamps to 0x7F */
    uint8_t encoded = cwstream_encode_timestamp(1166);
    TEST_ASSERT_EQUAL_HEX8(0x7F, encoded);
}

void test_timestamp_encode_large_overflow(void) {
    /* Very large clamps to 0x7F */
    uint8_t encoded = cwstream_encode_timestamp(5000);
    TEST_ASSERT_EQUAL_HEX8(0x7F, encoded);
}

void test_timestamp_encode_int_max(void) {
    /* INT32_MAX clamps to 0x7F */
    uint8_t encoded = cwstream_encode_timestamp(INT_MAX);
    TEST_ASSERT_EQUAL_HEX8(0x7F, encoded);
}

/*===========================================================================*/
/* Resolution Boundary Tests                                                 */
/*===========================================================================*/

void test_timestamp_encode_33ms(void) {
    /* 33 rounds down to 32 (4ms resolution bucket) */
    uint8_t encoded = cwstream_encode_timestamp(33);
    TEST_ASSERT_EQUAL_HEX8(0x20, encoded);
}

void test_timestamp_encode_34ms(void) {
    /* 34 rounds down to 32 */
    uint8_t encoded = cwstream_encode_timestamp(34);
    TEST_ASSERT_EQUAL_HEX8(0x20, encoded);
}

void test_timestamp_encode_35ms(void) {
    /* 35 rounds down to 32 */
    uint8_t encoded = cwstream_encode_timestamp(35);
    TEST_ASSERT_EQUAL_HEX8(0x20, encoded);
}

void test_timestamp_encode_158ms(void) {
    /* 158 rounds down to 157 (16ms resolution bucket) */
    uint8_t encoded = cwstream_encode_timestamp(158);
    TEST_ASSERT_EQUAL_HEX8(0x40, encoded);
}

void test_timestamp_encode_165ms(void) {
    /* 165 rounds down to 157 */
    uint8_t encoded = cwstream_encode_timestamp(165);
    TEST_ASSERT_EQUAL_HEX8(0x40, encoded);
}

/*===========================================================================*/
/* Timestamp Decoding: Inverse of Encoding                                   */
/*===========================================================================*/

void test_timestamp_decode_zero(void) {
    /* Zero decodes to zero */
    int decoded = cwstream_decode_timestamp(0x00);
    TEST_ASSERT_EQUAL_INT(0, decoded);
}

void test_timestamp_decode_linear_max(void) {
    /* Linear range maximum: 0x1F -> 31 */
    int decoded = cwstream_decode_timestamp(0x1F);
    TEST_ASSERT_EQUAL_INT(31, decoded);
}

void test_timestamp_decode_medium_start(void) {
    /* Medium range start: 0x20 -> 32 */
    int decoded = cwstream_decode_timestamp(0x20);
    TEST_ASSERT_EQUAL_INT(32, decoded);
}

void test_timestamp_decode_medium_end(void) {
    /* Medium range end: 0x3F -> 32 + 4*(0x3F-0x20) = 32 + 124 = 156 */
    int decoded = cwstream_decode_timestamp(0x3F);
    TEST_ASSERT_EQUAL_INT(156, decoded);
}

void test_timestamp_decode_long_start(void) {
    /* Long range start: 0x40 -> 157 */
    int decoded = cwstream_decode_timestamp(0x40);
    TEST_ASSERT_EQUAL_INT(157, decoded);
}

void test_timestamp_decode_long_end(void) {
    /* Long range end: 0x7F -> 157 + 16*(0x7F-0x40) = 157 + 1008 = 1165 */
    int decoded = cwstream_decode_timestamp(0x7F);
    TEST_ASSERT_EQUAL_INT(1165, decoded);
}

/*===========================================================================*/
/* Bit 7 Masking (Key Bit Stripping)                                         */
/*===========================================================================*/

void test_timestamp_decode_with_key_bit_0x80(void) {
    /* Key bit should be masked off: 0x80 -> 0 */
    int decoded = cwstream_decode_timestamp(0x80);
    TEST_ASSERT_EQUAL_INT(0, decoded);
}

void test_timestamp_decode_with_key_bit_0x9F(void) {
    /* 0x1F with key bit: 0x9F -> 31 */
    int decoded = cwstream_decode_timestamp(0x9F);
    TEST_ASSERT_EQUAL_INT(31, decoded);
}

void test_timestamp_decode_with_key_bit_0xBF(void) {
    /* 0x3F with key bit: 0xBF -> 156 */
    int decoded = cwstream_decode_timestamp(0xBF);
    TEST_ASSERT_EQUAL_INT(156, decoded);
}

void test_timestamp_decode_with_key_bit_0xFF(void) {
    /* 0x7F with key bit: 0xFF -> 1165 */
    int decoded = cwstream_decode_timestamp(0xFF);
    TEST_ASSERT_EQUAL_INT(1165, decoded);
}

/*===========================================================================*/
/* Round-Trip Property Tests                                                 */
/*===========================================================================*/

void test_timestamp_roundtrip_linear(void) {
    /* For all ms in [0,31]: decode(encode(ms)) == ms */
    for (int ms = 0; ms <= 31; ms++) {
        uint8_t encoded = cwstream_encode_timestamp(ms);
        int decoded = cwstream_decode_timestamp(encoded);
        TEST_ASSERT_EQUAL_INT_MESSAGE(ms, decoded, "Linear range roundtrip failed");
    }
}

void test_timestamp_roundtrip_medium(void) {
    /* For ms in {32,36,40,...,156}: decode(encode(ms)) == ms (exact on boundaries) */
    for (int ms = 32; ms <= 156; ms += 4) {
        uint8_t encoded = cwstream_encode_timestamp(ms);
        int decoded = cwstream_decode_timestamp(encoded);
        TEST_ASSERT_EQUAL_INT_MESSAGE(ms, decoded, "Medium range roundtrip failed");
    }
}

void test_timestamp_roundtrip_long(void) {
    /* For ms in {157,173,...,1165}: decode(encode(ms)) == ms (exact on boundaries) */
    for (int ms = 157; ms <= 1165; ms += 16) {
        uint8_t encoded = cwstream_encode_timestamp(ms);
        int decoded = cwstream_decode_timestamp(encoded);
        TEST_ASSERT_EQUAL_INT_MESSAGE(ms, decoded, "Long range roundtrip failed");
    }
}

/*===========================================================================*/
/* Additional Medium Range Values for Coverage                               */
/*===========================================================================*/

void test_timestamp_decode_medium_0x27(void) {
    /* 0x27 -> 32 + 4*(0x27-0x20) = 32 + 28 = 60 */
    int decoded = cwstream_decode_timestamp(0x27);
    TEST_ASSERT_EQUAL_INT(60, decoded);
}

void test_timestamp_decode_medium_0x31(void) {
    /* 0x31 -> 32 + 4*(0x31-0x20) = 32 + 68 = 100 */
    int decoded = cwstream_decode_timestamp(0x31);
    TEST_ASSERT_EQUAL_INT(100, decoded);
}

/*===========================================================================*/
/* Additional Long Range Values for Coverage                                 */
/*===========================================================================*/

void test_timestamp_decode_long_0x55(void) {
    /* 0x55 -> 157 + 16*(0x55-0x40) = 157 + 336 = 493 */
    /* Note: test plan says 500ms encodes to 0x55, but decode gives 493 due to quantization */
    int decoded = cwstream_decode_timestamp(0x55);
    TEST_ASSERT_EQUAL_INT(493, decoded);
}

void test_timestamp_decode_long_0x72(void) {
    /* 0x72 -> 157 + 16*(0x72-0x40) = 157 + 800 = 957 */
    /* Note: test plan says 1000ms encodes to 0x72, but decode gives 957 due to quantization */
    int decoded = cwstream_decode_timestamp(0x72);
    TEST_ASSERT_EQUAL_INT(957, decoded);
}
