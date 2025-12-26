/**
 * @file morse_table.c
 * @brief ITU Morse code pattern to character lookup
 */

#include "morse_table.h"
#include <stddef.h>
#include <string.h>

/* ============================================================================
 * Morse Code Table (ITU Standard)
 * ============================================================================ */

typedef struct {
    const char *pattern;
    char character;
} morse_entry_t;

/**
 * ITU International Morse Code table
 *
 * Letters: A-Z (26)
 * Numbers: 0-9 (10)
 * Punctuation and prosigns
 */
static const morse_entry_t MORSE_TABLE[] = {
    /* Letters A-Z */
    { ".-",     'A' },
    { "-...",   'B' },
    { "-.-.",   'C' },
    { "-..",    'D' },
    { ".",      'E' },
    { "..-.",   'F' },
    { "--.",    'G' },
    { "....",   'H' },
    { "..",     'I' },
    { ".---",   'J' },
    { "-.-",    'K' },
    { ".-..",   'L' },
    { "--",     'M' },
    { "-.",     'N' },
    { "---",    'O' },
    { ".--.",   'P' },
    { "--.-",   'Q' },
    { ".-.",    'R' },
    { "...",    'S' },
    { "-",      'T' },
    { "..-",    'U' },
    { "...-",   'V' },
    { ".--",    'W' },
    { "-..-",   'X' },
    { "-.--",   'Y' },
    { "--..",   'Z' },

    /* Numbers 0-9 */
    { "-----",  '0' },
    { ".----",  '1' },
    { "..---",  '2' },
    { "...--",  '3' },
    { "....-",  '4' },
    { ".....",  '5' },
    { "-....",  '6' },
    { "--...",  '7' },
    { "---..",  '8' },
    { "----.",  '9' },

    /* Punctuation */
    { ".-.-.-", '.' },  /* Period */
    { "--..--", ',' },  /* Comma */
    { "..--..", '?' },  /* Question mark */
    { ".----.", '\'' }, /* Apostrophe */
    { "-.-.--", '!' },  /* Exclamation (KW) */
    { "-..-.",  '/' },  /* Slash */
    { "-.--.",  '(' },  /* Open parenthesis */
    { "-.--.-", ')' },  /* Close parenthesis */
    { ".-...",  '&' },  /* Ampersand (AS) */
    { "---...", ':' },  /* Colon */
    { "-.-.-.", ';' },  /* Semicolon */
    { "-...-",  '=' },  /* Equals / BT prosign */
    { ".-.-.",  '+' },  /* Plus / AR prosign */
    { "-....-", '-' },  /* Hyphen */
    { "..--.-", '_' },  /* Underscore */
    { ".-..-.", '"' },  /* Quotation mark */
    { "...-..-",'$' },  /* Dollar sign */
    { ".--.-.", '@' },  /* At sign */

    /* Prosigns (mapped to printable characters) */
    { "...-.-", '*' },  /* SK (end of contact) */
    { "-.-.-",  '<' },  /* CT (commence transmission) / KA */
    { "........",'#' }, /* Error signal (8 dots) */
};

#define MORSE_TABLE_SIZE (sizeof(MORSE_TABLE) / sizeof(MORSE_TABLE[0]))

/* ============================================================================
 * Public API
 * ============================================================================ */

char morse_table_lookup(const char *pattern) {
    if (pattern == NULL || pattern[0] == '\0') {
        return '\0';
    }

    for (size_t i = 0; i < MORSE_TABLE_SIZE; i++) {
        if (strcmp(MORSE_TABLE[i].pattern, pattern) == 0) {
            return MORSE_TABLE[i].character;
        }
    }

    return '\0';  /* Not found */
}

const char *morse_table_reverse(char c) {
    /* Convert lowercase to uppercase for lookup */
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }

    for (size_t i = 0; i < MORSE_TABLE_SIZE; i++) {
        if (MORSE_TABLE[i].character == c) {
            return MORSE_TABLE[i].pattern;
        }
    }

    return NULL;  /* Not found */
}

unsigned morse_table_count(void) {
    return (unsigned)MORSE_TABLE_SIZE;
}
