/**
 * @file test_fault.c
 * @brief Unit tests for fault state management
 */

#include "unity.h"
#include "fault.h"
#include "stubs/esp_stubs.h"

static fault_state_t s_fault;

void test_fault_init(void) {
    fault_init(&s_fault);

    TEST_ASSERT_FALSE(fault_is_active(&s_fault));
    TEST_ASSERT_EQUAL(FAULT_NONE, fault_get_code(&s_fault));
    TEST_ASSERT_EQUAL(0, fault_get_count(&s_fault));
}

void test_fault_set_clear(void) {
    fault_init(&s_fault);

    /* Set fault */
    fault_set(&s_fault, FAULT_OVERRUN, 42);

    TEST_ASSERT_TRUE(fault_is_active(&s_fault));
    TEST_ASSERT_EQUAL(FAULT_OVERRUN, fault_get_code(&s_fault));
    TEST_ASSERT_EQUAL(42, fault_get_data(&s_fault));

    /* Clear fault */
    fault_clear(&s_fault);

    TEST_ASSERT_FALSE(fault_is_active(&s_fault));
    TEST_ASSERT_EQUAL(FAULT_NONE, fault_get_code(&s_fault));
}

void test_fault_count(void) {
    fault_init(&s_fault);

    /* Set multiple faults of same type */
    fault_set(&s_fault, FAULT_PRODUCER_OVERRUN, 1);
    TEST_ASSERT_EQUAL(1, fault_get_count(&s_fault));

    fault_set(&s_fault, FAULT_PRODUCER_OVERRUN, 2);
    TEST_ASSERT_EQUAL(2, fault_get_count(&s_fault));

    fault_set(&s_fault, FAULT_PRODUCER_OVERRUN, 3);
    TEST_ASSERT_EQUAL(3, fault_get_count(&s_fault));

    /* Clear and verify count resets */
    fault_clear(&s_fault);
    TEST_ASSERT_EQUAL(0, fault_get_count(&s_fault));

    /* New fault starts count at 1 */
    fault_set(&s_fault, FAULT_OVERRUN, 100);
    TEST_ASSERT_EQUAL(1, fault_get_count(&s_fault));
}
