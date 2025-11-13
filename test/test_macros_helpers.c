// Test suite for macro parser helper functions
#include "unity.h"
#include "pico_mocks.h"
#include "macros.h"
#include "macros_test_api.h"
#include <string.h>

// These functions are static in macros.c, so we need to include the C file directly
// to access them for testing. This is a common technique for testing static functions.
#include "../src/macros.c"

void setUp(void) {
    // Set up anything needed for the tests
}

void tearDown(void) {
    // Clean up after tests
}

void test_lookup_mnemonic_keycode(void) {
    TEST_ASSERT_EQUAL_UINT8(0x28, lookup_mnemonic_keycode("ENTER"));
    TEST_ASSERT_EQUAL_UINT8(0x3a, lookup_mnemonic_keycode("F1"));
    TEST_ASSERT_EQUAL_UINT8(0x00, lookup_mnemonic_keycode("INVALID"));
}

void test_keycode_to_mnemonic(void) {
    TEST_ASSERT_EQUAL_STRING("ENTER", keycode_to_mnemonic(0x28));
    TEST_ASSERT_EQUAL_STRING("F1", keycode_to_mnemonic(0x3a));
    TEST_ASSERT_NULL(keycode_to_mnemonic(0x00));
}

void test_keycode_to_ascii(void) {
    TEST_ASSERT_EQUAL_CHAR('a', keycode_to_ascii(0x04, 0x00));
    TEST_ASSERT_EQUAL_CHAR('A', keycode_to_ascii(0x04, 0x02));
    TEST_ASSERT_EQUAL_CHAR('1', keycode_to_ascii(0x1e, 0x00));
    TEST_ASSERT_EQUAL_CHAR('!', keycode_to_ascii(0x1e, 0x02));
    TEST_ASSERT_EQUAL_CHAR(0, keycode_to_ascii(0x00, 0x00));
}

void test_parse_trigger(void) {
    const char* input1 = "a {";
    const char* p1 = input1;
    TEST_ASSERT_EQUAL_UINT8(0x04, parse_trigger(&p1));

    const char* input2 = "F1 {";
    const char* p2 = input2;
    TEST_ASSERT_EQUAL_UINT8(0x3a, parse_trigger(&p2));

    const char* input3 = "0x04 {";
    const char* p3 = input3;
    TEST_ASSERT_EQUAL_UINT8(0x04, parse_trigger(&p3));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_lookup_mnemonic_keycode);
    RUN_TEST(test_keycode_to_mnemonic);
    RUN_TEST(test_keycode_to_ascii);
    RUN_TEST(test_parse_trigger);
    return UNITY_END();
}
