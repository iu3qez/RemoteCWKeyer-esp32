/**
 * @file winkeyer_morse.h
 * @brief ASCII-to-Morse lookup table for Winkeyer protocol
 *
 * ITU Morse encoding with bit patterns (0=dit, 1=dah, LSB first).
 * Supports A-Z, 0-9, and common punctuation.
 */

#ifndef WINKEYER_MORSE_H
#define WINKEYER_MORSE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Morse character encoding
 *
 * Pattern is LSB-first: bit 0 is the first element sent.
 * 0 = dit, 1 = dah
 *
 * Example: 'A' (.-) = pattern 0b10, length 2
 *   - Send bit 0 (0) = dit
 *   - Send bit 1 (1) = dah
 */
typedef struct {
    uint8_t pattern;  /**< Bit pattern (LSB first, 0=dit, 1=dah) */
    uint8_t length;   /**< Number of elements (1-6) */
} morse_char_t;

/**
 * @brief Look up Morse encoding for ASCII character
 *
 * Supports:
 * - A-Z (case insensitive)
 * - 0-9
 * - Punctuation: . , ? / = -
 *
 * @param c ASCII character to look up
 * @return Pointer to morse_char_t (static), or NULL if unsupported
 *
 * @note Space (' ') returns NULL - handle word space separately
 */
const morse_char_t *morse_lookup(char c);

#ifdef __cplusplus
}
#endif

#endif /* WINKEYER_MORSE_H */
