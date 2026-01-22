/**
 * @file morse_table.h
 * @brief Unified ITU Morse code table
 *
 * Provides both encoding (char -> pattern) and decoding (pattern -> char)
 * for the ITU International Morse Code.
 *
 * Two representations:
 * - Bit-packed (morse_char_t): O(1) lookup for encoding, used by winkeyer
 * - String-based: Linear search for decoding received CW patterns
 */

#ifndef KEYER_MORSE_TABLE_H
#define KEYER_MORSE_TABLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Bit-Packed Encoding (char -> morse elements)
 * ============================================================================ */

/**
 * @brief Morse character encoding (bit-packed)
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
 * O(1) for letters and digits (direct array index).
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

/* ============================================================================
 * String-Based Decoding (pattern string -> char)
 * ============================================================================ */

/**
 * @brief Lookup character for a morse pattern string
 *
 * @param pattern Null-terminated string of '.' and '-' (max 8 chars)
 * @return Decoded character, or '\0' if pattern not found
 *
 * Example:
 *   morse_table_lookup(".-")   -> 'A'
 *   morse_table_lookup("...") -> 'S'
 *   morse_table_lookup("xyz") -> '\0' (invalid)
 */
char morse_table_lookup(const char *pattern);

/**
 * @brief Get pattern string for a character (reverse lookup)
 *
 * @param c ASCII character to look up
 * @return Pattern string (static, do not free), or NULL if not found
 *
 * Example:
 *   morse_table_reverse('A') -> ".-"
 *   morse_table_reverse('?') -> "..--.."
 */
const char *morse_table_reverse(char c);

/**
 * @brief Get number of entries in the string-based morse table
 *
 * @return Number of entries
 */
unsigned morse_table_count(void);

/* ============================================================================
 * Prosign Support
 * ============================================================================ */

/**
 * @brief Check if text starts with a prosign tag
 *
 * @param text Text to check (e.g., "<SK>", "<AR>test")
 * @param pattern_out Output: pattern if prosign found (can be NULL)
 * @return Length of prosign tag if found (e.g., 4 for "<SK>"), 0 otherwise
 *
 * Example:
 *   morse_match_prosign("<SK>", &pattern) -> 4, pattern = "...-.-"
 *   morse_match_prosign("HELLO", NULL) -> 0
 */
size_t morse_match_prosign(const char *text, const char **pattern_out);

/**
 * @brief Get prosign display name for pattern
 *
 * @param pattern Morse pattern
 * @return Prosign tag (e.g., "<SK>") or NULL if not a prosign
 */
const char *morse_get_prosign_tag(const char *pattern);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_MORSE_TABLE_H */
