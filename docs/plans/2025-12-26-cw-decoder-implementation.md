# CW Decoder Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement local CW decoder that shows transmitted morse code as text on console and future WebUI.

**Architecture:** Best-effort consumer reads from keying_stream_t, timing classifier (EMA) detects dit/dah/gaps, pattern decoder accumulates and looks up in static morse table, circular buffer stores decoded chars with timestamps.

**Tech Stack:** C11, ESP-IDF, Unity tests, static allocation only

---

## Task 1: Create morse_table Component

**Files:**
- Create: `components/keyer_decoder/CMakeLists.txt`
- Create: `components/keyer_decoder/include/morse_table.h`
- Create: `components/keyer_decoder/src/morse_table.c`
- Create: `test_host/test_morse_table.c`

**Step 1: Create component CMakeLists.txt**

```cmake
# components/keyer_decoder/CMakeLists.txt
idf_component_register(
    SRCS
        "src/morse_table.c"
        "src/timing_classifier.c"
        "src/decoder.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        keyer_core
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror
    -Wconversion -Wsign-conversion
)
```

**Step 2: Create morse_table.h**

```c
/* components/keyer_decoder/include/morse_table.h */
#ifndef MORSE_TABLE_H
#define MORSE_TABLE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lookup morse pattern and return ASCII character
 * @param pattern String of '.' and '-' (e.g., ".-" for 'A')
 * @return ASCII character, or '\0' if not found
 */
char morse_table_lookup(const char *pattern);

/**
 * @brief Get number of entries in morse table
 * @return Number of patterns
 */
int morse_table_size(void);

#ifdef __cplusplus
}
#endif

#endif /* MORSE_TABLE_H */
```

**Step 3: Create test file**

```c
/* test_host/test_morse_table.c */
#include "unity.h"
#include "morse_table.h"

void test_morse_lookup_letters(void) {
    TEST_ASSERT_EQUAL_CHAR('A', morse_table_lookup(".-"));
    TEST_ASSERT_EQUAL_CHAR('B', morse_table_lookup("-..."));
    TEST_ASSERT_EQUAL_CHAR('E', morse_table_lookup("."));
    TEST_ASSERT_EQUAL_CHAR('T', morse_table_lookup("-"));
    TEST_ASSERT_EQUAL_CHAR('S', morse_table_lookup("..."));
    TEST_ASSERT_EQUAL_CHAR('O', morse_table_lookup("---"));
    TEST_ASSERT_EQUAL_CHAR('Z', morse_table_lookup("--.."));
}

void test_morse_lookup_numbers(void) {
    TEST_ASSERT_EQUAL_CHAR('0', morse_table_lookup("-----"));
    TEST_ASSERT_EQUAL_CHAR('1', morse_table_lookup(".----"));
    TEST_ASSERT_EQUAL_CHAR('5', morse_table_lookup("....."));
    TEST_ASSERT_EQUAL_CHAR('9', morse_table_lookup("----."));
}

void test_morse_lookup_punctuation(void) {
    TEST_ASSERT_EQUAL_CHAR('.', morse_table_lookup(".-.-.-"));
    TEST_ASSERT_EQUAL_CHAR(',', morse_table_lookup("--..--"));
    TEST_ASSERT_EQUAL_CHAR('?', morse_table_lookup("..--.."));
}

void test_morse_lookup_invalid(void) {
    TEST_ASSERT_EQUAL_CHAR('\0', morse_table_lookup(""));
    TEST_ASSERT_EQUAL_CHAR('\0', morse_table_lookup("........"));
    TEST_ASSERT_EQUAL_CHAR('\0', morse_table_lookup("xyz"));
    TEST_ASSERT_EQUAL_CHAR('\0', morse_table_lookup(NULL));
}

void test_morse_table_size(void) {
    TEST_ASSERT_GREATER_THAN(40, morse_table_size());
}
```

**Step 4: Run test to verify it fails**

Run:
```bash
cd test_host && cmake -B build && cmake --build build 2>&1 | head -20
```
Expected: FAIL - morse_table.h not found

**Step 5: Create morse_table.c implementation**

```c
/* components/keyer_decoder/src/morse_table.c */
#include "morse_table.h"
#include <string.h>
#include <stddef.h>

typedef struct {
    const char *pattern;
    char character;
} morse_entry_t;

static const morse_entry_t MORSE_TABLE[] = {
    /* Letters A-Z */
    { ".-",    'A' },
    { "-...",  'B' },
    { "-.-.",  'C' },
    { "-..",   'D' },
    { ".",     'E' },
    { "..-.",  'F' },
    { "--.",   'G' },
    { "....",  'H' },
    { "..",    'I' },
    { ".---",  'J' },
    { "-.-",   'K' },
    { ".-..",  'L' },
    { "--",    'M' },
    { "-.",    'N' },
    { "---",   'O' },
    { ".--.",  'P' },
    { "--.-",  'Q' },
    { ".-.",   'R' },
    { "...",   'S' },
    { "-",     'T' },
    { "..-",   'U' },
    { "...-",  'V' },
    { ".--",   'W' },
    { "-..-",  'X' },
    { "-.--",  'Y' },
    { "--..",  'Z' },

    /* Numbers 0-9 */
    { "-----", '0' },
    { ".----", '1' },
    { "..---", '2' },
    { "...--", '3' },
    { "....-", '4' },
    { ".....", '5' },
    { "-....", '6' },
    { "--...", '7' },
    { "---..", '8' },
    { "----.", '9' },

    /* Punctuation */
    { ".-.-.-", '.' },
    { "--..--", ',' },
    { "..--..", '?' },
    { ".----.", '\'' },
    { "-.-.--", '!' },
    { "-..-.",  '/' },
    { "-.--.",  '(' },
    { "-.--.-", ')' },
    { ".-...",  '&' },
    { "---...", ':' },
    { "-.-.-.", ';' },
    { "-...-",  '=' },
    { ".-.-.",  '+' },
    { "-....-", '-' },
    { "..--.-", '_' },
    { ".-..-.", '"' },
    { "...-..-", '$' },
    { ".--.-.", '@' },
};

#define MORSE_TABLE_SIZE (sizeof(MORSE_TABLE) / sizeof(MORSE_TABLE[0]))

char morse_table_lookup(const char *pattern) {
    if (pattern == NULL || pattern[0] == '\0') {
        return '\0';
    }

    for (size_t i = 0; i < MORSE_TABLE_SIZE; i++) {
        if (strcmp(MORSE_TABLE[i].pattern, pattern) == 0) {
            return MORSE_TABLE[i].character;
        }
    }

    return '\0';
}

int morse_table_size(void) {
    return (int)MORSE_TABLE_SIZE;
}
```

**Step 6: Update test_host/CMakeLists.txt**

Add to include_directories:
```cmake
${COMPONENT_DIR}/keyer_decoder/include
```

Add to sources:
```cmake
set(DECODER_SOURCES
    ${COMPONENT_DIR}/keyer_decoder/src/morse_table.c
)
```

Add test file to TEST_SOURCES:
```cmake
test_morse_table.c
```

Add DECODER_SOURCES to add_executable.

**Step 7: Update test_main.c**

Add declarations:
```c
void test_morse_lookup_letters(void);
void test_morse_lookup_numbers(void);
void test_morse_lookup_punctuation(void);
void test_morse_lookup_invalid(void);
void test_morse_table_size(void);
```

Add test runs:
```c
printf("\n=== Morse Table Tests ===\n");
RUN_TEST(test_morse_lookup_letters);
RUN_TEST(test_morse_lookup_numbers);
RUN_TEST(test_morse_lookup_punctuation);
RUN_TEST(test_morse_lookup_invalid);
RUN_TEST(test_morse_table_size);
```

**Step 8: Run tests to verify they pass**

Run:
```bash
cd test_host && cmake -B build && cmake --build build && ./build/test_runner 2>&1 | grep -A20 "Morse Table"
```
Expected: All morse table tests PASS

**Step 9: Commit**

```bash
git add components/keyer_decoder/ test_host/test_morse_table.c test_host/CMakeLists.txt test_host/test_main.c
git commit -m "feat(decoder): add morse_table with ITU patterns"
```

---

## Task 2: Implement Timing Classifier

**Files:**
- Create: `components/keyer_decoder/include/timing_classifier.h`
- Create: `components/keyer_decoder/src/timing_classifier.c`
- Create: `test_host/test_timing_classifier.c`

**Step 1: Create timing_classifier.h**

```c
/* components/keyer_decoder/include/timing_classifier.h */
#ifndef TIMING_CLASSIFIER_H
#define TIMING_CLASSIFIER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Classified key event type */
typedef enum {
    KEY_EVENT_DIT = 0,
    KEY_EVENT_DAH = 1,
    KEY_EVENT_INTRA_GAP = 2,
    KEY_EVENT_CHAR_GAP = 3,
    KEY_EVENT_WORD_GAP = 4,
    KEY_EVENT_UNKNOWN = 255
} key_event_t;

/** Timing classifier state */
typedef struct {
    int64_t dit_avg_us;
    int64_t dah_avg_us;
    uint32_t dit_count;
    uint32_t dah_count;
    float tolerance_pct;
} timing_classifier_t;

/** Timing statistics */
typedef struct {
    int64_t dit_avg_us;
    int64_t dah_avg_us;
    float ratio;
    uint32_t dit_count;
    uint32_t dah_count;
    uint32_t wpm;
} timing_stats_t;

/**
 * @brief Initialize timing classifier
 * @param tc Classifier to initialize
 * @param tolerance_pct Timing tolerance (±%), typically 25.0
 */
void timing_classifier_init(timing_classifier_t *tc, float tolerance_pct);

/**
 * @brief Classify a duration as dit/dah or gap type
 * @param tc Classifier state
 * @param duration_us Duration in microseconds
 * @param was_key_on true=mark (dit/dah), false=space (gap)
 * @return Classified event type
 */
key_event_t timing_classifier_classify(timing_classifier_t *tc,
                                        int64_t duration_us,
                                        bool was_key_on);

/**
 * @brief Get detected WPM
 * @param tc Classifier state
 * @return WPM (0 if not calibrated)
 */
uint32_t timing_classifier_get_wpm(const timing_classifier_t *tc);

/**
 * @brief Get timing statistics
 * @param tc Classifier state
 * @return Statistics structure
 */
timing_stats_t timing_classifier_get_stats(const timing_classifier_t *tc);

/**
 * @brief Reset classifier to defaults
 * @param tc Classifier state
 */
void timing_classifier_reset(timing_classifier_t *tc);

#ifdef __cplusplus
}
#endif

#endif /* TIMING_CLASSIFIER_H */
```

**Step 2: Create test file**

```c
/* test_host/test_timing_classifier.c */
#include "unity.h"
#include "timing_classifier.h"

void test_timing_init(void) {
    timing_classifier_t tc;
    timing_classifier_init(&tc, 25.0f);

    TEST_ASSERT_EQUAL(0, tc.dit_count);
    TEST_ASSERT_EQUAL(0, tc.dah_count);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, tc.tolerance_pct);
}

void test_timing_classify_dit(void) {
    timing_classifier_t tc;
    timing_classifier_init(&tc, 25.0f);

    /* 60ms = dit at 20 WPM */
    key_event_t e = timing_classifier_classify(&tc, 60000, true);
    TEST_ASSERT_EQUAL(KEY_EVENT_DIT, e);
    TEST_ASSERT_EQUAL(1, tc.dit_count);
}

void test_timing_classify_dah(void) {
    timing_classifier_t tc;
    timing_classifier_init(&tc, 25.0f);

    /* Feed some dits first to calibrate */
    timing_classifier_classify(&tc, 60000, true);
    timing_classifier_classify(&tc, 60000, true);
    timing_classifier_classify(&tc, 60000, true);

    /* 180ms = dah (3x dit) */
    key_event_t e = timing_classifier_classify(&tc, 180000, true);
    TEST_ASSERT_EQUAL(KEY_EVENT_DAH, e);
    TEST_ASSERT_EQUAL(1, tc.dah_count);
}

void test_timing_classify_intra_gap(void) {
    timing_classifier_t tc;
    timing_classifier_init(&tc, 25.0f);

    /* Calibrate with dits */
    timing_classifier_classify(&tc, 60000, true);
    timing_classifier_classify(&tc, 60000, true);
    timing_classifier_classify(&tc, 60000, true);

    /* ~60ms gap = intra-character (1 dit unit) */
    key_event_t e = timing_classifier_classify(&tc, 60000, false);
    TEST_ASSERT_EQUAL(KEY_EVENT_INTRA_GAP, e);
}

void test_timing_classify_char_gap(void) {
    timing_classifier_t tc;
    timing_classifier_init(&tc, 25.0f);

    /* Calibrate */
    timing_classifier_classify(&tc, 60000, true);
    timing_classifier_classify(&tc, 60000, true);
    timing_classifier_classify(&tc, 60000, true);

    /* ~180ms gap = char gap (3 dit units) */
    key_event_t e = timing_classifier_classify(&tc, 180000, false);
    TEST_ASSERT_EQUAL(KEY_EVENT_CHAR_GAP, e);
}

void test_timing_classify_word_gap(void) {
    timing_classifier_t tc;
    timing_classifier_init(&tc, 25.0f);

    /* Calibrate */
    timing_classifier_classify(&tc, 60000, true);
    timing_classifier_classify(&tc, 60000, true);
    timing_classifier_classify(&tc, 60000, true);

    /* ~420ms gap = word gap (7 dit units) */
    key_event_t e = timing_classifier_classify(&tc, 420000, false);
    TEST_ASSERT_EQUAL(KEY_EVENT_WORD_GAP, e);
}

void test_timing_get_wpm(void) {
    timing_classifier_t tc;
    timing_classifier_init(&tc, 25.0f);

    /* Feed dits at 20 WPM (60ms) */
    timing_classifier_classify(&tc, 60000, true);
    timing_classifier_classify(&tc, 60000, true);
    timing_classifier_classify(&tc, 60000, true);

    uint32_t wpm = timing_classifier_get_wpm(&tc);
    /* Should be around 20 WPM */
    TEST_ASSERT_GREATER_OR_EQUAL(18, wpm);
    TEST_ASSERT_LESS_OR_EQUAL(22, wpm);
}

void test_timing_reset(void) {
    timing_classifier_t tc;
    timing_classifier_init(&tc, 25.0f);

    timing_classifier_classify(&tc, 60000, true);
    timing_classifier_reset(&tc);

    TEST_ASSERT_EQUAL(0, tc.dit_count);
    TEST_ASSERT_EQUAL(0, tc.dah_count);
}
```

**Step 3: Run test to verify it fails**

Run:
```bash
cd test_host && cmake --build build 2>&1 | head -20
```
Expected: FAIL - timing_classifier.c not found

**Step 4: Create timing_classifier.c implementation**

```c
/* components/keyer_decoder/src/timing_classifier.c */
#include "timing_classifier.h"
#include <stddef.h>

/* Default timing at 20 WPM */
#define DEFAULT_DIT_US   60000   /* 60ms */
#define DEFAULT_DAH_US  180000   /* 180ms (3x dit) */

/* EMA smoothing factor */
#define EMA_ALPHA 0.3f

/* Minimum samples before reliable classification */
#define MIN_SAMPLES 3

void timing_classifier_init(timing_classifier_t *tc, float tolerance_pct) {
    if (tc == NULL) return;

    tc->dit_avg_us = DEFAULT_DIT_US;
    tc->dah_avg_us = DEFAULT_DAH_US;
    tc->dit_count = 0;
    tc->dah_count = 0;
    tc->tolerance_pct = tolerance_pct;
}

static int64_t compute_dit_threshold(const timing_classifier_t *tc) {
    float tolerance_factor = 1.0f + (tc->tolerance_pct / 100.0f);
    return (int64_t)((float)tc->dit_avg_us * 1.5f * tolerance_factor);
}

static void update_dit_avg(timing_classifier_t *tc, int64_t measured_us) {
    if (tc->dit_count == 0) {
        tc->dit_avg_us = measured_us;
    } else {
        tc->dit_avg_us = (int64_t)(EMA_ALPHA * (float)measured_us +
                                    (1.0f - EMA_ALPHA) * (float)tc->dit_avg_us);
    }
    tc->dit_count++;
}

static void update_dah_avg(timing_classifier_t *tc, int64_t measured_us) {
    if (tc->dah_count == 0) {
        tc->dah_avg_us = measured_us;
    } else {
        tc->dah_avg_us = (int64_t)(EMA_ALPHA * (float)measured_us +
                                    (1.0f - EMA_ALPHA) * (float)tc->dah_avg_us);
    }
    tc->dah_count++;
}

key_event_t timing_classifier_classify(timing_classifier_t *tc,
                                        int64_t duration_us,
                                        bool was_key_on) {
    if (tc == NULL || duration_us <= 0) {
        return KEY_EVENT_UNKNOWN;
    }

    if (was_key_on) {
        /* Classify as dit or dah */
        int64_t threshold = compute_dit_threshold(tc);

        if (duration_us < threshold) {
            update_dit_avg(tc, duration_us);
            return KEY_EVENT_DIT;
        } else {
            update_dah_avg(tc, duration_us);
            return KEY_EVENT_DAH;
        }
    } else {
        /* Classify gap type */
        uint32_t total_samples = tc->dit_count + tc->dah_count;
        if (total_samples < MIN_SAMPLES) {
            return KEY_EVENT_UNKNOWN;
        }

        /* Gap thresholds based on dit average */
        int64_t char_gap_threshold = tc->dit_avg_us * 2;
        int64_t word_gap_threshold = tc->dit_avg_us * 5;

        if (duration_us < char_gap_threshold) {
            return KEY_EVENT_INTRA_GAP;
        } else if (duration_us < word_gap_threshold) {
            return KEY_EVENT_CHAR_GAP;
        } else {
            return KEY_EVENT_WORD_GAP;
        }
    }
}

uint32_t timing_classifier_get_wpm(const timing_classifier_t *tc) {
    if (tc == NULL || tc->dit_avg_us <= 0) {
        return 0;
    }

    /* WPM = 1,200,000 / dit_us (PARIS standard) */
    int64_t wpm = 1200000 / tc->dit_avg_us;

    /* Clamp to reasonable range */
    if (wpm < 5) return 5;
    if (wpm > 60) return 60;

    return (uint32_t)wpm;
}

timing_stats_t timing_classifier_get_stats(const timing_classifier_t *tc) {
    timing_stats_t stats = {0};

    if (tc != NULL) {
        stats.dit_avg_us = tc->dit_avg_us;
        stats.dah_avg_us = tc->dah_avg_us;
        stats.dit_count = tc->dit_count;
        stats.dah_count = tc->dah_count;
        stats.wpm = timing_classifier_get_wpm(tc);

        if (tc->dit_avg_us > 0) {
            stats.ratio = (float)tc->dah_avg_us / (float)tc->dit_avg_us;
        }
    }

    return stats;
}

void timing_classifier_reset(timing_classifier_t *tc) {
    if (tc != NULL) {
        float tolerance = tc->tolerance_pct;
        timing_classifier_init(tc, tolerance);
    }
}
```

**Step 5: Update CMakeLists.txt and test_main.c**

Add timing_classifier.c to DECODER_SOURCES.
Add test_timing_classifier.c to TEST_SOURCES.
Add test declarations and RUN_TEST calls.

**Step 6: Run tests**

Run:
```bash
cd test_host && cmake --build build && ./build/test_runner 2>&1 | grep -A20 "Timing"
```
Expected: All timing classifier tests PASS

**Step 7: Commit**

```bash
git add components/keyer_decoder/ test_host/
git commit -m "feat(decoder): add timing classifier with EMA adaptation"
```

---

## Task 3: Implement Decoder Core

**Files:**
- Create: `components/keyer_decoder/include/decoder.h`
- Create: `components/keyer_decoder/src/decoder.c`
- Create: `test_host/test_decoder.c`

**Step 1: Create decoder.h**

```c
/* components/keyer_decoder/include/decoder.h */
#ifndef DECODER_H
#define DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "timing_classifier.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DECODER_BUFFER_SIZE 128
#define DECODER_MAX_PATTERN 6

/** Decoded character with timestamp */
typedef struct {
    char character;
    int64_t timestamp_us;
} decoded_char_t;

/** Decoder state */
typedef enum {
    DECODER_STATE_IDLE = 0,
    DECODER_STATE_RECEIVING = 1
} decoder_state_t;

/**
 * @brief Initialize decoder
 */
void decoder_init(void);

/**
 * @brief Process a classified key event
 * @param event Classified event from timing_classifier
 * @param timestamp_us Timestamp of event
 */
void decoder_handle_event(key_event_t event, int64_t timestamp_us);

/**
 * @brief Enable/disable decoder
 */
void decoder_set_enabled(bool enabled);
bool decoder_is_enabled(void);

/**
 * @brief Get decoded text with timestamps
 * @param buf Output buffer
 * @param max_count Maximum entries
 * @return Number of entries written
 */
size_t decoder_get_text_with_timestamps(decoded_char_t *buf, size_t max_count);

/**
 * @brief Get decoded text (without timestamps)
 * @param buf Output buffer
 * @param max_len Maximum length
 * @return Number of characters written (excluding null terminator)
 */
size_t decoder_get_text(char *buf, size_t max_len);

/**
 * @brief Get last decoded character
 * @return Last character with timestamp
 */
decoded_char_t decoder_get_last_char(void);

/**
 * @brief Get detected WPM
 * @return WPM (0 if not calibrated)
 */
uint32_t decoder_get_wpm(void);

/**
 * @brief Get current pattern being accumulated
 * @param buf Output buffer
 * @param max_len Maximum length
 * @return Pattern length
 */
size_t decoder_get_current_pattern(char *buf, size_t max_len);

/**
 * @brief Get decoder state
 * @return Current state
 */
decoder_state_t decoder_get_state(void);

/**
 * @brief Get number of characters in buffer
 * @return Character count
 */
size_t decoder_get_buffer_count(void);

/**
 * @brief Reset decoder state and buffers
 */
void decoder_reset(void);

/**
 * @brief Get timing classifier statistics
 * @return Timing stats
 */
timing_stats_t decoder_get_timing_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* DECODER_H */
```

**Step 2: Create test file**

```c
/* test_host/test_decoder.c */
#include "unity.h"
#include "decoder.h"

void test_decoder_init(void) {
    decoder_init();
    TEST_ASSERT_TRUE(decoder_is_enabled());
    TEST_ASSERT_EQUAL(DECODER_STATE_IDLE, decoder_get_state());
    TEST_ASSERT_EQUAL(0, decoder_get_buffer_count());
}

void test_decoder_decode_e(void) {
    decoder_init();

    /* E = . */
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 2000);

    TEST_ASSERT_EQUAL('E', decoder_get_last_char().character);
    TEST_ASSERT_EQUAL(1, decoder_get_buffer_count());
}

void test_decoder_decode_t(void) {
    decoder_init();

    /* T = - */
    decoder_handle_event(KEY_EVENT_DAH, 1000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 2000);

    TEST_ASSERT_EQUAL('T', decoder_get_last_char().character);
}

void test_decoder_decode_a(void) {
    decoder_init();

    /* A = .- */
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 2000);
    decoder_handle_event(KEY_EVENT_DAH, 3000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 4000);

    TEST_ASSERT_EQUAL('A', decoder_get_last_char().character);
}

void test_decoder_decode_s(void) {
    decoder_init();

    /* S = ... */
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 2000);
    decoder_handle_event(KEY_EVENT_DIT, 3000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 4000);
    decoder_handle_event(KEY_EVENT_DIT, 5000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 6000);

    TEST_ASSERT_EQUAL('S', decoder_get_last_char().character);
}

void test_decoder_word_gap_adds_space(void) {
    decoder_init();

    /* E = . */
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_WORD_GAP, 2000);

    char buf[16];
    decoder_get_text(buf, sizeof(buf));

    TEST_ASSERT_EQUAL_STRING("E ", buf);
}

void test_decoder_get_text(void) {
    decoder_init();

    /* SOS = ... --- ... */
    /* S */
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 2000);
    decoder_handle_event(KEY_EVENT_DIT, 3000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 4000);
    decoder_handle_event(KEY_EVENT_DIT, 5000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 6000);
    /* O */
    decoder_handle_event(KEY_EVENT_DAH, 7000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 8000);
    decoder_handle_event(KEY_EVENT_DAH, 9000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 10000);
    decoder_handle_event(KEY_EVENT_DAH, 11000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 12000);
    /* S */
    decoder_handle_event(KEY_EVENT_DIT, 13000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 14000);
    decoder_handle_event(KEY_EVENT_DIT, 15000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 16000);
    decoder_handle_event(KEY_EVENT_DIT, 17000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 18000);

    char buf[16];
    decoder_get_text(buf, sizeof(buf));

    TEST_ASSERT_EQUAL_STRING("SOS", buf);
}

void test_decoder_unknown_pattern(void) {
    decoder_init();

    /* Invalid: 7 dits (no such pattern) */
    for (int i = 0; i < 7; i++) {
        decoder_handle_event(KEY_EVENT_DIT, (int64_t)(i * 1000 + 1000));
        decoder_handle_event(KEY_EVENT_INTRA_GAP, (int64_t)(i * 1000 + 1500));
    }
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 10000);

    TEST_ASSERT_EQUAL('?', decoder_get_last_char().character);
}

void test_decoder_disabled(void) {
    decoder_init();
    decoder_set_enabled(false);

    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 2000);

    TEST_ASSERT_EQUAL(0, decoder_get_buffer_count());
}

void test_decoder_reset(void) {
    decoder_init();

    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 2000);

    decoder_reset();

    TEST_ASSERT_EQUAL(0, decoder_get_buffer_count());
    TEST_ASSERT_EQUAL(DECODER_STATE_IDLE, decoder_get_state());
}

void test_decoder_timestamp(void) {
    decoder_init();

    decoder_handle_event(KEY_EVENT_DIT, 12345678);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 12345999);

    decoded_char_t last = decoder_get_last_char();
    TEST_ASSERT_EQUAL('E', last.character);
    TEST_ASSERT_EQUAL(12345999, last.timestamp_us);
}
```

**Step 3: Run test to verify it fails**

Expected: FAIL - decoder.c not found

**Step 4: Create decoder.c implementation**

```c
/* components/keyer_decoder/src/decoder.c */
#include "decoder.h"
#include "morse_table.h"
#include <string.h>

/* State */
static bool s_enabled = true;
static decoder_state_t s_state = DECODER_STATE_IDLE;
static timing_classifier_t s_classifier;

/* Pattern buffer */
static char s_pattern[DECODER_MAX_PATTERN + 1];
static size_t s_pattern_len = 0;

/* Decoded text buffer (circular) */
static decoded_char_t s_buffer[DECODER_BUFFER_SIZE];
static size_t s_buffer_head = 0;
static size_t s_buffer_count = 0;

/* Last decoded character */
static decoded_char_t s_last_char = {0};

static void append_char(char c, int64_t timestamp_us) {
    decoded_char_t entry = { .character = c, .timestamp_us = timestamp_us };

    s_buffer[s_buffer_head] = entry;
    s_buffer_head = (s_buffer_head + 1) % DECODER_BUFFER_SIZE;

    if (s_buffer_count < DECODER_BUFFER_SIZE) {
        s_buffer_count++;
    }

    s_last_char = entry;
}

static void finalize_pattern(int64_t timestamp_us, bool add_space) {
    if (s_pattern_len == 0) {
        s_state = DECODER_STATE_IDLE;
        return;
    }

    s_pattern[s_pattern_len] = '\0';
    char c = morse_table_lookup(s_pattern);
    if (c == '\0') {
        c = '?';
    }

    append_char(c, timestamp_us);

    if (add_space) {
        append_char(' ', timestamp_us);
    }

    s_pattern_len = 0;
    s_state = DECODER_STATE_IDLE;
}

void decoder_init(void) {
    s_enabled = true;
    s_state = DECODER_STATE_IDLE;
    s_pattern_len = 0;
    s_buffer_head = 0;
    s_buffer_count = 0;
    s_last_char.character = '\0';
    s_last_char.timestamp_us = 0;

    timing_classifier_init(&s_classifier, 25.0f);
}

void decoder_handle_event(key_event_t event, int64_t timestamp_us) {
    if (!s_enabled) return;
    if (event == KEY_EVENT_UNKNOWN) return;

    switch (s_state) {
        case DECODER_STATE_IDLE:
            if (event == KEY_EVENT_DIT) {
                s_pattern[0] = '.';
                s_pattern_len = 1;
                s_state = DECODER_STATE_RECEIVING;
            } else if (event == KEY_EVENT_DAH) {
                s_pattern[0] = '-';
                s_pattern_len = 1;
                s_state = DECODER_STATE_RECEIVING;
            }
            break;

        case DECODER_STATE_RECEIVING:
            if (event == KEY_EVENT_DIT) {
                if (s_pattern_len < DECODER_MAX_PATTERN) {
                    s_pattern[s_pattern_len++] = '.';
                }
            } else if (event == KEY_EVENT_DAH) {
                if (s_pattern_len < DECODER_MAX_PATTERN) {
                    s_pattern[s_pattern_len++] = '-';
                }
            } else if (event == KEY_EVENT_INTRA_GAP) {
                /* Ignore - within character */
            } else if (event == KEY_EVENT_CHAR_GAP) {
                finalize_pattern(timestamp_us, false);
            } else if (event == KEY_EVENT_WORD_GAP) {
                finalize_pattern(timestamp_us, true);
            }
            break;
    }
}

void decoder_set_enabled(bool enabled) {
    s_enabled = enabled;
}

bool decoder_is_enabled(void) {
    return s_enabled;
}

size_t decoder_get_text_with_timestamps(decoded_char_t *buf, size_t max_count) {
    if (buf == NULL || max_count == 0) return 0;

    size_t count = (s_buffer_count < max_count) ? s_buffer_count : max_count;
    size_t start = (s_buffer_head + DECODER_BUFFER_SIZE - s_buffer_count) % DECODER_BUFFER_SIZE;

    for (size_t i = 0; i < count; i++) {
        buf[i] = s_buffer[(start + i) % DECODER_BUFFER_SIZE];
    }

    return count;
}

size_t decoder_get_text(char *buf, size_t max_len) {
    if (buf == NULL || max_len == 0) return 0;

    size_t count = (s_buffer_count < max_len - 1) ? s_buffer_count : max_len - 1;
    size_t start = (s_buffer_head + DECODER_BUFFER_SIZE - s_buffer_count) % DECODER_BUFFER_SIZE;

    for (size_t i = 0; i < count; i++) {
        buf[i] = s_buffer[(start + i) % DECODER_BUFFER_SIZE].character;
    }
    buf[count] = '\0';

    return count;
}

decoded_char_t decoder_get_last_char(void) {
    return s_last_char;
}

uint32_t decoder_get_wpm(void) {
    return timing_classifier_get_wpm(&s_classifier);
}

size_t decoder_get_current_pattern(char *buf, size_t max_len) {
    if (buf == NULL || max_len == 0) return 0;

    size_t len = (s_pattern_len < max_len - 1) ? s_pattern_len : max_len - 1;
    memcpy(buf, s_pattern, len);
    buf[len] = '\0';

    return len;
}

decoder_state_t decoder_get_state(void) {
    return s_state;
}

size_t decoder_get_buffer_count(void) {
    return s_buffer_count;
}

void decoder_reset(void) {
    s_state = DECODER_STATE_IDLE;
    s_pattern_len = 0;
    s_buffer_head = 0;
    s_buffer_count = 0;
    s_last_char.character = '\0';
    s_last_char.timestamp_us = 0;

    timing_classifier_reset(&s_classifier);
}

timing_stats_t decoder_get_timing_stats(void) {
    return timing_classifier_get_stats(&s_classifier);
}
```

**Step 5: Update CMakeLists.txt and test_main.c**

Add decoder.c to sources, test_decoder.c to tests.

**Step 6: Run tests**

Run:
```bash
cd test_host && cmake --build build && ./build/test_runner 2>&1 | grep -A30 "Decoder"
```
Expected: All decoder tests PASS

**Step 7: Commit**

```bash
git add components/keyer_decoder/ test_host/
git commit -m "feat(decoder): add pattern decoder with state machine"
```

---

## Task 4: Add Stream Consumer Integration

**Files:**
- Modify: `components/keyer_decoder/src/decoder.c`
- Modify: `components/keyer_decoder/include/decoder.h`

**Step 1: Add decoder_process() to decoder.h**

Add to decoder.h:
```c
/**
 * @brief Process samples from keying stream
 * Call this from bg_task periodically
 */
void decoder_process(void);
```

**Step 2: Implement decoder_process() in decoder.c**

Add includes and variables:
```c
#ifdef ESP_PLATFORM
#include "consumer.h"
#include "stream.h"
#include "esp_timer.h"

extern keying_stream_t g_keying_stream;

static best_effort_consumer_t s_consumer;
static bool s_consumer_initialized = false;
static int64_t s_last_edge_us = 0;
static bool s_last_was_mark = false;
#endif
```

Update decoder_init():
```c
void decoder_init(void) {
    /* ... existing init ... */

#ifdef ESP_PLATFORM
    best_effort_consumer_init(&s_consumer, &g_keying_stream, 100);
    s_consumer_initialized = true;
    s_last_edge_us = 0;
    s_last_was_mark = false;
#endif
}
```

Add decoder_process():
```c
void decoder_process(void) {
#ifdef ESP_PLATFORM
    if (!s_enabled || !s_consumer_initialized) return;

    stream_sample_t sample;
    while (best_effort_consumer_tick(&s_consumer, &sample)) {
        bool is_mark = (sample.local_key != 0);
        int64_t now_us = esp_timer_get_time();

        /* Detect edge */
        if (s_last_edge_us > 0 && is_mark != s_last_was_mark) {
            int64_t duration_us = now_us - s_last_edge_us;
            key_event_t event = timing_classifier_classify(
                &s_classifier, duration_us, s_last_was_mark);
            decoder_handle_event(event, now_us);
        }

        if (is_mark != s_last_was_mark) {
            s_last_edge_us = now_us;
        }
        s_last_was_mark = is_mark;
    }
#endif
}
```

**Step 3: Commit**

```bash
git add components/keyer_decoder/
git commit -m "feat(decoder): add stream consumer integration"
```

---

## Task 5: Add Console Commands

**Files:**
- Modify: `components/keyer_console/src/commands.c`
- Modify: `components/keyer_console/src/completion.c`

**Step 1: Add decoder include**

In commands.c, add:
```c
#include "decoder.h"
```

**Step 2: Implement cmd_decoder()**

```c
static console_error_t cmd_decoder(const console_parsed_cmd_t *cmd) {
    if (cmd->argc == 0) {
        /* Show status */
        bool enabled = decoder_is_enabled();
        uint32_t wpm = decoder_get_wpm();
        size_t count = decoder_get_buffer_count();

        printf("Decoder: %s, WPM: %lu, buffer: %zu/%d chars\r\n",
               enabled ? "ON" : "OFF",
               (unsigned long)wpm,
               count, DECODER_BUFFER_SIZE);

        if (count > 0) {
            char buf[64];
            size_t len = decoder_get_text(buf, sizeof(buf));
            if (len > 32) {
                printf("Last: \"...%s\"\r\n", buf + len - 32);
            } else {
                printf("Last: \"%s\"\r\n", buf);
            }
        }
        return CONSOLE_OK;
    }

    const char *arg = cmd->args[0];

    if (strcmp(arg, "on") == 0) {
        decoder_set_enabled(true);
        printf("Decoder: ON\r\n");
    } else if (strcmp(arg, "off") == 0) {
        decoder_set_enabled(false);
        printf("Decoder: OFF\r\n");
    } else if (strcmp(arg, "clear") == 0) {
        decoder_reset();
        printf("Decoder reset\r\n");
    } else if (strcmp(arg, "text") == 0) {
        decoded_char_t buf[64];
        size_t count = decoder_get_text_with_timestamps(buf, 64);
        for (size_t i = 0; i < count; i++) {
            int64_t ts = buf[i].timestamp_us;
            int secs = (int)(ts / 1000000);
            int ms = (int)((ts / 1000) % 1000);
            char c = buf[i].character;
            if (c == ' ') {
                printf("[%d.%03d] (space)\r\n", secs, ms);
            } else {
                printf("[%d.%03d] %c\r\n", secs, ms, c);
            }
        }
    } else if (strcmp(arg, "stats") == 0) {
        timing_stats_t stats = decoder_get_timing_stats();
        printf("WPM: %lu (dit: %lldms, dah: %lldms, ratio: %.2f)\r\n",
               (unsigned long)stats.wpm,
               stats.dit_avg_us / 1000,
               stats.dah_avg_us / 1000,
               (double)stats.ratio);
        printf("Samples: dit=%lu, dah=%lu\r\n",
               (unsigned long)stats.dit_count,
               (unsigned long)stats.dah_count);
        printf("Buffer: %zu/%d chars\r\n",
               decoder_get_buffer_count(), DECODER_BUFFER_SIZE);
        printf("State: %s\r\n",
               decoder_get_state() == DECODER_STATE_IDLE ? "IDLE" : "RECEIVING");
    } else {
        return CONSOLE_ERR_INVALID_VALUE;
    }

    return CONSOLE_OK;
}
```

**Step 3: Add to command registry**

```c
static const char USAGE_DECODER[] =
    "  decoder           Show status and last text\r\n"
    "  decoder on|off    Enable/disable decoder\r\n"
    "  decoder text      Show buffer with timestamps\r\n"
    "  decoder clear     Clear buffer and reset timing\r\n"
    "  decoder stats     Show timing statistics";

/* In s_commands array: */
{ "decoder", "CW decoder control", USAGE_DECODER, cmd_decoder },
```

**Step 4: Add tab completion**

In completion.c, add decoder completions.

**Step 5: Build and test**

```bash
idf.py build
```

**Step 6: Commit**

```bash
git add components/keyer_console/
git commit -m "feat(console): add decoder command"
```

---

## Task 6: Integrate with bg_task

**Files:**
- Modify: `main/bg_task.c`

**Step 1: Add decoder include and call**

```c
#include "decoder.h"

void bg_task(void *arg) {
    (void)arg;

    /* Initialize decoder */
    decoder_init();

    /* ... existing consumer init ... */

    for (;;) {
        /* ... existing code ... */

        /* Process decoder */
        decoder_process();

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
```

**Step 2: Remove duplicate consumer**

The decoder now has its own consumer, so remove the existing consumer code that was just for placeholder.

**Step 3: Build and flash**

```bash
idf.py build flash monitor
```

**Step 4: Test on device**

```
> decoder
Decoder: ON, WPM: 0, buffer: 0/128 chars

# Key some morse code with paddle

> decoder
Decoder: ON, WPM: 25, buffer: 5/128 chars
Last: "HELLO"

> decoder stats
WPM: 25 (dit: 48ms, dah: 144ms, ratio: 3.00)
```

**Step 5: Commit**

```bash
git add main/bg_task.c
git commit -m "feat(decoder): integrate with bg_task"
```

---

## Task 7: Final Integration Test

**Step 1: Full build**

```bash
idf.py fullclean && idf.py build
```

**Step 2: Run host tests**

```bash
cd test_host && cmake --build build && ./build/test_runner
```
Expected: All tests PASS

**Step 3: Flash and test on device**

```bash
idf.py flash monitor
```

Test commands:
```
decoder
decoder on
decoder off
decoder stats
decoder text
decoder clear
```

**Step 4: Final commit**

```bash
git add -A
git commit -m "feat(decoder): complete CW decoder implementation"
```

---

## Summary

| Task | Description |
|------|-------------|
| 1 | Morse table with ITU patterns |
| 2 | Timing classifier with EMA |
| 3 | Pattern decoder state machine |
| 4 | Stream consumer integration |
| 5 | Console commands |
| 6 | bg_task integration |
| 7 | Final integration test |
