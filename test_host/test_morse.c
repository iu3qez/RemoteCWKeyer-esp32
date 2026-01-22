/**
 * @file test_morse.c
 * @brief Unit tests for Morse lookup table and lock-free SPSC queue
 *
 * TDD Phase: RED - These tests define expected behavior for:
 * - ASCII-to-Morse lookup table
 * - Lock-free SPSC morse queue (single producer, single consumer)
 */

#include "unity.h"
#include "winkeyer_morse.h"
#include "morse_queue.h"
#include <string.h>

/* ============================================================================
 * Morse Lookup Table Tests
 * ============================================================================ */

/**
 * Test lookup for letter 'A' (dit-dah)
 * ITU: .-
 * Bit pattern: 0b10 (LSB first: dit=0, dah=1), length=2
 */
void test_morse_lookup_A(void) {
    const morse_char_t *mc = morse_lookup('A');
    TEST_ASSERT_NOT_NULL(mc);
    TEST_ASSERT_EQUAL(2, mc->length);
    /* LSB first: bit 0 = dit (0), bit 1 = dah (1) -> 0b10 = 2 */
    TEST_ASSERT_EQUAL(0x02, mc->pattern);
}

/**
 * Test lookup for letter 'B' (dah-dit-dit-dit)
 * ITU: -...
 * Bit pattern: 0b0001 (LSB first), length=4
 */
void test_morse_lookup_B(void) {
    const morse_char_t *mc = morse_lookup('B');
    TEST_ASSERT_NOT_NULL(mc);
    TEST_ASSERT_EQUAL(4, mc->length);
    /* LSB first: dah(1), dit(0), dit(0), dit(0) -> 0b0001 = 1 */
    TEST_ASSERT_EQUAL(0x01, mc->pattern);
}

/**
 * Test lookup for letter 'E' (single dit)
 * ITU: .
 * Bit pattern: 0b0, length=1
 */
void test_morse_lookup_E(void) {
    const morse_char_t *mc = morse_lookup('E');
    TEST_ASSERT_NOT_NULL(mc);
    TEST_ASSERT_EQUAL(1, mc->length);
    TEST_ASSERT_EQUAL(0x00, mc->pattern);
}

/**
 * Test lookup for letter 'T' (single dah)
 * ITU: -
 * Bit pattern: 0b1, length=1
 */
void test_morse_lookup_T(void) {
    const morse_char_t *mc = morse_lookup('T');
    TEST_ASSERT_NOT_NULL(mc);
    TEST_ASSERT_EQUAL(1, mc->length);
    TEST_ASSERT_EQUAL(0x01, mc->pattern);
}

/**
 * Test lookup for digit '0' (five dahs)
 * ITU: -----
 * Bit pattern: 0b11111 = 31, length=5
 */
void test_morse_lookup_0(void) {
    const morse_char_t *mc = morse_lookup('0');
    TEST_ASSERT_NOT_NULL(mc);
    TEST_ASSERT_EQUAL(5, mc->length);
    TEST_ASSERT_EQUAL(0x1F, mc->pattern);  /* 0b11111 */
}

/**
 * Test lookup for digit '5' (five dits)
 * ITU: .....
 * Bit pattern: 0b00000 = 0, length=5
 */
void test_morse_lookup_5(void) {
    const morse_char_t *mc = morse_lookup('5');
    TEST_ASSERT_NOT_NULL(mc);
    TEST_ASSERT_EQUAL(5, mc->length);
    TEST_ASSERT_EQUAL(0x00, mc->pattern);
}

/**
 * Test lookup for digit '1' (dit followed by four dahs)
 * ITU: .----
 * Bit pattern: 0b11110 (LSB first), length=5
 */
void test_morse_lookup_1(void) {
    const morse_char_t *mc = morse_lookup('1');
    TEST_ASSERT_NOT_NULL(mc);
    TEST_ASSERT_EQUAL(5, mc->length);
    /* LSB first: dit(0), dah(1), dah(1), dah(1), dah(1) -> 0b11110 = 30 */
    TEST_ASSERT_EQUAL(0x1E, mc->pattern);
}

/**
 * Test lookup for period '.' (dit-dah-dit-dah-dit-dah)
 * ITU: .-.-.-
 * Bit pattern: 0b101010 (LSB first), length=6
 */
void test_morse_lookup_period(void) {
    const morse_char_t *mc = morse_lookup('.');
    TEST_ASSERT_NOT_NULL(mc);
    TEST_ASSERT_EQUAL(6, mc->length);
    /* LSB first: . - . - . - -> 0, 1, 0, 1, 0, 1 -> 0b101010 = 42 */
    TEST_ASSERT_EQUAL(0x2A, mc->pattern);
}

/**
 * Test lookup for question mark '?' (dit-dit-dah-dah-dit-dit)
 * ITU: ..--..
 * Bit pattern: 0b001100 (LSB first), length=6
 */
void test_morse_lookup_question(void) {
    const morse_char_t *mc = morse_lookup('?');
    TEST_ASSERT_NOT_NULL(mc);
    TEST_ASSERT_EQUAL(6, mc->length);
    /* LSB first: . . - - . . -> 0, 0, 1, 1, 0, 0 -> 0b001100 = 12 */
    TEST_ASSERT_EQUAL(0x0C, mc->pattern);
}

/**
 * Test lookup for slash '/' (dah-dit-dit-dah-dit)
 * ITU: -..-.
 * Bit pattern: 0b01001 (LSB first), length=5
 */
void test_morse_lookup_slash(void) {
    const morse_char_t *mc = morse_lookup('/');
    TEST_ASSERT_NOT_NULL(mc);
    TEST_ASSERT_EQUAL(5, mc->length);
    /* LSB first: - . . - . -> 1, 0, 0, 1, 0 -> 0b01001 = 9 */
    TEST_ASSERT_EQUAL(0x09, mc->pattern);
}

/**
 * Test lookup for equals '=' (dah-dit-dit-dit-dah)
 * ITU: -...-
 * Bit pattern: 0b10001 (LSB first), length=5
 */
void test_morse_lookup_equals(void) {
    const morse_char_t *mc = morse_lookup('=');
    TEST_ASSERT_NOT_NULL(mc);
    TEST_ASSERT_EQUAL(5, mc->length);
    /* LSB first: - . . . - -> 1, 0, 0, 0, 1 -> 0b10001 = 17 */
    TEST_ASSERT_EQUAL(0x11, mc->pattern);
}

/**
 * Test lookup for comma ',' (dah-dah-dit-dit-dah-dah)
 * ITU: --..--
 * Bit pattern: 0b110011 (LSB first), length=6
 */
void test_morse_lookup_comma(void) {
    const morse_char_t *mc = morse_lookup(',');
    TEST_ASSERT_NOT_NULL(mc);
    TEST_ASSERT_EQUAL(6, mc->length);
    /* LSB first: - - . . - - -> 1, 1, 0, 0, 1, 1 -> 0b110011 = 51 */
    TEST_ASSERT_EQUAL(0x33, mc->pattern);
}

/**
 * Test lowercase conversion - 'a' should work same as 'A'
 */
void test_morse_lookup_lowercase(void) {
    const morse_char_t *mc_lower = morse_lookup('a');
    const morse_char_t *mc_upper = morse_lookup('A');
    TEST_ASSERT_NOT_NULL(mc_lower);
    TEST_ASSERT_NOT_NULL(mc_upper);
    TEST_ASSERT_EQUAL(mc_upper->length, mc_lower->length);
    TEST_ASSERT_EQUAL(mc_upper->pattern, mc_lower->pattern);
}

/**
 * Test lookup for space returns NULL (word space handled differently)
 */
void test_morse_lookup_space_returns_null(void) {
    const morse_char_t *mc = morse_lookup(' ');
    TEST_ASSERT_NULL(mc);
}

/**
 * Test lookup for unsupported character returns NULL
 */
void test_morse_lookup_unsupported_returns_null(void) {
    const morse_char_t *mc = morse_lookup('@');
    TEST_ASSERT_NULL(mc);

    mc = morse_lookup('#');
    TEST_ASSERT_NULL(mc);

    mc = morse_lookup(0x00);
    TEST_ASSERT_NULL(mc);

    mc = morse_lookup(0x7F);
    TEST_ASSERT_NULL(mc);
}

/**
 * Test all letters A-Z have valid entries
 */
void test_morse_lookup_all_letters(void) {
    for (char c = 'A'; c <= 'Z'; c++) {
        const morse_char_t *mc = morse_lookup(c);
        TEST_ASSERT_NOT_NULL_MESSAGE(mc, "Letter should have morse entry");
        TEST_ASSERT_TRUE_MESSAGE(mc->length >= 1 && mc->length <= 6,
                                  "Morse length must be 1-6");
    }
}

/**
 * Test all digits 0-9 have valid entries
 */
void test_morse_lookup_all_digits(void) {
    for (char c = '0'; c <= '9'; c++) {
        const morse_char_t *mc = morse_lookup(c);
        TEST_ASSERT_NOT_NULL_MESSAGE(mc, "Digit should have morse entry");
        TEST_ASSERT_EQUAL_MESSAGE(5, mc->length, "All digits have 5 elements");
    }
}

/* ============================================================================
 * Morse Queue Tests
 * ============================================================================ */

static morse_queue_t s_test_queue;

/**
 * Test queue initialization
 */
void test_morse_queue_init(void) {
    morse_queue_init(&s_test_queue);
    TEST_ASSERT_TRUE(morse_queue_is_empty(&s_test_queue));
    TEST_ASSERT_EQUAL(0, morse_queue_count(&s_test_queue));
}

/**
 * Test basic push and pop
 */
void test_morse_queue_push_pop(void) {
    morse_queue_init(&s_test_queue);

    morse_element_t elem = { .type = MORSE_ELEMENT_DIT };
    bool pushed = morse_queue_push(&s_test_queue, elem);
    TEST_ASSERT_TRUE(pushed);
    TEST_ASSERT_FALSE(morse_queue_is_empty(&s_test_queue));
    TEST_ASSERT_EQUAL(1, morse_queue_count(&s_test_queue));

    morse_element_t out;
    bool popped = morse_queue_pop(&s_test_queue, &out);
    TEST_ASSERT_TRUE(popped);
    TEST_ASSERT_EQUAL(MORSE_ELEMENT_DIT, out.type);
    TEST_ASSERT_TRUE(morse_queue_is_empty(&s_test_queue));
    TEST_ASSERT_EQUAL(0, morse_queue_count(&s_test_queue));
}

/**
 * Test all element types
 */
void test_morse_queue_all_element_types(void) {
    morse_queue_init(&s_test_queue);

    morse_element_type_t types[] = {
        MORSE_ELEMENT_DIT,
        MORSE_ELEMENT_DAH,
        MORSE_ELEMENT_CHAR_SPACE,
        MORSE_ELEMENT_WORD_SPACE,
        MORSE_ELEMENT_KEY_DOWN,
        MORSE_ELEMENT_KEY_UP,
    };
    size_t num_types = sizeof(types) / sizeof(types[0]);

    /* Push all types */
    for (size_t i = 0; i < num_types; i++) {
        morse_element_t elem = { .type = types[i] };
        TEST_ASSERT_TRUE(morse_queue_push(&s_test_queue, elem));
    }

    TEST_ASSERT_EQUAL(num_types, morse_queue_count(&s_test_queue));

    /* Pop and verify */
    for (size_t i = 0; i < num_types; i++) {
        morse_element_t out;
        TEST_ASSERT_TRUE(morse_queue_pop(&s_test_queue, &out));
        TEST_ASSERT_EQUAL(types[i], out.type);
    }

    TEST_ASSERT_TRUE(morse_queue_is_empty(&s_test_queue));
}

/**
 * Test pop from empty queue returns false
 */
void test_morse_queue_pop_empty_returns_false(void) {
    morse_queue_init(&s_test_queue);

    morse_element_t out;
    bool popped = morse_queue_pop(&s_test_queue, &out);
    TEST_ASSERT_FALSE(popped);
}

/**
 * Test queue fills to capacity
 */
void test_morse_queue_fill_to_capacity(void) {
    morse_queue_init(&s_test_queue);

    /* Push until full (MORSE_QUEUE_SIZE - 1 elements to distinguish full from empty) */
    for (size_t i = 0; i < MORSE_QUEUE_SIZE - 1; i++) {
        morse_element_t elem = { .type = MORSE_ELEMENT_DIT };
        bool pushed = morse_queue_push(&s_test_queue, elem);
        TEST_ASSERT_TRUE_MESSAGE(pushed, "Should be able to push until full");
    }

    TEST_ASSERT_EQUAL(MORSE_QUEUE_SIZE - 1, morse_queue_count(&s_test_queue));

    /* Next push should fail (queue full) */
    morse_element_t elem = { .type = MORSE_ELEMENT_DAH };
    bool pushed = morse_queue_push(&s_test_queue, elem);
    TEST_ASSERT_FALSE_MESSAGE(pushed, "Push to full queue should fail");
}

/**
 * Test queue wraparound behavior
 */
void test_morse_queue_wraparound(void) {
    morse_queue_init(&s_test_queue);

    /* Fill queue halfway */
    size_t half = MORSE_QUEUE_SIZE / 2;
    for (size_t i = 0; i < half; i++) {
        morse_element_t elem = { .type = MORSE_ELEMENT_DIT };
        TEST_ASSERT_TRUE(morse_queue_push(&s_test_queue, elem));
    }

    /* Pop all */
    for (size_t i = 0; i < half; i++) {
        morse_element_t out;
        TEST_ASSERT_TRUE(morse_queue_pop(&s_test_queue, &out));
    }

    TEST_ASSERT_TRUE(morse_queue_is_empty(&s_test_queue));

    /* Fill again - this should wrap around */
    for (size_t i = 0; i < MORSE_QUEUE_SIZE - 1; i++) {
        morse_element_t elem = { .type = (morse_element_type_t)(i % 4) };
        bool pushed = morse_queue_push(&s_test_queue, elem);
        TEST_ASSERT_TRUE_MESSAGE(pushed, "Wraparound push should succeed");
    }

    TEST_ASSERT_EQUAL(MORSE_QUEUE_SIZE - 1, morse_queue_count(&s_test_queue));

    /* Verify FIFO order preserved through wraparound */
    for (size_t i = 0; i < MORSE_QUEUE_SIZE - 1; i++) {
        morse_element_t out;
        TEST_ASSERT_TRUE(morse_queue_pop(&s_test_queue, &out));
        TEST_ASSERT_EQUAL((morse_element_type_t)(i % 4), out.type);
    }
}

/**
 * Test queue clear
 */
void test_morse_queue_clear(void) {
    morse_queue_init(&s_test_queue);

    /* Add some elements */
    for (int i = 0; i < 10; i++) {
        morse_element_t elem = { .type = MORSE_ELEMENT_DIT };
        morse_queue_push(&s_test_queue, elem);
    }

    TEST_ASSERT_EQUAL(10, morse_queue_count(&s_test_queue));
    TEST_ASSERT_FALSE(morse_queue_is_empty(&s_test_queue));

    /* Clear */
    morse_queue_clear(&s_test_queue);

    TEST_ASSERT_TRUE(morse_queue_is_empty(&s_test_queue));
    TEST_ASSERT_EQUAL(0, morse_queue_count(&s_test_queue));

    /* Should be able to push after clear */
    morse_element_t elem = { .type = MORSE_ELEMENT_DAH };
    TEST_ASSERT_TRUE(morse_queue_push(&s_test_queue, elem));
}

/**
 * Test interleaved push/pop operations
 */
void test_morse_queue_interleaved_ops(void) {
    morse_queue_init(&s_test_queue);

    /* Push 3 */
    for (int i = 0; i < 3; i++) {
        morse_element_t elem = { .type = MORSE_ELEMENT_DIT };
        TEST_ASSERT_TRUE(morse_queue_push(&s_test_queue, elem));
    }
    TEST_ASSERT_EQUAL(3, morse_queue_count(&s_test_queue));

    /* Pop 1 */
    morse_element_t out;
    TEST_ASSERT_TRUE(morse_queue_pop(&s_test_queue, &out));
    TEST_ASSERT_EQUAL(2, morse_queue_count(&s_test_queue));

    /* Push 2 */
    for (int i = 0; i < 2; i++) {
        morse_element_t elem = { .type = MORSE_ELEMENT_DAH };
        TEST_ASSERT_TRUE(morse_queue_push(&s_test_queue, elem));
    }
    TEST_ASSERT_EQUAL(4, morse_queue_count(&s_test_queue));

    /* Pop 2, should be DITs */
    TEST_ASSERT_TRUE(morse_queue_pop(&s_test_queue, &out));
    TEST_ASSERT_EQUAL(MORSE_ELEMENT_DIT, out.type);
    TEST_ASSERT_TRUE(morse_queue_pop(&s_test_queue, &out));
    TEST_ASSERT_EQUAL(MORSE_ELEMENT_DIT, out.type);

    /* Pop 2, should be DAHs */
    TEST_ASSERT_TRUE(morse_queue_pop(&s_test_queue, &out));
    TEST_ASSERT_EQUAL(MORSE_ELEMENT_DAH, out.type);
    TEST_ASSERT_TRUE(morse_queue_pop(&s_test_queue, &out));
    TEST_ASSERT_EQUAL(MORSE_ELEMENT_DAH, out.type);

    TEST_ASSERT_TRUE(morse_queue_is_empty(&s_test_queue));
}

/**
 * Test MORSE_QUEUE_SIZE is power of 2
 */
void test_morse_queue_size_is_power_of_2(void) {
    /* Power of 2 check: n & (n-1) == 0 for powers of 2 */
    TEST_ASSERT_EQUAL_MESSAGE(0, MORSE_QUEUE_SIZE & (MORSE_QUEUE_SIZE - 1),
                               "MORSE_QUEUE_SIZE must be power of 2");
    TEST_ASSERT_TRUE_MESSAGE(MORSE_QUEUE_SIZE >= 16,
                              "MORSE_QUEUE_SIZE should be at least 16");
}
