/**
 * @file test_cwnet_frame_parser.c
 * @brief Unit tests for CWNet frame parser (streaming, handles fragmentation)
 *
 * Test plan: thoughts/shared/plans/2026-01-12-cwnet-tdd-test-cases.md Section 3
 *
 * Frame format:
 *   Bit 7-6 of command byte:
 *     00 = No payload
 *     01 = Short block (1 byte length)
 *     10 = Long block (2 bytes length, little-endian)
 *     11 = Reserved
 */

#include "unity.h"
#include "cwnet_frame.h"
#include <string.h>

/*===========================================================================*/
/* 3.1.1 Frame Category Detection                                            */
/*===========================================================================*/

void test_frame_category_no_payload(void) {
    /* bits 7-6 = 00 (DISCONNECT = 0x02) */
    cwnet_frame_category_t cat = cwnet_frame_get_category(0x02);
    TEST_ASSERT_EQUAL(CWNET_FRAME_CAT_NO_PAYLOAD, cat);
}

void test_frame_category_short(void) {
    /* bits 7-6 = 01 (CONNECT = 0x41) */
    cwnet_frame_category_t cat = cwnet_frame_get_category(0x41);
    TEST_ASSERT_EQUAL(CWNET_FRAME_CAT_SHORT_PAYLOAD, cat);
}

void test_frame_category_long(void) {
    /* bits 7-6 = 10 (AUDIO = 0x91) */
    cwnet_frame_category_t cat = cwnet_frame_get_category(0x91);
    TEST_ASSERT_EQUAL(CWNET_FRAME_CAT_LONG_PAYLOAD, cat);
}

void test_frame_category_reserved(void) {
    /* bits 7-6 = 11 */
    cwnet_frame_category_t cat = cwnet_frame_get_category(0xC1);
    TEST_ASSERT_EQUAL(CWNET_FRAME_CAT_RESERVED, cat);
}

/*===========================================================================*/
/* 3.1.2 Command Type Extraction                                             */
/*===========================================================================*/

void test_cmd_type_connect(void) {
    /* CONNECT = 0x41, cmd = 0x01 */
    uint8_t cmd = cwnet_frame_get_command(0x41);
    TEST_ASSERT_EQUAL_HEX8(0x01, cmd);
}

void test_cmd_type_disconnect(void) {
    /* DISCONNECT = 0x02, cmd = 0x02 */
    uint8_t cmd = cwnet_frame_get_command(0x02);
    TEST_ASSERT_EQUAL_HEX8(0x02, cmd);
}

void test_cmd_type_ping(void) {
    /* PING = 0x43, cmd = 0x03 */
    uint8_t cmd = cwnet_frame_get_command(0x43);
    TEST_ASSERT_EQUAL_HEX8(0x03, cmd);
}

void test_cmd_type_morse(void) {
    /* MORSE = 0x50, cmd = 0x10 */
    uint8_t cmd = cwnet_frame_get_command(0x50);
    TEST_ASSERT_EQUAL_HEX8(0x10, cmd);
}

void test_cmd_type_audio(void) {
    /* AUDIO = 0x91, cmd = 0x11 */
    uint8_t cmd = cwnet_frame_get_command(0x91);
    TEST_ASSERT_EQUAL_HEX8(0x11, cmd);
}

/*===========================================================================*/
/* 3.2 Complete Frame Parsing - No Payload                                   */
/*===========================================================================*/

void test_parse_disconnect_frame(void) {
    /* DISCONNECT: [0x02] - no payload */
    uint8_t frame[] = {0x02};
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    cwnet_parse_result_t result = cwnet_frame_parse(&parser, frame, sizeof(frame));

    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, result.status);
    TEST_ASSERT_EQUAL_HEX8(0x02, result.command);
    TEST_ASSERT_EQUAL(0, result.payload_len);
    TEST_ASSERT_EQUAL(1, result.bytes_consumed);
}

/*===========================================================================*/
/* 3.2 Complete Frame Parsing - Short Block (1-byte length)                  */
/*===========================================================================*/

void test_parse_ping_frame(void) {
    /* PING: [0x43, 0x10, <16 bytes>] */
    uint8_t frame[2 + 16];
    frame[0] = 0x43;  /* PING with short block */
    frame[1] = 0x10;  /* Length = 16 */
    memset(&frame[2], 0xAA, 16);  /* Dummy payload */

    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    cwnet_parse_result_t result = cwnet_frame_parse(&parser, frame, sizeof(frame));

    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, result.status);
    TEST_ASSERT_EQUAL_HEX8(0x03, result.command);
    TEST_ASSERT_EQUAL(16, result.payload_len);
    TEST_ASSERT_EQUAL(18, result.bytes_consumed);
    TEST_ASSERT_EQUAL_PTR(&frame[2], result.payload);
}

void test_parse_connect_frame(void) {
    /* CONNECT: [0x41, 0x5C, <92 bytes>] */
    uint8_t frame[2 + 92];
    frame[0] = 0x41;  /* CONNECT with short block */
    frame[1] = 0x5C;  /* Length = 92 */
    memset(&frame[2], 0, 92);
    strcpy((char*)&frame[2], "testuser");
    strcpy((char*)&frame[2 + 44], "testcall");

    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    cwnet_parse_result_t result = cwnet_frame_parse(&parser, frame, sizeof(frame));

    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, result.status);
    TEST_ASSERT_EQUAL_HEX8(0x01, result.command);
    TEST_ASSERT_EQUAL(92, result.payload_len);
    TEST_ASSERT_EQUAL(94, result.bytes_consumed);
}

void test_parse_morse_frame_5bytes(void) {
    /* MORSE: [0x50, 0x05, <5 bytes>] */
    uint8_t frame[] = {0x50, 0x05, 0x80, 0x14, 0x8F, 0x22, 0x9F};

    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    cwnet_parse_result_t result = cwnet_frame_parse(&parser, frame, sizeof(frame));

    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, result.status);
    TEST_ASSERT_EQUAL_HEX8(0x10, result.command);
    TEST_ASSERT_EQUAL(5, result.payload_len);
    TEST_ASSERT_EQUAL(7, result.bytes_consumed);
}

/*===========================================================================*/
/* 3.2 Complete Frame Parsing - Long Block (2-byte length, LE)               */
/*===========================================================================*/

void test_parse_audio_frame_320(void) {
    /* AUDIO: [0x91, 0x40, 0x01, <320 bytes>] - 40ms @ 8kHz */
    uint8_t frame[3 + 320];
    frame[0] = 0x91;  /* AUDIO with long block */
    frame[1] = 0x40;  /* Length low byte = 0x40 */
    frame[2] = 0x01;  /* Length high byte = 0x01 -> 0x0140 = 320 */
    memset(&frame[3], 0x55, 320);

    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    cwnet_parse_result_t result = cwnet_frame_parse(&parser, frame, sizeof(frame));

    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, result.status);
    TEST_ASSERT_EQUAL_HEX8(0x11, result.command);
    TEST_ASSERT_EQUAL(320, result.payload_len);
    TEST_ASSERT_EQUAL(323, result.bytes_consumed);
}

void test_parse_audio_frame_256(void) {
    /* AUDIO: [0x91, 0x00, 0x01, <256 bytes>] */
    uint8_t frame[3 + 256];
    frame[0] = 0x91;
    frame[1] = 0x00;  /* 0x0100 = 256 LE */
    frame[2] = 0x01;
    memset(&frame[3], 0x33, 256);

    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    cwnet_parse_result_t result = cwnet_frame_parse(&parser, frame, sizeof(frame));

    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, result.status);
    TEST_ASSERT_EQUAL(256, result.payload_len);
    TEST_ASSERT_EQUAL(259, result.bytes_consumed);
}

/*===========================================================================*/
/* 3.3 Streaming Parser - Byte by Byte                                       */
/*===========================================================================*/

void test_stream_parse_disconnect_byte_by_byte(void) {
    /* DISCONNECT frame one byte at a time (only 1 byte total) */
    uint8_t frame[] = {0x02};
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    cwnet_parse_result_t result = cwnet_frame_parse(&parser, frame, 1);
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, result.status);
    TEST_ASSERT_EQUAL_HEX8(0x02, result.command);
}

void test_stream_parse_ping_byte_by_byte(void) {
    /* PING frame (18 bytes) fed one byte at a time */
    uint8_t frame[18];
    frame[0] = 0x43;  /* PING */
    frame[1] = 0x10;  /* len = 16 */
    memset(&frame[2], 0xBB, 16);

    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    /* Feed bytes one at a time - should return NEED_MORE until last byte */
    for (size_t i = 0; i < sizeof(frame) - 1; i++) {
        cwnet_parse_result_t result = cwnet_frame_parse(&parser, &frame[i], 1);
        TEST_ASSERT_EQUAL_MESSAGE(CWNET_PARSE_NEED_MORE, result.status,
                                  "Expected NEED_MORE before frame complete");
    }

    /* Final byte completes the frame */
    cwnet_parse_result_t result = cwnet_frame_parse(&parser, &frame[17], 1);
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, result.status);
    TEST_ASSERT_EQUAL_HEX8(0x03, result.command);
    TEST_ASSERT_EQUAL(16, result.payload_len);
}

/*===========================================================================*/
/* 3.3 Streaming Parser - Partial Reception                                  */
/*===========================================================================*/

void test_stream_parse_partial_header(void) {
    /* Receive only command byte, then rest */
    uint8_t frame[] = {0x43, 0x10, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                       0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                       0x0E, 0x0F};
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    /* First: just command byte */
    cwnet_parse_result_t r1 = cwnet_frame_parse(&parser, &frame[0], 1);
    TEST_ASSERT_EQUAL(CWNET_PARSE_NEED_MORE, r1.status);

    /* Then: rest of frame */
    cwnet_parse_result_t r2 = cwnet_frame_parse(&parser, &frame[1], 17);
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, r2.status);
    TEST_ASSERT_EQUAL_HEX8(0x03, r2.command);
}

void test_stream_parse_partial_payload(void) {
    /* Receive header + half payload, then rest */
    uint8_t frame[2 + 16];
    frame[0] = 0x43;
    frame[1] = 0x10;
    memset(&frame[2], 0xCC, 16);

    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    /* First chunk: header + 8 bytes payload */
    cwnet_parse_result_t r1 = cwnet_frame_parse(&parser, frame, 10);
    TEST_ASSERT_EQUAL(CWNET_PARSE_NEED_MORE, r1.status);

    /* Second chunk: remaining 8 bytes */
    cwnet_parse_result_t r2 = cwnet_frame_parse(&parser, &frame[10], 8);
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, r2.status);
    TEST_ASSERT_EQUAL(16, r2.payload_len);
}

/*===========================================================================*/
/* 3.3 Streaming Parser - Multiple Frames                                    */
/*===========================================================================*/

void test_stream_parse_two_disconnects(void) {
    /* Two DISCONNECT frames back-to-back */
    uint8_t frames[] = {0x02, 0x02};
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    /* Parse first */
    cwnet_parse_result_t r1 = cwnet_frame_parse(&parser, frames, 2);
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, r1.status);
    TEST_ASSERT_EQUAL(1, r1.bytes_consumed);

    /* Reset and parse second */
    cwnet_frame_parser_reset(&parser);
    cwnet_parse_result_t r2 = cwnet_frame_parse(&parser, &frames[1], 1);
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, r2.status);
}

void test_stream_parse_disconnect_then_ping(void) {
    /* DISCONNECT + PING in one buffer */
    uint8_t frames[1 + 18];
    frames[0] = 0x02;  /* DISCONNECT */
    frames[1] = 0x43;  /* PING */
    frames[2] = 0x10;
    memset(&frames[3], 0xDD, 16);

    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    /* Parse DISCONNECT */
    cwnet_parse_result_t r1 = cwnet_frame_parse(&parser, frames, sizeof(frames));
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, r1.status);
    TEST_ASSERT_EQUAL_HEX8(0x02, r1.command);
    TEST_ASSERT_EQUAL(1, r1.bytes_consumed);

    /* Reset and parse PING from remaining buffer */
    cwnet_frame_parser_reset(&parser);
    cwnet_parse_result_t r2 = cwnet_frame_parse(&parser, &frames[1], 18);
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, r2.status);
    TEST_ASSERT_EQUAL_HEX8(0x03, r2.command);
}

/*===========================================================================*/
/* 3.4 Error Handling                                                        */
/*===========================================================================*/

void test_parse_reserved_category(void) {
    /* Frame with bits 7-6 = 11 (reserved) */
    uint8_t frame[] = {0xC1};
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    cwnet_parse_result_t result = cwnet_frame_parse(&parser, frame, 1);
    TEST_ASSERT_EQUAL(CWNET_PARSE_ERROR, result.status);
}

void test_parse_incomplete_returns_need_more(void) {
    /* Only header, no payload for short block frame */
    uint8_t frame[] = {0x43, 0x10};  /* PING header only, needs 16 more bytes */
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    cwnet_parse_result_t result = cwnet_frame_parse(&parser, frame, 2);
    TEST_ASSERT_EQUAL(CWNET_PARSE_NEED_MORE, result.status);
}

void test_parse_zero_length_short_block(void) {
    /* MORSE with length=0: valid frame with empty payload */
    uint8_t frame[] = {0x50, 0x00};
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    cwnet_parse_result_t result = cwnet_frame_parse(&parser, frame, 2);
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, result.status);
    TEST_ASSERT_EQUAL_HEX8(0x10, result.command);
    TEST_ASSERT_EQUAL(0, result.payload_len);
    TEST_ASSERT_EQUAL(2, result.bytes_consumed);
}

void test_parse_null_buffer(void) {
    /* NULL buffer should not crash */
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    cwnet_parse_result_t result = cwnet_frame_parse(&parser, NULL, 0);
    TEST_ASSERT_EQUAL(CWNET_PARSE_NEED_MORE, result.status);
}

void test_parse_empty_buffer(void) {
    /* Empty buffer */
    uint8_t frame[1];
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    cwnet_parse_result_t result = cwnet_frame_parse(&parser, frame, 0);
    TEST_ASSERT_EQUAL(CWNET_PARSE_NEED_MORE, result.status);
}

/*===========================================================================*/
/* 3.5 Parser State Reset                                                    */
/*===========================================================================*/

void test_parser_reset_clears_state(void) {
    /* Parse partial frame, reset, parse new frame */
    uint8_t partial[] = {0x43, 0x10, 0x01, 0x02};  /* Incomplete PING */
    uint8_t complete[] = {0x02};  /* Complete DISCONNECT */

    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    /* Start parsing incomplete frame */
    cwnet_parse_result_t r1 = cwnet_frame_parse(&parser, partial, sizeof(partial));
    TEST_ASSERT_EQUAL(CWNET_PARSE_NEED_MORE, r1.status);

    /* Reset and parse new complete frame */
    cwnet_frame_parser_reset(&parser);
    cwnet_parse_result_t r2 = cwnet_frame_parse(&parser, complete, sizeof(complete));
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, r2.status);
    TEST_ASSERT_EQUAL_HEX8(0x02, r2.command);
}

void test_parser_init_state(void) {
    /* Fresh parser has clean initial state */
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    /* Should be ready to parse */
    uint8_t frame[] = {0x02};
    cwnet_parse_result_t result = cwnet_frame_parse(&parser, frame, 1);
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, result.status);
}

void test_parser_null_safety(void) {
    /* NULL parser should not crash */
    cwnet_frame_parser_init(NULL);
    cwnet_frame_parser_reset(NULL);

    cwnet_parse_result_t result = cwnet_frame_parse(NULL, NULL, 0);
    TEST_ASSERT_EQUAL(CWNET_PARSE_ERROR, result.status);
}

/*===========================================================================*/
/* Additional: Long block partial length                                     */
/*===========================================================================*/

void test_stream_parse_long_block_partial_length(void) {
    /* Long block with length split across two reads */
    uint8_t chunk1[] = {0x91, 0x40};  /* AUDIO + first length byte */
    uint8_t chunk2[] = {0x01};         /* Second length byte */
    uint8_t payload[320];
    memset(payload, 0x77, 320);

    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);

    /* First chunk: cmd + 1 length byte */
    cwnet_parse_result_t r1 = cwnet_frame_parse(&parser, chunk1, 2);
    TEST_ASSERT_EQUAL(CWNET_PARSE_NEED_MORE, r1.status);

    /* Second chunk: 2nd length byte */
    cwnet_parse_result_t r2 = cwnet_frame_parse(&parser, chunk2, 1);
    TEST_ASSERT_EQUAL(CWNET_PARSE_NEED_MORE, r2.status);

    /* Third chunk: payload */
    cwnet_parse_result_t r3 = cwnet_frame_parse(&parser, payload, 320);
    TEST_ASSERT_EQUAL(CWNET_PARSE_OK, r3.status);
    TEST_ASSERT_EQUAL(320, r3.payload_len);
}
