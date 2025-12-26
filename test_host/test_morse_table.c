/**
 * @file test_morse_table.c
 * @brief Unit tests for morse code lookup table
 */

#include "unity.h"
#include "morse_table.h"

void test_morse_lookup_letters(void) {
    /* Test common letters */
    TEST_ASSERT_EQUAL_CHAR('A', morse_table_lookup(".-"));
    TEST_ASSERT_EQUAL_CHAR('B', morse_table_lookup("-..."));
    TEST_ASSERT_EQUAL_CHAR('C', morse_table_lookup("-.-."));
    TEST_ASSERT_EQUAL_CHAR('E', morse_table_lookup("."));
    TEST_ASSERT_EQUAL_CHAR('T', morse_table_lookup("-"));
    TEST_ASSERT_EQUAL_CHAR('S', morse_table_lookup("..."));
    TEST_ASSERT_EQUAL_CHAR('O', morse_table_lookup("---"));
    TEST_ASSERT_EQUAL_CHAR('H', morse_table_lookup("...."));
    TEST_ASSERT_EQUAL_CHAR('Z', morse_table_lookup("--.."));
}

void test_morse_lookup_numbers(void) {
    TEST_ASSERT_EQUAL_CHAR('0', morse_table_lookup("-----"));
    TEST_ASSERT_EQUAL_CHAR('1', morse_table_lookup(".----"));
    TEST_ASSERT_EQUAL_CHAR('2', morse_table_lookup("..---"));
    TEST_ASSERT_EQUAL_CHAR('5', morse_table_lookup("....."));
    TEST_ASSERT_EQUAL_CHAR('9', morse_table_lookup("----."));
}

void test_morse_lookup_punctuation(void) {
    TEST_ASSERT_EQUAL_CHAR('.', morse_table_lookup(".-.-.-"));
    TEST_ASSERT_EQUAL_CHAR(',', morse_table_lookup("--..--"));
    TEST_ASSERT_EQUAL_CHAR('?', morse_table_lookup("..--.."));
    TEST_ASSERT_EQUAL_CHAR('/', morse_table_lookup("-..-."));
}

void test_morse_lookup_prosigns(void) {
    TEST_ASSERT_EQUAL_CHAR('=', morse_table_lookup("-...-"));  /* BT */
    TEST_ASSERT_EQUAL_CHAR('+', morse_table_lookup(".-.-."));  /* AR */
    TEST_ASSERT_EQUAL_CHAR('*', morse_table_lookup("...-.-")); /* SK */
}

void test_morse_lookup_invalid(void) {
    TEST_ASSERT_EQUAL_CHAR('\0', morse_table_lookup(""));
    TEST_ASSERT_EQUAL_CHAR('\0', morse_table_lookup("xyz"));
    TEST_ASSERT_EQUAL_CHAR('\0', morse_table_lookup(".........."));
    TEST_ASSERT_EQUAL_CHAR('\0', morse_table_lookup(NULL));
}

void test_morse_reverse_lookup(void) {
    TEST_ASSERT_EQUAL_STRING(".-", morse_table_reverse('A'));
    TEST_ASSERT_EQUAL_STRING(".-", morse_table_reverse('a'));  /* Lowercase */
    TEST_ASSERT_EQUAL_STRING("...", morse_table_reverse('S'));
    TEST_ASSERT_EQUAL_STRING("-----", morse_table_reverse('0'));
    TEST_ASSERT_NULL(morse_table_reverse('@' + 100));  /* Invalid char */
}

void test_morse_table_count(void) {
    unsigned count = morse_table_count();
    /* Should have at least 26 letters + 10 numbers */
    TEST_ASSERT_GREATER_OR_EQUAL(36, count);
    /* Verify it's reasonable (not absurdly large) */
    TEST_ASSERT_LESS_THAN(100, count);
}
