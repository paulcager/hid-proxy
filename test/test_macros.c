// Test suite for macro parser and serializer
#include "unity.h"
#include "pico_mocks.h"
#include "hid_proxy.h"
#include "macros.h"
#include <string.h>

// Test store buffer
static uint8_t test_store_buffer[FLASH_STORE_SIZE];
static store_t *test_store;

void setUp(void) {
    // Clear test buffer before each test
    memset(test_store_buffer, 0, sizeof(test_store_buffer));
    test_store = (store_t*)test_store_buffer;
}

void tearDown(void) {
    // Cleanup after each test
}

// Test 1: Simple text macro
void test_parse_simple_text(void) {
    const char *input = "a { \"Hello\" }";

    bool result = parse_macros(input, test_store);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0x04, test_store->keydefs[0].keycode); // 'a' keycode
    TEST_ASSERT_EQUAL(10, test_store->keydefs[0].used);      // 5 chars * 2 (press+release)

    // Verify each character's press and release
    // H (shift + h)
    TEST_ASSERT_EQUAL(0x02, test_store->keydefs[0].reports[0].modifier);  // Shift
    TEST_ASSERT_EQUAL(0x0b, test_store->keydefs[0].reports[0].keycode[0]); // H key
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[1].modifier);  // Release
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[1].keycode[0]);

    // e
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[2].modifier);
    TEST_ASSERT_EQUAL(0x08, test_store->keydefs[0].reports[2].keycode[0]); // e key
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[3].modifier);  // Release
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[3].keycode[0]);

    // l
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[4].modifier);
    TEST_ASSERT_EQUAL(0x0f, test_store->keydefs[0].reports[4].keycode[0]); // l key
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[5].modifier);  // Release
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[5].keycode[0]);

    // l (second)
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[6].modifier);
    TEST_ASSERT_EQUAL(0x0f, test_store->keydefs[0].reports[6].keycode[0]); // l key
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[7].modifier);  // Release
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[7].keycode[0]);

    // o
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[8].modifier);
    TEST_ASSERT_EQUAL(0x12, test_store->keydefs[0].reports[8].keycode[0]); // o key
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[9].modifier);  // Release
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[9].keycode[0]);
}

// Test 2: Mnemonic trigger
void test_parse_mnemonic_trigger(void) {
    const char *input = "F1 { \"Test\" }";

    bool result = parse_macros(input, test_store);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0x3a, test_store->keydefs[0].keycode); // F1 keycode
}

// Test 3: Hex trigger
void test_parse_hex_trigger(void) {
    const char *input = "0x04 { \"Test\" }";

    bool result = parse_macros(input, test_store);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0x04, test_store->keydefs[0].keycode);
}

// Test 4: Ctrl shorthand
void test_parse_ctrl_shorthand(void) {
    const char *input = "a { ^C ^V }";

    bool result = parse_macros(input, test_store);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(2, test_store->keydefs[0].used);
    // First report: Ctrl+C
    TEST_ASSERT_EQUAL(0x01, test_store->keydefs[0].reports[0].modifier); // Ctrl
    TEST_ASSERT_EQUAL(0x06, test_store->keydefs[0].reports[0].keycode[0]); // C
    // Second report: Ctrl+V
    TEST_ASSERT_EQUAL(0x01, test_store->keydefs[0].reports[1].modifier); // Ctrl
    TEST_ASSERT_EQUAL(0x19, test_store->keydefs[0].reports[1].keycode[0]); // V
}

// Test 5: Explicit HID reports
void test_parse_explicit_reports(void) {
    const char *input = "a { [01:06] [00:00] }";

    bool result = parse_macros(input, test_store);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(2, test_store->keydefs[0].used);
    TEST_ASSERT_EQUAL(0x01, test_store->keydefs[0].reports[0].modifier);
    TEST_ASSERT_EQUAL(0x06, test_store->keydefs[0].reports[0].keycode[0]);
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[1].modifier);
    TEST_ASSERT_EQUAL(0x00, test_store->keydefs[0].reports[1].keycode[0]);
}

// Test 6: Mnemonic commands
void test_parse_mnemonic_commands(void) {
    const char *input = "a { ENTER TAB ESC }";

    bool result = parse_macros(input, test_store);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(3, test_store->keydefs[0].used);
    TEST_ASSERT_EQUAL(0x28, test_store->keydefs[0].reports[0].keycode[0]); // ENTER
    TEST_ASSERT_EQUAL(0x2b, test_store->keydefs[0].reports[1].keycode[0]); // TAB
    TEST_ASSERT_EQUAL(0x29, test_store->keydefs[0].reports[2].keycode[0]); // ESC
}

// Test 7: Mixed commands
void test_parse_mixed_commands(void) {
    const char *input = "F1 { \"Hi\" ENTER ^C }";

    bool result = parse_macros(input, test_store);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0x3a, test_store->keydefs[0].keycode); // F1
    // Should have: H press, H release, i press, i release, ENTER, Ctrl+C
    TEST_ASSERT_EQUAL(6, test_store->keydefs[0].used);
}

// Test 8: Comments and whitespace
void test_parse_with_comments(void) {
    const char *input =
        "# This is a comment\n"
        "a { \"Test\" }\n"
        "# Another comment\n"
        "b { \"Data\" }";

    bool result = parse_macros(input, test_store);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(0x04, test_store->keydefs[0].keycode); // 'a'
    TEST_ASSERT_EQUAL(0x05, next_keydef(&test_store->keydefs[0])->keycode); // 'b'
}

// Test 9: Escaped quotes in strings
void test_parse_escaped_quotes(void) {
    const char *input = "a { \"He said \\\"Hi\\\"\" }";

    bool result = parse_macros(input, test_store);

    TEST_ASSERT_TRUE(result);
    // Should have: H e space s a i d space " H i "
    // Each char = 2 reports (press + release) = 12 * 2 = 24 reports
    TEST_ASSERT_EQUAL(24, test_store->keydefs[0].used);
}

// Test 10: Multiple keydefs
void test_parse_multiple_keydefs(void) {
    const char *input =
        "a { \"First\" }\n"
        "b { \"Second\" }\n"
        "c { \"Third\" }";

    bool result = parse_macros(input, test_store);

    TEST_ASSERT_TRUE(result);

    keydef_t *def1 = &test_store->keydefs[0];
    TEST_ASSERT_EQUAL(0x04, def1->keycode); // 'a'

    keydef_t *def2 = next_keydef(def1);
    TEST_ASSERT_EQUAL(0x05, def2->keycode); // 'b'

    keydef_t *def3 = next_keydef(def2);
    TEST_ASSERT_EQUAL(0x06, def3->keycode); // 'c'

    // Terminator
    keydef_t *terminator = next_keydef(def3);
    TEST_ASSERT_EQUAL(0x00, terminator->keycode);
}

// Test 11: Serializer - simple text
void test_serialize_simple_text(void) {
    char output[1024];

    // Manually create a keydef
    test_store->keydefs[0].keycode = 0x04; // 'a'
    test_store->keydefs[0].used = 2;
    // 'H' press
    test_store->keydefs[0].reports[0].modifier = 0x02; // Shift
    test_store->keydefs[0].reports[0].keycode[0] = 0x0b; // H
    // Release
    test_store->keydefs[0].reports[1].modifier = 0x00;
    test_store->keydefs[0].reports[1].keycode[0] = 0x00;
    // Terminator
    next_keydef(&test_store->keydefs[0])->keycode = 0;

    bool result = serialize_macros(test_store, output, sizeof(output));

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(strstr(output, "a {") != NULL);
    TEST_ASSERT_TRUE(strstr(output, "\"H\"") != NULL);
}

// Test 12: Serializer - ctrl shorthand
void test_serialize_ctrl_shorthand(void) {
    char output[1024];

    test_store->keydefs[0].keycode = 0x04; // 'a'
    test_store->keydefs[0].used = 1;
    // Ctrl+C
    test_store->keydefs[0].reports[0].modifier = 0x01;
    test_store->keydefs[0].reports[0].keycode[0] = 0x06; // C
    // Terminator
    next_keydef(&test_store->keydefs[0])->keycode = 0;

    bool result = serialize_macros(test_store, output, sizeof(output));

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(strstr(output, "^c") != NULL);
}

// Test 13: Round-trip test (parse then serialize)
void test_roundtrip(void) {
    const char *input = "a { \"Test\" ENTER }";
    char output[1024];

    // Parse
    bool parse_result = parse_macros(input, test_store);
    TEST_ASSERT_TRUE(parse_result);

    // Serialize
    bool serialize_result = serialize_macros(test_store, output, sizeof(output));
    TEST_ASSERT_TRUE(serialize_result);

    // Parse again
    uint8_t test_store_buffer2[FLASH_STORE_SIZE];
    store_t *test_store2 = (store_t*)test_store_buffer2;
    memset(test_store_buffer2, 0, sizeof(test_store_buffer2));

    bool parse_result2 = parse_macros(output, test_store2);
    TEST_ASSERT_TRUE(parse_result2);

    // Compare
    TEST_ASSERT_EQUAL(test_store->keydefs[0].keycode, test_store2->keydefs[0].keycode);
    TEST_ASSERT_EQUAL(test_store->keydefs[0].used, test_store2->keydefs[0].used);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_parse_simple_text);
    RUN_TEST(test_parse_mnemonic_trigger);
    RUN_TEST(test_parse_hex_trigger);
    RUN_TEST(test_parse_ctrl_shorthand);
    RUN_TEST(test_parse_explicit_reports);
    RUN_TEST(test_parse_mnemonic_commands);
    RUN_TEST(test_parse_mixed_commands);
    RUN_TEST(test_parse_with_comments);
    RUN_TEST(test_parse_escaped_quotes);
    RUN_TEST(test_parse_multiple_keydefs);
    RUN_TEST(test_serialize_simple_text);
    RUN_TEST(test_serialize_ctrl_shorthand);
    RUN_TEST(test_roundtrip);

    return UNITY_END();
}
