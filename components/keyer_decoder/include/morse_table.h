/**
 * @file morse_table.h
 * @brief ITU Morse code pattern to character lookup
 *
 * Static lookup table for decoding morse patterns (e.g., ".-" -> 'A').
 * Uses linear search over ~50 entries (<5us lookup time).
 */

#ifndef KEYER_MORSE_TABLE_H
#define KEYER_MORSE_TABLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lookup character for a morse pattern
 *
 * @param pattern Null-terminated string of '.' and '-' (max 6 chars)
 * @return Decoded character, or '\0' if pattern not found
 *
 * Example:
 *   morse_table_lookup(".-")   -> 'A'
 *   morse_table_lookup("...") -> 'S'
 *   morse_table_lookup("xyz") -> '\0' (invalid)
 */
char morse_table_lookup(const char *pattern);

/**
 * @brief Get pattern for a character (reverse lookup)
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
 * @brief Get number of entries in the morse table
 *
 * @return Number of entries
 */
unsigned morse_table_count(void);

/**
 * @brief Check if text starts with a prosign tag
 *
 * @param text Text to check (e.g., "<SK>", "<AR>test")
 * @param pattern_out Output: pattern if prosign found (can be NULL)
 * @return Length of prosign tag if found (e.g., 4 for "<SK>"), 0 otherwise
 *
 * Example:
 *   morse_match_prosign("<SK>", &pattern) -> 4, pattern = "...-.-"
 *   morse_match_prosign("<AR> K", &pattern) -> 4, pattern = ".-.-."
 *   morse_match_prosign("HELLO", NULL) -> 0
 */
size_t morse_match_prosign(const char *text, const char **pattern_out);

/**
 * @brief Get prosign display name for pattern
 *
 * @param pattern Morse pattern
 * @return Prosign tag (e.g., "<SK>") or NULL if not a prosign
 *
 * Example:
 *   morse_get_prosign_tag("...-.-") -> "<SK>"
 *   morse_get_prosign_tag(".-") -> NULL (not a prosign)
 */
const char *morse_get_prosign_tag(const char *pattern);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_MORSE_TABLE_H */
