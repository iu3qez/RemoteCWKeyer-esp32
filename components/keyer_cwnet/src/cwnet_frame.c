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
                parser->payload_len = (uint16_t)(parser->length_byte_1 | (len_high << 8));
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
                        /* Payload was fragmented and completes now - copy the
                         * remainder to the internal buffer. Always fits: a
                         * payload too large to buffer is diverted to SKIP
                         * below the first time fragmentation is seen, before
                         * any byte of it is buffered, so payload_received
                         * only ever becomes nonzero here when the whole
                         * thing fits CWNET_FRAME_PARSER_BUF_SIZE. This branch
                         * is therefore unreachable today, verified by review;
                         * it stays as a fail-closed guard rather than being
                         * "simplified" away, because this parser runs on
                         * firmware already flashed and a future change to the
                         * buffering logic above must not silently reopen it
                         * into an overflow. If it is ever false, the correct
                         * response is CWNET_PARSE_SKIPPED with no payload
                         * (the framing stays in sync, R14's SKIPPED state
                         * exists exactly for this): never a payload whose
                         * declared length is longer than what was actually
                         * copied, which a caller trusting payload_len would
                         * read out of bounds, and never CWNET_PARSE_ERROR,
                         * which would resync the client's parser inside the
                         * payload instead of at the next frame boundary. */
                        size_t copy_len = needed;
                        if (parser->payload_received + copy_len <= CWNET_FRAME_PARSER_BUF_SIZE) {
                            memcpy(&parser->payload_buf[parser->payload_received],
                                   &data[pos], copy_len);
                            result.payload = parser->payload_buf;
                        } else {
                            pos += needed;
                            result.status = CWNET_PARSE_SKIPPED;
                            result.command = cwnet_frame_get_command(parser->command);
                            result.payload_len = 0;
                            result.payload = NULL;
                            result.bytes_consumed = pos;
                            cwnet_frame_parser_reset(parser);
                            return result;
                        }
                    }

                    pos += needed;
                    result.status = CWNET_PARSE_OK;
                    result.command = cwnet_frame_get_command(parser->command);
                    result.payload_len = parser->payload_len;
                    result.bytes_consumed = pos;
                    cwnet_frame_parser_reset(parser);
                    return result;
                } else if (parser->payload_len > CWNET_FRAME_PARSER_BUF_SIZE) {
                    /* This call does not carry the rest of the payload, and
                     * its declared length would never fit the buffer even
                     * fully assembled (R14): consume what is here and switch
                     * to SKIP for the remainder, instead of overflowing
                     * payload_buf or losing frame sync with an ERROR. */
                    parser->payload_received = (uint16_t)(parser->payload_received + remaining);
                    pos += remaining;
                    parser->state = CWNET_PARSER_STATE_SKIP;
                } else {
                    /* Partial payload that still fits - buffer it */
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

            case CWNET_PARSER_STATE_SKIP: {
                /* Discard a fragmented payload too large to buffer (R14).
                 * Consume exactly its declared length -- never more, never
                 * less -- so framing never drifts into what would have been
                 * the payload. Never touches payload_buf. */
                size_t remaining = len - pos;
                size_t needed = (size_t)(parser->payload_len - parser->payload_received);
                size_t consume = remaining < needed ? remaining : needed;

                pos += consume;
                parser->payload_received = (uint16_t)(parser->payload_received + consume);

                if (parser->payload_received >= parser->payload_len) {
                    result.status = CWNET_PARSE_SKIPPED;
                    result.command = cwnet_frame_get_command(parser->command);
                    result.payload_len = 0;
                    result.payload = NULL;
                    result.bytes_consumed = pos;
                    cwnet_frame_parser_reset(parser);
                    return result;
                }
                /* Stay in SKIP state, return NEED_MORE */
                break;
            }
        }
    }

    result.bytes_consumed = pos;
    return result;
}

bool cwnet_frame_build(uint8_t cmd,
                        const uint8_t *payload, size_t payload_len,
                        uint8_t *out_buf, size_t out_buf_size,
                        size_t *out_len) {
    if (out_buf == NULL || out_len == NULL) {
        return false;
    }
    if (payload_len > 0 && payload == NULL) {
        return false;
    }
    if (payload_len > 0xFFFFu) {
        /* Does not fit a 16-bit length field */
        return false;
    }

    cwnet_frame_category_t cat;
    size_t header_len;
    if (payload_len == 0) {
        cat = CWNET_FRAME_CAT_NO_PAYLOAD;
        header_len = 1;
    } else if (payload_len <= 0xFFu) {
        cat = CWNET_FRAME_CAT_SHORT_PAYLOAD;
        header_len = 2;
    } else {
        cat = CWNET_FRAME_CAT_LONG_PAYLOAD;
        header_len = 3;
    }

    size_t total_len = header_len + payload_len;
    if (out_buf_size < total_len) {
        /* Reject without writing */
        return false;
    }

    out_buf[0] = (uint8_t)(((unsigned)cat << 6) | (cmd & CWNET_CMD_MASK_COMMAND));

    if (cat == CWNET_FRAME_CAT_SHORT_PAYLOAD) {
        out_buf[1] = (uint8_t)payload_len;
    } else if (cat == CWNET_FRAME_CAT_LONG_PAYLOAD) {
        out_buf[1] = (uint8_t)(payload_len & 0xFFu);
        out_buf[2] = (uint8_t)((payload_len >> 8) & 0xFFu);
    }

    if (payload_len > 0) {
        memcpy(&out_buf[header_len], payload, payload_len);
    }

    *out_len = total_len;
    return true;
}
