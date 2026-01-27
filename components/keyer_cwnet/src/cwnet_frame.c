/**
 * @file cwnet_frame.c
 * @brief CWNet frame parser implementation
 */

#include "cwnet_frame.h"
#include <string.h>

/* Command byte masks */
#define CWNET_CMD_MASK_BLOCKLEN  0xC0
#define CWNET_CMD_MASK_COMMAND   0x3F

cwnet_frame_category_t cwnet_frame_get_category(uint8_t cmd_byte) {
    return (cwnet_frame_category_t)((cmd_byte >> 6) & 0x03);
}

uint8_t cwnet_frame_get_command(uint8_t cmd_byte) {
    return cmd_byte & CWNET_CMD_MASK_COMMAND;
}

void cwnet_frame_parser_init(cwnet_frame_parser_t *parser) {
    if (parser == NULL) {
        return;
    }
    memset(parser, 0, sizeof(*parser));
    parser->state = CWNET_PARSER_STATE_COMMAND;
}

void cwnet_frame_parser_reset(cwnet_frame_parser_t *parser) {
    if (parser == NULL) {
        return;
    }
    parser->state = CWNET_PARSER_STATE_COMMAND;
    parser->command = 0;
    parser->category = CWNET_FRAME_CAT_NO_PAYLOAD;
    parser->payload_len = 0;
    parser->payload_received = 0;
    parser->length_byte_1 = 0;
}

cwnet_parse_result_t cwnet_frame_parse(cwnet_frame_parser_t *parser,
                                        const uint8_t *data,
                                        size_t len) {
    cwnet_parse_result_t result = {
        .status = CWNET_PARSE_NEED_MORE,
        .command = 0,
        .payload_len = 0,
        .payload = NULL,
        .bytes_consumed = 0
    };

    if (parser == NULL) {
        result.status = CWNET_PARSE_ERROR;
        return result;
    }

    if (data == NULL || len == 0) {
        return result;  /* NEED_MORE */
    }

    size_t pos = 0;

    while (pos < len) {
        switch (parser->state) {
            case CWNET_PARSER_STATE_COMMAND: {
                parser->command = data[pos];
                parser->category = cwnet_frame_get_category(parser->command);
                pos++;

                if (parser->category == CWNET_FRAME_CAT_RESERVED) {
                    result.status = CWNET_PARSE_ERROR;
                    result.bytes_consumed = pos;
                    cwnet_frame_parser_reset(parser);
                    return result;
                }

                if (parser->category == CWNET_FRAME_CAT_NO_PAYLOAD) {
                    /* Frame complete - no payload */
                    result.status = CWNET_PARSE_OK;
                    result.command = cwnet_frame_get_command(parser->command);
                    result.payload_len = 0;
                    result.payload = NULL;
                    result.bytes_consumed = pos;
                    cwnet_frame_parser_reset(parser);
                    return result;
                }

                /* Need length byte(s) */
                parser->state = CWNET_PARSER_STATE_LENGTH_1;
                break;
            }

            case CWNET_PARSER_STATE_LENGTH_1: {
                if (parser->category == CWNET_FRAME_CAT_SHORT_PAYLOAD) {
                    /* Single length byte */
                    parser->payload_len = data[pos];
                    pos++;
                    parser->payload_received = 0;

                    if (parser->payload_len == 0) {
                        /* Zero-length payload is valid */
                        result.status = CWNET_PARSE_OK;
                        result.command = cwnet_frame_get_command(parser->command);
                        result.payload_len = 0;
                        result.payload = NULL;
                        result.bytes_consumed = pos;
                        cwnet_frame_parser_reset(parser);
                        return result;
                    }

                    parser->state = CWNET_PARSER_STATE_PAYLOAD;
                } else {
                    /* Long block - first of two length bytes */
                    parser->length_byte_1 = data[pos];
                    pos++;
                    parser->state = CWNET_PARSER_STATE_LENGTH_2;
                }
                break;
            }

            case CWNET_PARSER_STATE_LENGTH_2: {
                /* Long block - second length byte (little-endian) */
                uint16_t len_high = (uint16_t)data[pos];
                parser->payload_len = parser->length_byte_1 | (len_high << 8);
                pos++;
                parser->payload_received = 0;

                if (parser->payload_len == 0) {
                    /* Zero-length payload is valid */
                    result.status = CWNET_PARSE_OK;
                    result.command = cwnet_frame_get_command(parser->command);
                    result.payload_len = 0;
                    result.payload = NULL;
                    result.bytes_consumed = pos;
                    cwnet_frame_parser_reset(parser);
                    return result;
                }

                parser->state = CWNET_PARSER_STATE_PAYLOAD;
                break;
            }

            case CWNET_PARSER_STATE_PAYLOAD: {
                size_t remaining = len - pos;
                size_t needed = (size_t)(parser->payload_len - parser->payload_received);

                if (remaining >= needed) {
                    /* Have all payload data */
                    if (parser->payload_received == 0) {
                        /* All payload in current buffer - return pointer directly */
                        result.payload = &data[pos];
                    } else {
                        /* Payload was fragmented - copy remainder to internal buffer */
                        size_t copy_len = needed;
                        if (parser->payload_received + copy_len <= CWNET_FRAME_PARSER_BUF_SIZE) {
                            memcpy(&parser->payload_buf[parser->payload_received],
                                   &data[pos], copy_len);
                        }
                        result.payload = parser->payload_buf;
                    }

                    pos += needed;
                    result.status = CWNET_PARSE_OK;
                    result.command = cwnet_frame_get_command(parser->command);
                    result.payload_len = parser->payload_len;
                    result.bytes_consumed = pos;
                    cwnet_frame_parser_reset(parser);
                    return result;
                } else {
                    /* Partial payload - buffer it */
                    size_t copy_len = remaining;
                    if (parser->payload_received + copy_len <= CWNET_FRAME_PARSER_BUF_SIZE) {
                        memcpy(&parser->payload_buf[parser->payload_received],
                               &data[pos], copy_len);
                    }
                    parser->payload_received += (uint16_t)copy_len;
                    pos += remaining;
                    /* Stay in PAYLOAD state, return NEED_MORE */
                }
                break;
            }
        }
    }

    result.bytes_consumed = pos;
    return result;
}
