/**
 * @file cwnet_frame.h
 * @brief CWNet frame parser - streaming parser with fragmentation support
 *
 * Frame format:
 *   Command byte bits 7-6 determine block length:
 *     00 = No payload (command only)
 *     01 = Short block (1 byte length follows)
 *     10 = Long block (2 bytes length follow, little-endian)
 *     11 = Reserved
 *
 *   Command byte bits 5-0 = command type (0x00 - 0x3F)
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Frame category based on bits 7-6 of command byte
 */
typedef enum {
    CWNET_FRAME_CAT_NO_PAYLOAD = 0,     /**< bits 7-6 = 00: no payload */
    CWNET_FRAME_CAT_SHORT_PAYLOAD = 1,  /**< bits 7-6 = 01: 1-byte length */
    CWNET_FRAME_CAT_LONG_PAYLOAD = 2,   /**< bits 7-6 = 10: 2-byte length (LE) */
    CWNET_FRAME_CAT_RESERVED = 3        /**< bits 7-6 = 11: reserved */
} cwnet_frame_category_t;

/**
 * @brief Parse result status
 */
typedef enum {
    CWNET_PARSE_OK = 0,         /**< Frame parsed successfully */
    CWNET_PARSE_NEED_MORE,      /**< Need more data to complete frame */
    CWNET_PARSE_ERROR           /**< Parse error (invalid frame) */
} cwnet_parse_status_t;

/**
 * @brief Parser state
 */
typedef enum {
    CWNET_PARSER_STATE_COMMAND = 0,     /**< Waiting for command byte */
    CWNET_PARSER_STATE_LENGTH_1,        /**< Waiting for first length byte */
    CWNET_PARSER_STATE_LENGTH_2,        /**< Waiting for second length byte (long block) */
    CWNET_PARSER_STATE_PAYLOAD          /**< Accumulating payload */
} cwnet_parser_state_t;

/**
 * @brief Maximum internal payload buffer size
 *
 * For large payloads (audio), the parser returns a pointer into the
 * input buffer rather than copying. This buffer is for fragmented
 * small payloads.
 */
#define CWNET_FRAME_PARSER_BUF_SIZE 256

/**
 * @brief Streaming frame parser context
 */
typedef struct {
    cwnet_parser_state_t state;         /**< Current parser state */
    uint8_t command;                    /**< Command byte (when parsed) */
    cwnet_frame_category_t category;    /**< Frame category */
    uint16_t payload_len;               /**< Expected payload length */
    uint16_t payload_received;          /**< Bytes received so far */
    uint8_t length_byte_1;              /**< First length byte (for long block) */
    uint8_t payload_buf[CWNET_FRAME_PARSER_BUF_SIZE]; /**< Internal buffer */
} cwnet_frame_parser_t;

/**
 * @brief Parse result
 */
typedef struct {
    cwnet_parse_status_t status;    /**< Parse status */
    uint8_t command;                /**< Command type (bits 5-0) */
    uint16_t payload_len;           /**< Payload length (0 for no-payload frames) */
    const uint8_t *payload;         /**< Pointer to payload data (NULL if none) */
    size_t bytes_consumed;          /**< Bytes consumed from input buffer */
} cwnet_parse_result_t;

/**
 * @brief Get frame category from command byte
 *
 * @param cmd_byte Command byte
 * @return Frame category
 */
cwnet_frame_category_t cwnet_frame_get_category(uint8_t cmd_byte);

/**
 * @brief Get command type from command byte (bits 5-0)
 *
 * @param cmd_byte Command byte
 * @return Command type (0x00-0x3F)
 */
uint8_t cwnet_frame_get_command(uint8_t cmd_byte);

/**
 * @brief Initialize frame parser
 *
 * @param parser Parser context (may be NULL - no-op)
 */
void cwnet_frame_parser_init(cwnet_frame_parser_t *parser);

/**
 * @brief Reset parser state (discard partial frame)
 *
 * @param parser Parser context (may be NULL - no-op)
 */
void cwnet_frame_parser_reset(cwnet_frame_parser_t *parser);

/**
 * @brief Parse frame data
 *
 * Feed data to the parser. May be called multiple times with fragments.
 * When a complete frame is parsed, returns CWNET_PARSE_OK with frame info.
 *
 * @param parser Parser context (NULL returns error)
 * @param data Input data buffer (may be NULL if len=0)
 * @param len Input data length
 * @return Parse result
 */
cwnet_parse_result_t cwnet_frame_parse(cwnet_frame_parser_t *parser,
                                        const uint8_t *data,
                                        size_t len);
