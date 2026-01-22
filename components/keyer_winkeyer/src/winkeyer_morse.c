/**
 * @file winkeyer_morse.c
 * @brief ASCII-to-Morse lookup table implementation
 *
 * ITU Morse Code encoding:
 * - Pattern stored as bits: 0=dit, 1=dah
 * - LSB is sent first
 * - Length indicates how many bits to send
 */

#include "winkeyer_morse.h"
#include <ctype.h>

/* ============================================================================
 * ITU Morse Code Table
 * ============================================================================
 *
 * Encoding: bit pattern (LSB first), length
 *
 * Example: A = .- (dit-dah)
 *   - First element: dit = 0 (bit 0)
 *   - Second element: dah = 1 (bit 1)
 *   - Pattern: 0b10 = 2, Length: 2
 *
 * Example: B = -... (dah-dit-dit-dit)
 *   - Pattern: 1,0,0,0 -> 0b0001 = 1, Length: 4
 */

/* Letters A-Z */
static const morse_char_t s_letters[26] = {
    /* A: .-     */ { 0x02, 2 },  /* 0b10 */
    /* B: -...   */ { 0x01, 4 },  /* 0b0001 */
    /* C: -.-.   */ { 0x05, 4 },  /* 0b0101 */
    /* D: -..    */ { 0x01, 3 },  /* 0b001 */
    /* E: .      */ { 0x00, 1 },  /* 0b0 */
    /* F: ..-.   */ { 0x04, 4 },  /* 0b0100 */
    /* G: --.    */ { 0x03, 3 },  /* 0b011 */
    /* H: ....   */ { 0x00, 4 },  /* 0b0000 */
    /* I: ..     */ { 0x00, 2 },  /* 0b00 */
    /* J: .---   */ { 0x0E, 4 },  /* 0b1110 */
    /* K: -.-    */ { 0x05, 3 },  /* 0b101 */
    /* L: .-..   */ { 0x02, 4 },  /* 0b0010 */
    /* M: --     */ { 0x03, 2 },  /* 0b11 */
    /* N: -.     */ { 0x01, 2 },  /* 0b01 */
    /* O: ---    */ { 0x07, 3 },  /* 0b111 */
    /* P: .--.   */ { 0x06, 4 },  /* 0b0110 */
    /* Q: --.-   */ { 0x0B, 4 },  /* 0b1011 */
    /* R: .-.    */ { 0x02, 3 },  /* 0b010 */
    /* S: ...    */ { 0x00, 3 },  /* 0b000 */
    /* T: -      */ { 0x01, 1 },  /* 0b1 */
    /* U: ..-    */ { 0x04, 3 },  /* 0b100 */
    /* V: ...-   */ { 0x08, 4 },  /* 0b1000 */
    /* W: .--    */ { 0x06, 3 },  /* 0b110 */
    /* X: -..-   */ { 0x09, 4 },  /* 0b1001 */
    /* Y: -.--   */ { 0x0D, 4 },  /* 0b1101 */
    /* Z: --..   */ { 0x03, 4 },  /* 0b0011 */
};

/* Digits 0-9 (all have length 5) */
static const morse_char_t s_digits[10] = {
    /* 0: -----  */ { 0x1F, 5 },  /* 0b11111 */
    /* 1: .----  */ { 0x1E, 5 },  /* 0b11110 */
    /* 2: ..---  */ { 0x1C, 5 },  /* 0b11100 */
    /* 3: ...--  */ { 0x18, 5 },  /* 0b11000 */
    /* 4: ....-  */ { 0x10, 5 },  /* 0b10000 */
    /* 5: .....  */ { 0x00, 5 },  /* 0b00000 */
    /* 6: -....  */ { 0x01, 5 },  /* 0b00001 */
    /* 7: --...  */ { 0x03, 5 },  /* 0b00011 */
    /* 8: ---..  */ { 0x07, 5 },  /* 0b00111 */
    /* 9: ----.  */ { 0x0F, 5 },  /* 0b01111 */
};

/* Punctuation (sparse, use switch for lookup) */
static const morse_char_t s_period    = { 0x2A, 6 };  /* .-.-.-  0b101010 */
static const morse_char_t s_comma     = { 0x33, 6 };  /* --..--  0b110011 */
static const morse_char_t s_question  = { 0x0C, 6 };  /* ..--..  0b001100 */
static const morse_char_t s_slash     = { 0x09, 5 };  /* -..-.   0b01001 */
static const morse_char_t s_equals    = { 0x11, 5 };  /* -...-   0b10001 */
static const morse_char_t s_hyphen    = { 0x21, 6 };  /* -....-  0b100001 */

const morse_char_t *morse_lookup(char c) {
    /* Convert to uppercase for case-insensitive lookup */
    char upper = (char)toupper((unsigned char)c);

    /* Letters */
    if (upper >= 'A' && upper <= 'Z') {
        return &s_letters[upper - 'A'];
    }

    /* Digits */
    if (c >= '0' && c <= '9') {
        return &s_digits[c - '0'];
    }

    /* Punctuation */
    switch (c) {
        case '.': return &s_period;
        case ',': return &s_comma;
        case '?': return &s_question;
        case '/': return &s_slash;
        case '=': return &s_equals;
        case '-': return &s_hyphen;
        default:  return NULL;  /* Unsupported character (including space) */
    }
}
