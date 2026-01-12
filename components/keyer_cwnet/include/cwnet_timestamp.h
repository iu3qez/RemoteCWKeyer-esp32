/**
 * @file cwnet_timestamp.h
 * @brief CWNet 7-bit non-linear timestamp encoding/decoding
 *
 * Encoding scheme:
 *   Range 1: 0-31ms   -> 0x00-0x1F (1ms resolution, linear)
 *   Range 2: 32-156ms -> 0x20-0x3F (4ms resolution)
 *   Range 3: 157-1165ms -> 0x40-0x7F (16ms resolution)
 *
 * Bit 7 is reserved for key state in CW stream bytes.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Encode milliseconds to 7-bit CW timestamp
 *
 * @param ms Duration in milliseconds (negative clamps to 0, >1165 clamps to max)
 * @return uint8_t Encoded 7-bit timestamp (0x00-0x7F)
 */
uint8_t cwstream_encode_timestamp(int ms);

/**
 * @brief Decode 7-bit CW timestamp to milliseconds
 *
 * Bit 7 (key bit) is automatically stripped before decoding.
 *
 * @param encoded Encoded timestamp byte (bit 7 ignored)
 * @return int Decoded duration in milliseconds
 */
int cwstream_decode_timestamp(uint8_t encoded);
