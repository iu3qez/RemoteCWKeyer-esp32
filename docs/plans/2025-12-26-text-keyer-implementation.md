# Text Keyer Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement text-to-Morse keyer with memory slots and console commands.

**Architecture:** Text keyer runs on Core 1 (bg_task), produces samples on keying_stream. Paddle abort via atomic flag set by rt_task (Core 0). 8 memory slots in NVS.

**Tech Stack:** ESP-IDF 5.x, C11, stdatomic.h, NVS, Unity tests

---

## Task 1: Create keyer_morse Component

**Files:**
- Create: `components/keyer_morse/CMakeLists.txt`
- Create: `components/keyer_morse/include/morse_table.h`
- Create: `components/keyer_morse/src/morse_table.c`
- Create: `test_host/test_morse_table.c`

### Step 1.1: Create component structure

```bash
mkdir -p components/keyer_morse/include components/keyer_morse/src
```

### Step 1.2: Write CMakeLists.txt

Create `components/keyer_morse/CMakeLists.txt`:

```cmake
# keyer_morse - Morse code encoding/decoding table
#
# Shared between text_keyer (encoder) and decoder.
# No dependencies - pure lookup table.

idf_component_register(
    SRCS
        "src/morse_table.c"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
)
```

### Step 1.3: Write morse_table.h

Create `components/keyer_morse/include/morse_table.h`:

```c
/**
 * @file morse_table.h
 * @brief Morse code encoding/decoding table
 *
 * Shared between text_keyer (encoder) and decoder.
 * Supports A-Z, 0-9, punctuation, and prosigns.
 */

#ifndef KEYER_MORSE_TABLE_H
#define KEYER_MORSE_TABLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum pattern length (some prosigns are long) */
#define MORSE_MAX_PATTERN_LEN 8

/**
 * @brief Encode character to morse pattern
 *
 * @param c Character to encode (A-Z case insensitive, 0-9, punctuation)
 * @return Morse pattern string (".-", "-...", etc.) or NULL if not found
 */
const char *morse_encode_char(char c);

/**
 * @brief Encode prosign to morse pattern
 *
 * @param tag Prosign tag including brackets (e.g., "<SK>", "<AR>")
 * @return Morse pattern string or NULL if not found
 */
const char *morse_encode_prosign(const char *tag);

/**
 * @brief Decode morse pattern to character
 *
 * @param pattern Morse pattern (".-", "-...", etc.)
 * @return Character or '\0' if not found
 */
char morse_decode_pattern(const char *pattern);

/**
 * @brief Check if text starts with a prosign tag
 *
 * @param text Text to check
 * @param pattern_out Output: pattern if prosign found (can be NULL)
 * @return Length of prosign tag if found (e.g., 4 for "<SK>"), 0 otherwise
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
```

### Step 1.4: Write test file first (TDD)

Create `test_host/test_morse_table.c`:

```c
/**
 * @file test_morse_table.c
 * @brief Tests for morse table encoding/decoding
 */

#include "unity.h"
#include "morse_table.h"
#include <string.h>

void test_morse_encode_letters(void) {
    TEST_ASSERT_EQUAL_STRING(".-", morse_encode_char('A'));
    TEST_ASSERT_EQUAL_STRING(".-", morse_encode_char('a'));  /* Case insensitive */
    TEST_ASSERT_EQUAL_STRING("-...", morse_encode_char('B'));
    TEST_ASSERT_EQUAL_STRING("--..--", morse_encode_char(','));
    TEST_ASSERT_EQUAL_STRING("..--.-", morse_encode_char('_'));
}

void test_morse_encode_numbers(void) {
    TEST_ASSERT_EQUAL_STRING("-----", morse_encode_char('0'));
    TEST_ASSERT_EQUAL_STRING(".----", morse_encode_char('1'));
    TEST_ASSERT_EQUAL_STRING("----.", morse_encode_char('9'));
}

void test_morse_encode_punctuation(void) {
    TEST_ASSERT_EQUAL_STRING(".-.-.-", morse_encode_char('.'));
    TEST_ASSERT_EQUAL_STRING("..--..", morse_encode_char('?'));
    TEST_ASSERT_EQUAL_STRING("-..-.", morse_encode_char('/'));
}

void test_morse_encode_unknown(void) {
    TEST_ASSERT_NULL(morse_encode_char('$'));
    TEST_ASSERT_NULL(morse_encode_char('\n'));
}

void test_morse_encode_prosigns(void) {
    TEST_ASSERT_EQUAL_STRING("...-.-", morse_encode_prosign("<SK>"));
    TEST_ASSERT_EQUAL_STRING(".-.-.", morse_encode_prosign("<AR>"));
    TEST_ASSERT_EQUAL_STRING("-...-", morse_encode_prosign("<BT>"));
    TEST_ASSERT_EQUAL_STRING("-.--.", morse_encode_prosign("<KN>"));
    TEST_ASSERT_NULL(morse_encode_prosign("<XX>"));
    TEST_ASSERT_NULL(morse_encode_prosign("SK"));  /* No brackets */
}

void test_morse_decode_pattern(void) {
    TEST_ASSERT_EQUAL_CHAR('A', morse_decode_pattern(".-"));
    TEST_ASSERT_EQUAL_CHAR('B', morse_decode_pattern("-..."));
    TEST_ASSERT_EQUAL_CHAR('0', morse_decode_pattern("-----"));
    TEST_ASSERT_EQUAL_CHAR('\0', morse_decode_pattern("........"));  /* Invalid */
}

void test_morse_match_prosign(void) {
    const char *pattern = NULL;

    /* Match at start */
    TEST_ASSERT_EQUAL(4, morse_match_prosign("<SK>", &pattern));
    TEST_ASSERT_EQUAL_STRING("...-.-", pattern);

    /* Match with text after */
    TEST_ASSERT_EQUAL(4, morse_match_prosign("<AR> K", &pattern));
    TEST_ASSERT_EQUAL_STRING(".-.-.", pattern);

    /* No match */
    TEST_ASSERT_EQUAL(0, morse_match_prosign("HELLO", &pattern));
    TEST_ASSERT_EQUAL(0, morse_match_prosign("<XX>", &pattern));

    /* Partial tag */
    TEST_ASSERT_EQUAL(0, morse_match_prosign("<SK", NULL));
}

void test_morse_prosign_roundtrip(void) {
    const char *tag = morse_get_prosign_tag("...-.-");
    TEST_ASSERT_EQUAL_STRING("<SK>", tag);

    TEST_ASSERT_NULL(morse_get_prosign_tag(".-"));  /* Not a prosign */
}
```

### Step 1.5: Add test to CMakeLists.txt

Modify `test_host/CMakeLists.txt`, add to include_directories:

```cmake
    ${COMPONENT_DIR}/keyer_morse/include
```

Add to sources:

```cmake
set(MORSE_SOURCES
    ${COMPONENT_DIR}/keyer_morse/src/morse_table.c
)
```

Add `test_morse_table.c` to TEST_SOURCES.

Add `${MORSE_SOURCES}` to add_executable.

### Step 1.6: Run test to verify it fails

```bash
cd test_host && cmake -B build && cmake --build build && ./build/test_runner
```

Expected: Fails with undefined references to morse_* functions.

### Step 1.7: Implement morse_table.c

Create `components/keyer_morse/src/morse_table.c`:

```c
/**
 * @file morse_table.c
 * @brief Morse code encoding/decoding implementation
 */

#include "morse_table.h"
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * Morse Table (ITU standard)
 * ============================================================================ */

typedef struct {
    char character;
    const char *pattern;
} morse_entry_t;

typedef struct {
    const char *tag;      /* e.g., "<SK>" */
    const char *pattern;  /* e.g., "...-.-" */
} prosign_entry_t;

static const morse_entry_t MORSE_TABLE[] = {
    /* Letters */
    {'A', ".-"},     {'B', "-..."},   {'C', "-.-."},   {'D', "-.."},
    {'E', "."},      {'F', "..-."},   {'G', "--."},    {'H', "...."},
    {'I', ".."},     {'J', ".---"},   {'K', "-.-"},    {'L', ".-.."},
    {'M', "--"},     {'N', "-."},     {'O', "---"},    {'P', ".--."},
    {'Q', "--.-"},   {'R', ".-."},    {'S', "..."},    {'T', "-"},
    {'U', "..-"},    {'V', "...-"},   {'W', ".--"},    {'X', "-..-"},
    {'Y', "-.--"},   {'Z', "--.."},

    /* Numbers */
    {'0', "-----"},  {'1', ".----"},  {'2', "..---"},  {'3', "...--"},
    {'4', "....-"},  {'5', "....."},  {'6', "-...."},  {'7', "--..."},
    {'8', "---.."},  {'9', "----."},

    /* Punctuation */
    {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'!', "-.-.--"},
    {'/', "-..-."}, {'=', "-...-"},  {'+', ".-.-."},  {'-', "-....-"},
    {'@', ".--.-."}, {'_', "..--.-"},

    {0, NULL}  /* Sentinel */
};

static const prosign_entry_t PROSIGN_TABLE[] = {
    {"<SK>", "...-.-"},   /* End of contact */
    {"<AR>", ".-.-."},    /* End of message */
    {"<BT>", "-...-"},    /* Break/Pause */
    {"<KN>", "-.--."},    /* Specific station only */
    {"<AS>", ".-..."},    /* Wait */
    {"<SN>", "...-."}, /* Understood (also VE) */
    {NULL, NULL}  /* Sentinel */
};

#define MORSE_TABLE_SIZE (sizeof(MORSE_TABLE) / sizeof(MORSE_TABLE[0]) - 1)
#define PROSIGN_TABLE_SIZE (sizeof(PROSIGN_TABLE) / sizeof(PROSIGN_TABLE[0]) - 1)

/* ============================================================================
 * Public API
 * ============================================================================ */

const char *morse_encode_char(char c) {
    char upper = (char)toupper((unsigned char)c);

    for (size_t i = 0; i < MORSE_TABLE_SIZE; i++) {
        if (MORSE_TABLE[i].character == upper) {
            return MORSE_TABLE[i].pattern;
        }
    }
    return NULL;
}

const char *morse_encode_prosign(const char *tag) {
    if (tag == NULL || tag[0] != '<') {
        return NULL;
    }

    for (size_t i = 0; i < PROSIGN_TABLE_SIZE; i++) {
        if (strcmp(PROSIGN_TABLE[i].tag, tag) == 0) {
            return PROSIGN_TABLE[i].pattern;
        }
    }
    return NULL;
}

char morse_decode_pattern(const char *pattern) {
    if (pattern == NULL) {
        return '\0';
    }

    /* Check regular characters */
    for (size_t i = 0; i < MORSE_TABLE_SIZE; i++) {
        if (strcmp(MORSE_TABLE[i].pattern, pattern) == 0) {
            return MORSE_TABLE[i].character;
        }
    }

    return '\0';
}

size_t morse_match_prosign(const char *text, const char **pattern_out) {
    if (text == NULL || text[0] != '<') {
        return 0;
    }

    for (size_t i = 0; i < PROSIGN_TABLE_SIZE; i++) {
        size_t len = strlen(PROSIGN_TABLE[i].tag);
        if (strncmp(text, PROSIGN_TABLE[i].tag, len) == 0) {
            if (pattern_out != NULL) {
                *pattern_out = PROSIGN_TABLE[i].pattern;
            }
            return len;
        }
    }

    return 0;
}

const char *morse_get_prosign_tag(const char *pattern) {
    if (pattern == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < PROSIGN_TABLE_SIZE; i++) {
        if (strcmp(PROSIGN_TABLE[i].pattern, pattern) == 0) {
            return PROSIGN_TABLE[i].tag;
        }
    }

    return NULL;
}
```

### Step 1.8: Run tests to verify pass

```bash
cd test_host && cmake --build build && ./build/test_runner
```

Expected: All morse_table tests pass.

### Step 1.9: Commit

```bash
git add components/keyer_morse/ test_host/test_morse_table.c test_host/CMakeLists.txt
git commit -m "feat(morse): add morse_table component with encoding/decoding

Shared morse code table for text_keyer and decoder:
- A-Z, 0-9, punctuation encoding
- Prosigns: <SK>, <AR>, <BT>, <KN>, <AS>, <SN>
- Bidirectional encoding/decoding
- Host tests with Unity"
```

---

## Task 2: Create keyer_text Component - Core

**Files:**
- Create: `components/keyer_text/CMakeLists.txt`
- Create: `components/keyer_text/include/text_keyer.h`
- Create: `components/keyer_text/src/text_keyer.c`
- Create: `test_host/test_text_keyer.c`

### Step 2.1: Create component structure

```bash
mkdir -p components/keyer_text/include components/keyer_text/src
```

### Step 2.2: Write CMakeLists.txt

Create `components/keyer_text/CMakeLists.txt`:

```cmake
# keyer_text - Text-to-Morse keyer
#
# Converts text strings to morse code and produces keying samples.
# Runs on Core 1 (bg_task).

idf_component_register(
    SRCS
        "src/text_keyer.c"
        "src/text_memory.c"
    INCLUDE_DIRS "include"
    REQUIRES keyer_core keyer_morse keyer_config nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
)
```

### Step 2.3: Write text_keyer.h

Create `components/keyer_text/include/text_keyer.h`:

```c
/**
 * @file text_keyer.h
 * @brief Text-to-Morse keyer
 *
 * Converts text to morse code and produces samples on keying stream.
 * Runs on Core 1 (bg_task) with ~10ms tick resolution.
 *
 * Features:
 * - Free-form text via send command
 * - 8 memory slots in NVS
 * - Paddle abort via atomic flag
 * - Uses global WPM from config
 */

#ifndef KEYER_TEXT_KEYER_H
#define KEYER_TEXT_KEYER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include "stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum text length for send command */
#define TEXT_KEYER_MAX_LEN 128

/**
 * @brief Text keyer state
 */
typedef enum {
    TEXT_KEYER_IDLE = 0,      /**< Ready for new text */
    TEXT_KEYER_SENDING,       /**< Transmitting */
    TEXT_KEYER_PAUSED,        /**< Paused (can resume) */
} text_keyer_state_t;

/**
 * @brief Text keyer configuration
 */
typedef struct {
    keying_stream_t *stream;          /**< Output stream (non-owning) */
    const atomic_bool *paddle_abort;  /**< Abort trigger (non-owning, can be NULL) */
} text_keyer_config_t;

/**
 * @brief Initialize text keyer
 *
 * @param config Configuration (stream is required)
 * @return 0 on success, -1 on error
 */
int text_keyer_init(const text_keyer_config_t *config);

/**
 * @brief Send text as morse code
 *
 * @param text Text to send (A-Z, 0-9, punctuation, prosigns, spaces)
 * @return 0 on success, -1 if already sending or invalid
 */
int text_keyer_send(const char *text);

/**
 * @brief Abort current transmission
 */
void text_keyer_abort(void);

/**
 * @brief Pause current transmission
 */
void text_keyer_pause(void);

/**
 * @brief Resume paused transmission
 */
void text_keyer_resume(void);

/**
 * @brief Get current state
 *
 * @return Current state
 */
text_keyer_state_t text_keyer_get_state(void);

/**
 * @brief Get transmission progress
 *
 * @param sent Output: characters sent so far
 * @param total Output: total characters
 */
void text_keyer_get_progress(size_t *sent, size_t *total);

/**
 * @brief Tick function - call from bg_task (~10ms)
 *
 * @param now_us Current timestamp in microseconds
 */
void text_keyer_tick(int64_t now_us);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_TEXT_KEYER_H */
```

### Step 2.4: Write test file first (TDD)

Create `test_host/test_text_keyer.c`:

```c
/**
 * @file test_text_keyer.c
 * @brief Tests for text keyer state machine
 */

#include "unity.h"
#include "text_keyer.h"
#include "stream.h"
#include "config.h"
#include <string.h>

/* Test fixtures */
static stream_sample_t s_buffer[64];
static keying_stream_t s_stream;
static atomic_bool s_paddle_abort;

void setUp_text_keyer(void) {
    stream_init(&s_stream, s_buffer, 64);
    atomic_store(&s_paddle_abort, false);

    text_keyer_config_t cfg = {
        .stream = &s_stream,
        .paddle_abort = &s_paddle_abort,
    };
    text_keyer_init(&cfg);

    /* Set WPM for predictable timing */
    CONFIG_SET_WPM(20);  /* 60ms dit */
}

void test_text_keyer_initial_state(void) {
    setUp_text_keyer();
    TEST_ASSERT_EQUAL(TEXT_KEYER_IDLE, text_keyer_get_state());
}

void test_text_keyer_send_starts_sending(void) {
    setUp_text_keyer();
    TEST_ASSERT_EQUAL(0, text_keyer_send("E"));
    TEST_ASSERT_EQUAL(TEXT_KEYER_SENDING, text_keyer_get_state());
}

void test_text_keyer_send_while_sending_fails(void) {
    setUp_text_keyer();
    text_keyer_send("HELLO");
    TEST_ASSERT_EQUAL(-1, text_keyer_send("WORLD"));
}

void test_text_keyer_send_empty_fails(void) {
    setUp_text_keyer();
    TEST_ASSERT_EQUAL(-1, text_keyer_send(""));
    TEST_ASSERT_EQUAL(-1, text_keyer_send(NULL));
}

void test_text_keyer_abort(void) {
    setUp_text_keyer();
    text_keyer_send("HELLO");
    text_keyer_abort();
    TEST_ASSERT_EQUAL(TEXT_KEYER_IDLE, text_keyer_get_state());
}

void test_text_keyer_pause_resume(void) {
    setUp_text_keyer();
    text_keyer_send("HELLO");

    text_keyer_pause();
    TEST_ASSERT_EQUAL(TEXT_KEYER_PAUSED, text_keyer_get_state());

    text_keyer_resume();
    TEST_ASSERT_EQUAL(TEXT_KEYER_SENDING, text_keyer_get_state());
}

void test_text_keyer_progress(void) {
    setUp_text_keyer();
    text_keyer_send("HI");

    size_t sent, total;
    text_keyer_get_progress(&sent, &total);
    TEST_ASSERT_EQUAL(0, sent);
    TEST_ASSERT_EQUAL(2, total);
}

void test_text_keyer_paddle_abort(void) {
    setUp_text_keyer();
    text_keyer_send("HELLO");

    /* Simulate paddle press */
    atomic_store(&s_paddle_abort, true);
    text_keyer_tick(0);

    TEST_ASSERT_EQUAL(TEXT_KEYER_IDLE, text_keyer_get_state());
}

void test_text_keyer_completes_single_char(void) {
    setUp_text_keyer();
    text_keyer_send("E");  /* Single dit: . */

    /* At 20 WPM: dit = 60ms, intra_gap would apply if more elements */
    /* E is just one dit, so: dit(60ms) + done */

    int64_t t = 0;
    text_keyer_tick(t);  /* Start dit */
    TEST_ASSERT_EQUAL(TEXT_KEYER_SENDING, text_keyer_get_state());

    t += 70000;  /* 70ms - past dit duration */
    text_keyer_tick(t);

    /* Should complete after single dit */
    TEST_ASSERT_EQUAL(TEXT_KEYER_IDLE, text_keyer_get_state());
}

void test_text_keyer_produces_samples(void) {
    setUp_text_keyer();
    size_t initial_pos = stream_write_position(&s_stream);

    text_keyer_send("E");

    int64_t t = 0;
    text_keyer_tick(t);  /* Key down */

    /* Should have pushed at least one sample */
    size_t new_pos = stream_write_position(&s_stream);
    TEST_ASSERT_GREATER_THAN(initial_pos, new_pos);
}
```

### Step 2.5: Update test_host/CMakeLists.txt

Add to include_directories:
```cmake
    ${COMPONENT_DIR}/keyer_text/include
```

Add to sources (after MORSE_SOURCES):
```cmake
set(TEXT_SOURCES
    ${COMPONENT_DIR}/keyer_text/src/text_keyer.c
)
```

Add `test_text_keyer.c` to TEST_SOURCES.

Add `${TEXT_SOURCES}` to add_executable.

### Step 2.6: Run test to verify it fails

```bash
cd test_host && cmake -B build && cmake --build build
```

Expected: Fails with undefined references to text_keyer_* functions.

### Step 2.7: Implement text_keyer.c (state machine only)

Create `components/keyer_text/src/text_keyer.c`:

```c
/**
 * @file text_keyer.c
 * @brief Text-to-Morse keyer implementation
 */

#include "text_keyer.h"
#include "morse_table.h"
#include "config.h"
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * Internal Types
 * ============================================================================ */

typedef enum {
    ELEMENT_DIT,
    ELEMENT_DAH,
    ELEMENT_INTRA_GAP,
    ELEMENT_CHAR_GAP,
    ELEMENT_WORD_GAP,
} element_type_t;

typedef struct {
    char text[TEXT_KEYER_MAX_LEN];
    size_t text_len;
    size_t char_index;
    const char *current_pattern;
    size_t pattern_index;
    element_type_t element;
    int64_t element_end_us;
    bool key_down;
} send_state_t;

/* ============================================================================
 * Module State
 * ============================================================================ */

static keying_stream_t *s_stream = NULL;
static const atomic_bool *s_paddle_abort = NULL;
static text_keyer_state_t s_state = TEXT_KEYER_IDLE;
static send_state_t s_send = {0};

/* ============================================================================
 * Timing Helpers
 * ============================================================================ */

static int64_t dit_duration_us(void) {
    uint32_t wpm = CONFIG_GET_WPM();
    if (wpm < 5) wpm = 5;
    if (wpm > 60) wpm = 60;
    return 1200000 / (int64_t)wpm;
}

/* ============================================================================
 * Sample Production
 * ============================================================================ */

static void emit_sample(bool key_down) {
    if (s_stream == NULL) return;

    stream_sample_t sample = STREAM_SAMPLE_EMPTY;
    sample.local_key = key_down ? 1 : 0;
    if (key_down) {
        sample.flags |= FLAG_LOCAL_EDGE;
    }
    stream_push(s_stream, sample);
}

/* ============================================================================
 * Pattern Navigation
 * ============================================================================ */

static const char *get_next_pattern(void) {
    while (s_send.char_index < s_send.text_len) {
        char c = s_send.text[s_send.char_index];

        /* Check for prosign */
        if (c == '<') {
            const char *pattern = NULL;
            size_t len = morse_match_prosign(&s_send.text[s_send.char_index], &pattern);
            if (len > 0 && pattern != NULL) {
                s_send.char_index += len;
                return pattern;
            }
        }

        /* Space = word gap */
        if (c == ' ') {
            s_send.char_index++;
            return " ";  /* Special marker for word gap */
        }

        /* Regular character */
        const char *pattern = morse_encode_char(c);
        s_send.char_index++;

        if (pattern != NULL) {
            return pattern;
        }
        /* Skip unknown characters */
    }
    return NULL;
}

static bool start_next_element(int64_t now_us) {
    int64_t dit_us = dit_duration_us();

    /* Need new pattern? */
    if (s_send.current_pattern == NULL ||
        s_send.current_pattern[s_send.pattern_index] == '\0') {

        /* Char gap before next character (unless first or word gap) */
        if (s_send.current_pattern != NULL &&
            s_send.current_pattern[0] != ' ') {
            s_send.element = ELEMENT_CHAR_GAP;
            s_send.element_end_us = now_us + (dit_us * 3);
            s_send.key_down = false;
            emit_sample(false);
        }

        s_send.current_pattern = get_next_pattern();
        s_send.pattern_index = 0;

        if (s_send.current_pattern == NULL) {
            return false;  /* Done */
        }

        /* Word gap */
        if (s_send.current_pattern[0] == ' ') {
            s_send.element = ELEMENT_WORD_GAP;
            s_send.element_end_us = now_us + (dit_us * 7);
            s_send.key_down = false;
            emit_sample(false);
            s_send.current_pattern = NULL;  /* Force get next pattern */
            return true;
        }

        /* Skip char gap if we just set one */
        if (s_send.element == ELEMENT_CHAR_GAP) {
            return true;
        }
    }

    /* Send dit or dah */
    char elem = s_send.current_pattern[s_send.pattern_index++];

    if (elem == '.') {
        s_send.element = ELEMENT_DIT;
        s_send.element_end_us = now_us + dit_us;
    } else if (elem == '-') {
        s_send.element = ELEMENT_DAH;
        s_send.element_end_us = now_us + (dit_us * 3);
    } else {
        return start_next_element(now_us);  /* Skip invalid */
    }

    s_send.key_down = true;
    emit_sample(true);
    return true;
}

static void finish_element(int64_t now_us) {
    int64_t dit_us = dit_duration_us();

    if (s_send.key_down) {
        s_send.key_down = false;
        emit_sample(false);

        /* Intra-element gap if more elements in pattern */
        if (s_send.current_pattern != NULL &&
            s_send.current_pattern[s_send.pattern_index] != '\0') {
            s_send.element = ELEMENT_INTRA_GAP;
            s_send.element_end_us = now_us + dit_us;
            return;
        }
    }

    s_send.element_end_us = 0;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

int text_keyer_init(const text_keyer_config_t *config) {
    if (config == NULL || config->stream == NULL) {
        return -1;
    }

    s_stream = config->stream;
    s_paddle_abort = config->paddle_abort;
    s_state = TEXT_KEYER_IDLE;
    memset(&s_send, 0, sizeof(s_send));

    return 0;
}

int text_keyer_send(const char *text) {
    if (text == NULL || text[0] == '\0') {
        return -1;
    }

    if (s_state != TEXT_KEYER_IDLE) {
        return -1;
    }

    size_t len = strlen(text);
    if (len >= TEXT_KEYER_MAX_LEN) {
        len = TEXT_KEYER_MAX_LEN - 1;
    }

    memset(&s_send, 0, sizeof(s_send));
    memcpy(s_send.text, text, len);
    s_send.text[len] = '\0';
    s_send.text_len = len;

    /* Count actual characters (excluding spaces and prosign brackets) */
    s_send.char_index = 0;

    s_state = TEXT_KEYER_SENDING;
    return 0;
}

void text_keyer_abort(void) {
    if (s_state == TEXT_KEYER_IDLE) return;

    if (s_send.key_down) {
        emit_sample(false);
    }

    s_state = TEXT_KEYER_IDLE;
    memset(&s_send, 0, sizeof(s_send));
}

void text_keyer_pause(void) {
    if (s_state != TEXT_KEYER_SENDING) return;

    if (s_send.key_down) {
        emit_sample(false);
        s_send.key_down = false;
    }

    s_state = TEXT_KEYER_PAUSED;
}

void text_keyer_resume(void) {
    if (s_state != TEXT_KEYER_PAUSED) return;

    s_send.element_end_us = 0;  /* Restart timing */
    s_state = TEXT_KEYER_SENDING;
}

text_keyer_state_t text_keyer_get_state(void) {
    return s_state;
}

void text_keyer_get_progress(size_t *sent, size_t *total) {
    if (sent != NULL) {
        *sent = s_send.char_index;
    }
    if (total != NULL) {
        *total = s_send.text_len;
    }
}

void text_keyer_tick(int64_t now_us) {
    if (s_state != TEXT_KEYER_SENDING) return;

    /* Check paddle abort */
    if (s_paddle_abort != NULL &&
        atomic_load_explicit(s_paddle_abort, memory_order_acquire)) {
        text_keyer_abort();
        return;
    }

    /* First tick - start first element */
    if (s_send.element_end_us == 0) {
        if (!start_next_element(now_us)) {
            s_state = TEXT_KEYER_IDLE;
        }
        return;
    }

    /* Check if current element finished */
    if (now_us >= s_send.element_end_us) {
        finish_element(now_us);

        if (s_send.element_end_us == 0) {
            if (!start_next_element(now_us)) {
                s_state = TEXT_KEYER_IDLE;
            }
        }
    }
}
```

### Step 2.8: Run tests to verify pass

```bash
cd test_host && cmake --build build && ./build/test_runner
```

Expected: All text_keyer tests pass.

### Step 2.9: Commit

```bash
git add components/keyer_text/ test_host/test_text_keyer.c test_host/CMakeLists.txt
git commit -m "feat(text): add text_keyer core state machine

Text-to-Morse keyer implementation:
- State machine: IDLE, SENDING, PAUSED
- ITU timing from global WPM config
- Paddle abort via atomic flag
- Prosigns support via morse_table
- Produces samples on keying_stream"
```

---

## Task 3: Add Text Memory (NVS)

**Files:**
- Create: `components/keyer_text/include/text_memory.h`
- Create: `components/keyer_text/src/text_memory.c`

### Step 3.1: Write text_memory.h

Create `components/keyer_text/include/text_memory.h`:

```c
/**
 * @file text_memory.h
 * @brief Memory slots for stored text messages (NVS)
 *
 * 8 slots for frequently used messages (CQ, 73, contest exchanges).
 */

#ifndef KEYER_TEXT_MEMORY_H
#define KEYER_TEXT_MEMORY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of memory slots */
#define TEXT_MEMORY_SLOTS 8

/** Maximum text length per slot */
#define TEXT_MEMORY_MAX_LEN 128

/** Maximum label length */
#define TEXT_MEMORY_LABEL_LEN 16

/**
 * @brief Memory slot structure
 */
typedef struct {
    char text[TEXT_MEMORY_MAX_LEN];
    char label[TEXT_MEMORY_LABEL_LEN];
} text_memory_slot_t;

/**
 * @brief Initialize text memory (load from NVS)
 *
 * @return 0 on success, -1 on error
 */
int text_memory_init(void);

/**
 * @brief Get slot contents
 *
 * @param slot Slot number (0-7)
 * @param out Output slot (can be NULL to just check if valid)
 * @return 0 on success, -1 if slot invalid or empty
 */
int text_memory_get(uint8_t slot, text_memory_slot_t *out);

/**
 * @brief Set slot contents
 *
 * @param slot Slot number (0-7)
 * @param text Text to store (NULL to clear)
 * @param label Label (NULL to keep existing or use default)
 * @return 0 on success, -1 on error
 */
int text_memory_set(uint8_t slot, const char *text, const char *label);

/**
 * @brief Clear slot
 *
 * @param slot Slot number (0-7)
 * @return 0 on success, -1 on error
 */
int text_memory_clear(uint8_t slot);

/**
 * @brief Set slot label only
 *
 * @param slot Slot number (0-7)
 * @param label New label
 * @return 0 on success, -1 on error
 */
int text_memory_set_label(uint8_t slot, const char *label);

/**
 * @brief Save all slots to NVS
 *
 * @return 0 on success, -1 on error
 */
int text_memory_save(void);

/**
 * @brief Check if slot has content
 *
 * @param slot Slot number (0-7)
 * @return true if slot has text, false if empty
 */
bool text_memory_is_set(uint8_t slot);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_TEXT_MEMORY_H */
```

### Step 3.2: Implement text_memory.c

Create `components/keyer_text/src/text_memory.c`:

```c
/**
 * @file text_memory.c
 * @brief Memory slots implementation (NVS persistence)
 */

#include "text_memory.h"
#include <string.h>
#include <stdbool.h>

#ifdef CONFIG_IDF_TARGET
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
static const char *TAG = "text_mem";
#define NVS_NAMESPACE "text_mem"
#endif

/* ============================================================================
 * Module State
 * ============================================================================ */

static text_memory_slot_t s_slots[TEXT_MEMORY_SLOTS];
static bool s_initialized = false;

/* Default messages */
static const text_memory_slot_t DEFAULT_SLOTS[] = {
    { .text = "CQ CQ CQ DE IU3QEZ IU3QEZ K", .label = "CQ" },
    { .text = "73 TU DE IU3QEZ <SK>", .label = "73" },
    { .text = "UR RST 599 599", .label = "RST" },
    { .text = "QTH THIENE THIENE", .label = "QTH" },
    { .text = "", .label = "" },
    { .text = "", .label = "" },
    { .text = "", .label = "" },
    { .text = "", .label = "" },
};

/* ============================================================================
 * NVS Helpers
 * ============================================================================ */

#ifdef CONFIG_IDF_TARGET
static void load_from_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved slots, using defaults");
        return;
    }

    char key[16];
    for (uint8_t i = 0; i < TEXT_MEMORY_SLOTS; i++) {
        size_t len = TEXT_MEMORY_MAX_LEN;
        snprintf(key, sizeof(key), "slot%d_text", i);
        if (nvs_get_str(handle, key, s_slots[i].text, &len) != ESP_OK) {
            continue;  /* Keep default */
        }

        len = TEXT_MEMORY_LABEL_LEN;
        snprintf(key, sizeof(key), "slot%d_label", i);
        nvs_get_str(handle, key, s_slots[i].label, &len);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Loaded slots from NVS");
}

static int save_to_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return -1;
    }

    char key[16];
    for (uint8_t i = 0; i < TEXT_MEMORY_SLOTS; i++) {
        snprintf(key, sizeof(key), "slot%d_text", i);
        nvs_set_str(handle, key, s_slots[i].text);

        snprintf(key, sizeof(key), "slot%d_label", i);
        nvs_set_str(handle, key, s_slots[i].label);
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "Saved slots to NVS");
    return 0;
}
#else
/* Host stubs */
static void load_from_nvs(void) {}
static int save_to_nvs(void) { return 0; }
#endif

/* ============================================================================
 * Public API
 * ============================================================================ */

int text_memory_init(void) {
    /* Load defaults first */
    memcpy(s_slots, DEFAULT_SLOTS, sizeof(s_slots));

    /* Override with NVS if available */
    load_from_nvs();

    s_initialized = true;
    return 0;
}

int text_memory_get(uint8_t slot, text_memory_slot_t *out) {
    if (slot >= TEXT_MEMORY_SLOTS) {
        return -1;
    }

    if (!s_initialized) {
        text_memory_init();
    }

    if (s_slots[slot].text[0] == '\0') {
        return -1;  /* Empty */
    }

    if (out != NULL) {
        *out = s_slots[slot];
    }
    return 0;
}

int text_memory_set(uint8_t slot, const char *text, const char *label) {
    if (slot >= TEXT_MEMORY_SLOTS) {
        return -1;
    }

    if (!s_initialized) {
        text_memory_init();
    }

    if (text == NULL) {
        s_slots[slot].text[0] = '\0';
    } else {
        strncpy(s_slots[slot].text, text, TEXT_MEMORY_MAX_LEN - 1);
        s_slots[slot].text[TEXT_MEMORY_MAX_LEN - 1] = '\0';
    }

    if (label != NULL) {
        strncpy(s_slots[slot].label, label, TEXT_MEMORY_LABEL_LEN - 1);
        s_slots[slot].label[TEXT_MEMORY_LABEL_LEN - 1] = '\0';
    }

    return save_to_nvs();
}

int text_memory_clear(uint8_t slot) {
    return text_memory_set(slot, NULL, "");
}

int text_memory_set_label(uint8_t slot, const char *label) {
    if (slot >= TEXT_MEMORY_SLOTS || label == NULL) {
        return -1;
    }

    if (!s_initialized) {
        text_memory_init();
    }

    strncpy(s_slots[slot].label, label, TEXT_MEMORY_LABEL_LEN - 1);
    s_slots[slot].label[TEXT_MEMORY_LABEL_LEN - 1] = '\0';

    return save_to_nvs();
}

int text_memory_save(void) {
    return save_to_nvs();
}

bool text_memory_is_set(uint8_t slot) {
    if (slot >= TEXT_MEMORY_SLOTS || !s_initialized) {
        return false;
    }
    return s_slots[slot].text[0] != '\0';
}
```

### Step 3.3: Commit

```bash
git add components/keyer_text/include/text_memory.h components/keyer_text/src/text_memory.c
git commit -m "feat(text): add text_memory NVS persistence

8 memory slots for stored messages:
- Default messages: CQ, 73, RST, QTH
- NVS persistence with text_mem namespace
- Label support for display"
```

---

## Task 4: Add Paddle Abort Signal

**Files:**
- Modify: `main/rt_task.c`

### Step 4.1: Add atomic flag for paddle state

Modify `main/rt_task.c`, add after external globals:

```c
/* Paddle state for text keyer abort */
atomic_bool g_paddle_active = ATOMIC_VAR_INIT(false);
```

### Step 4.2: Update RT loop to set flag

In the `rt_task` for loop, after GPIO poll, add:

```c
        /* Update paddle active flag for text keyer abort */
        bool paddle_active = !gpio_is_idle(gpio);
        atomic_store_explicit(&g_paddle_active, paddle_active, memory_order_release);
```

### Step 4.3: Commit

```bash
git add main/rt_task.c
git commit -m "feat(rt): add paddle active atomic flag

Signals text keyer on Core 1 to abort when paddle is pressed.
Updated every 1ms, <11ms worst-case abort latency."
```

---

## Task 5: Add Console Commands

**Files:**
- Modify: `components/keyer_console/src/commands.c`
- Modify: `components/keyer_console/src/completion.c`

### Step 5.1: Add includes and handlers to commands.c

Add at top of commands.c after existing includes:

```c
#include "text_keyer.h"
#include "text_memory.h"
```

Add command handlers before the command table:

```c
/* ============================================================================
 * Text Keyer Commands
 * ============================================================================ */

/**
 * @brief send <text> - Send text as CW
 */
static console_error_t cmd_send(const console_parsed_cmd_t *cmd) {
    if (cmd->argc == 0) {
        return CONSOLE_ERR_MISSING_ARG;
    }

    /* Concatenate all arguments with spaces */
    static char text[TEXT_KEYER_MAX_LEN];
    text[0] = '\0';

    for (int i = 0; i < cmd->argc && cmd->args[i] != NULL; i++) {
        if (i > 0) {
            strncat(text, " ", sizeof(text) - strlen(text) - 1);
        }
        strncat(text, cmd->args[i], sizeof(text) - strlen(text) - 1);
    }

    if (text_keyer_send(text) != 0) {
        printf("Error: already sending or invalid text\r\n");
        return CONSOLE_ERR_INVALID_VALUE;
    }

    printf("Sending: %s\r\n", text);
    return CONSOLE_OK;
}

/**
 * @brief m1-m8 - Send memory slot
 */
static console_error_t cmd_memory_send(const console_parsed_cmd_t *cmd) {
    /* Extract slot number from command name (m1 -> 0, m8 -> 7) */
    const char *name = cmd->command;
    if (name[0] != 'm' || name[1] < '1' || name[1] > '8') {
        return CONSOLE_ERR_UNKNOWN_CMD;
    }

    uint8_t slot = (uint8_t)(name[1] - '1');

    text_memory_slot_t mem;
    if (text_memory_get(slot, &mem) != 0) {
        printf("Slot %d is empty\r\n", slot + 1);
        return CONSOLE_ERR_INVALID_VALUE;
    }

    if (text_keyer_send(mem.text) != 0) {
        printf("Error: already sending\r\n");
        return CONSOLE_ERR_INVALID_VALUE;
    }

    printf("Sending M%d [%s]: %s\r\n", slot + 1, mem.label, mem.text);
    return CONSOLE_OK;
}

/**
 * @brief abort - Abort current transmission
 */
static console_error_t cmd_abort(const console_parsed_cmd_t *cmd) {
    (void)cmd;
    text_keyer_abort();
    printf("Aborted\r\n");
    return CONSOLE_OK;
}

/**
 * @brief pause - Pause transmission
 */
static console_error_t cmd_pause(const console_parsed_cmd_t *cmd) {
    (void)cmd;
    text_keyer_pause();
    printf("Paused\r\n");
    return CONSOLE_OK;
}

/**
 * @brief resume - Resume transmission
 */
static console_error_t cmd_resume(const console_parsed_cmd_t *cmd) {
    (void)cmd;
    text_keyer_resume();
    printf("Resumed\r\n");
    return CONSOLE_OK;
}

/**
 * @brief mem [slot] [text|clear|label <label>] - Memory slot management
 */
static console_error_t cmd_mem(const console_parsed_cmd_t *cmd) {
    /* No args - list all slots */
    if (cmd->argc == 0) {
        for (uint8_t i = 0; i < TEXT_MEMORY_SLOTS; i++) {
            text_memory_slot_t slot;
            if (text_memory_get(i, &slot) == 0) {
                printf("M%d [%s]: %s\r\n", i + 1, slot.label, slot.text);
            } else {
                printf("M%d: (empty)\r\n", i + 1);
            }
        }
        return CONSOLE_OK;
    }

    /* Parse slot number */
    int slot_num = atoi(cmd->args[0]);
    if (slot_num < 1 || slot_num > 8) {
        printf("Error: slot must be 1-8\r\n");
        return CONSOLE_ERR_OUT_OF_RANGE;
    }
    uint8_t slot = (uint8_t)(slot_num - 1);

    /* Just slot number - show that slot */
    if (cmd->argc == 1) {
        text_memory_slot_t mem;
        if (text_memory_get(slot, &mem) == 0) {
            printf("M%d [%s]: %s\r\n", slot + 1, mem.label, mem.text);
        } else {
            printf("M%d: (empty)\r\n", slot + 1);
        }
        return CONSOLE_OK;
    }

    /* Check for subcommands */
    if (strcmp(cmd->args[1], "clear") == 0) {
        text_memory_clear(slot);
        printf("M%d cleared\r\n", slot + 1);
        return CONSOLE_OK;
    }

    if (strcmp(cmd->args[1], "label") == 0) {
        if (cmd->argc < 3 || cmd->args[2] == NULL) {
            return CONSOLE_ERR_MISSING_ARG;
        }
        text_memory_set_label(slot, cmd->args[2]);
        printf("M%d label set to '%s'\r\n", slot + 1, cmd->args[2]);
        return CONSOLE_OK;
    }

    /* Remaining args are the text */
    static char text[TEXT_KEYER_MAX_LEN];
    text[0] = '\0';

    for (int i = 1; i < cmd->argc && cmd->args[i] != NULL; i++) {
        if (i > 1) {
            strncat(text, " ", sizeof(text) - strlen(text) - 1);
        }
        strncat(text, cmd->args[i], sizeof(text) - strlen(text) - 1);
    }

    text_memory_set(slot, text, NULL);
    printf("M%d saved\r\n", slot + 1);
    return CONSOLE_OK;
}
```

### Step 5.2: Add commands to registry

Find the `static const console_cmd_t COMMANDS[]` array and add:

```c
    { "send", "Send text as CW",
      "Usage: send <text>\r\n"
      "  Example: send CQ CQ DE IU3QEZ K",
      cmd_send },
    { "m1", "Send memory slot 1", NULL, cmd_memory_send },
    { "m2", "Send memory slot 2", NULL, cmd_memory_send },
    { "m3", "Send memory slot 3", NULL, cmd_memory_send },
    { "m4", "Send memory slot 4", NULL, cmd_memory_send },
    { "m5", "Send memory slot 5", NULL, cmd_memory_send },
    { "m6", "Send memory slot 6", NULL, cmd_memory_send },
    { "m7", "Send memory slot 7", NULL, cmd_memory_send },
    { "m8", "Send memory slot 8", NULL, cmd_memory_send },
    { "abort", "Abort CW transmission", NULL, cmd_abort },
    { "pause", "Pause CW transmission", NULL, cmd_pause },
    { "resume", "Resume CW transmission", NULL, cmd_resume },
    { "mem", "Memory slot management",
      "Usage: mem [slot] [text|clear|label <label>]\r\n"
      "  mem           List all slots\r\n"
      "  mem 1         Show slot 1\r\n"
      "  mem 1 CQ CQ   Save 'CQ CQ' to slot 1\r\n"
      "  mem 1 clear   Clear slot 1\r\n"
      "  mem 1 label X Set slot 1 label to X",
      cmd_mem },
```

### Step 5.3: Update CMakeLists.txt for keyer_console

Modify `components/keyer_console/CMakeLists.txt`, add `keyer_text` to REQUIRES:

```cmake
    REQUIRES keyer_core keyer_config keyer_usb keyer_text keyer_hal
```

### Step 5.4: Commit

```bash
git add components/keyer_console/src/commands.c components/keyer_console/CMakeLists.txt
git commit -m "feat(console): add text keyer commands

Commands: send, m1-m8, abort, pause, resume, mem
- send <text>: send free-form text as CW
- m1-m8: send memory slots
- mem: manage memory slots (list, set, clear, label)"
```

---

## Task 6: Add Tab Completion

**Files:**
- Modify: `components/keyer_console/src/completion.c`

### Step 6.1: Add completion for mem subcommands

In `completion.c`, add new completion type and function:

```c
typedef enum {
    COMPLETE_COMMAND,
    COMPLETE_PARAM,
    COMPLETE_DIAG,
    COMPLETE_DEBUG,
    COMPLETE_MEM_SLOT,
    COMPLETE_MEM_SUBCMD,
} complete_type_t;

/**
 * @brief Get matching mem slot numbers
 */
static size_t get_matching_mem_slots(const char *prefix, size_t len,
                                     const char **matches, size_t max_matches) {
    static const char *slots[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
    size_t count = 0;

    for (size_t i = 0; i < 8 && count < max_matches; i++) {
        if (strncmp(slots[i], prefix, len) == 0) {
            matches[count++] = slots[i];
        }
    }
    return count;
}

/**
 * @brief Get matching mem subcommands
 */
static size_t get_matching_mem_subcmds(const char *prefix, size_t len,
                                       const char **matches, size_t max_matches) {
    static const char *subcmds[] = {"clear", "label"};
    size_t count = 0;

    for (size_t i = 0; i < 2 && count < max_matches; i++) {
        if (strncmp(subcmds[i], prefix, len) == 0) {
            matches[count++] = subcmds[i];
        }
    }
    return count;
}
```

### Step 6.2: Update console_complete() to handle mem command

In the `console_complete` function, add handling for mem command after the existing checks:

```c
    /* After "mem " - complete slot numbers */
    if (strncmp(line, "mem ", 4) == 0) {
        const char *arg_start = line + 4;
        while (*arg_start == ' ') arg_start++;

        /* Check if we're on first arg (slot) or second arg (subcmd) */
        const char *space = strchr(arg_start, ' ');
        if (space == NULL) {
            /* First arg - slot number */
            type = COMPLETE_MEM_SLOT;
            token_start = arg_start;
            token_len = strlen(arg_start);
        } else {
            /* Second arg - subcommand */
            type = COMPLETE_MEM_SUBCMD;
            token_start = space + 1;
            while (*token_start == ' ') token_start++;
            token_len = strlen(token_start);
        }
    }
```

And add the case handlers:

```c
        case COMPLETE_MEM_SLOT:
            count = get_matching_mem_slots(token_start, token_len, matches, MAX_COMPLETIONS);
            break;

        case COMPLETE_MEM_SUBCMD:
            count = get_matching_mem_subcmds(token_start, token_len, matches, MAX_COMPLETIONS);
            break;
```

### Step 6.3: Commit

```bash
git add components/keyer_console/src/completion.c
git commit -m "feat(console): add tab completion for mem command

Completes:
- mem <slot>: 1-8
- mem <slot> <subcmd>: clear, label"
```

---

## Task 7: Integrate in bg_task

**Files:**
- Modify: `main/bg_task.c`

### Step 7.1: Add includes and initialization

Add at top of `bg_task.c`:

```c
#include "text_keyer.h"
#include "text_memory.h"

/* External paddle abort flag from rt_task */
extern atomic_bool g_paddle_active;
```

### Step 7.2: Initialize text keyer in bg_task

In `bg_task()` function, before the main loop:

```c
    /* Initialize text keyer */
    text_keyer_config_t text_cfg = {
        .stream = &g_keying_stream,
        .paddle_abort = &g_paddle_active,
    };
    text_keyer_init(&text_cfg);
    text_memory_init();

    RT_INFO(&g_bg_log_stream, now_us, "Text keyer initialized");
```

### Step 7.3: Add tick call in loop

In the main loop, add after consumer processing:

```c
        /* Tick text keyer */
        text_keyer_tick(now_us);
```

### Step 7.4: Update CMakeLists.txt for main

Modify `main/CMakeLists.txt`, ensure keyer_text is available:

```cmake
idf_component_register(
    SRCS "main.c" "rt_task.c" "bg_task.c"
    INCLUDE_DIRS "."
    REQUIRES keyer_core keyer_iambic keyer_audio keyer_logging keyer_hal
             keyer_console keyer_config keyer_usb keyer_text
)
```

### Step 7.5: Commit

```bash
git add main/bg_task.c main/CMakeLists.txt
git commit -m "feat(main): integrate text keyer in bg_task

- Initialize text_keyer and text_memory on startup
- Tick text_keyer every 1ms in bg_task loop
- Connect paddle abort signal from rt_task"
```

---

## Task 8: Build and Test

### Step 8.1: Run host tests

```bash
cd test_host && cmake -B build && cmake --build build && ./build/test_runner
```

Expected: All tests pass.

### Step 8.2: Build for ESP32-S3

```bash
idf.py build
```

Expected: Clean build with no warnings.

### Step 8.3: Flash and test

```bash
idf.py flash monitor
```

Test commands:
```
> help
> send E         # Single dit
> send CQ        # "CQ" in morse
> m1             # Send memory slot 1
> mem            # List all slots
> mem 5 TEST     # Save to slot 5
> abort          # Test abort
```

### Step 8.4: Final commit

```bash
git add -A
git commit -m "feat(text): complete text keyer implementation

Text-to-Morse keyer with:
- morse_table component (shared encoder/decoder)
- text_keyer state machine on Core 1
- 8 NVS memory slots with defaults
- Console commands: send, m1-m8, abort, pause, resume, mem
- Tab completion for mem command
- Paddle abort via atomic flag (<11ms latency)
- ITU PARIS timing from global WPM"
```

---

## Summary

| Task | Description | Estimated |
|------|-------------|-----------|
| 1 | keyer_morse component | 30 min |
| 2 | text_keyer core | 45 min |
| 3 | text_memory NVS | 20 min |
| 4 | Paddle abort signal | 10 min |
| 5 | Console commands | 30 min |
| 6 | Tab completion | 15 min |
| 7 | bg_task integration | 15 min |
| 8 | Build and test | 20 min |
| **Total** | | **~3 hours** |
