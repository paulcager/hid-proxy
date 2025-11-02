#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "macros.h"
#include "hid_proxy.h"

// A temporary buffer to act as our flash storage for testing
static uint8_t test_flash_buffer[FLASH_STORE_SIZE];
static store_t* test_store = (store_t*)test_flash_buffer;

// Forward declaration for a helper from key_defs.c
static inline keydef_t *next_keydef(keydef_t *this) {
    void *t = this;
    t += sizeof(keydef_t) + (this->used * sizeof(hid_keyboard_report_t));
    return t;
}

void setup_test() {
    // Clear the test buffer before each test
    memset(test_flash_buffer, 0, FLASH_STORE_SIZE);
    strcpy(test_store->magic, FLASH_STORE_MAGIC);
}

void test_simple_type() {
    printf("Running test: test_simple_type... ");
    setup_test();

    const char* macro_file =
        "# Define 'a' to type Hello\n"
        "define a\n"
        "  type \"Hello\"\n"
        "end\n";

    bool success = parse_macros(macro_file, test_store);
    assert(success);

    keydef_t* def = test_store->keydefs;

    // Check first definition
    assert(def->keycode == 0x04); // 'a'
    // We will need a proper lookup for this, for now, placeholder
    // assert(def->used == 10); // 5 chars * 2 reports (press/release)

    // Check for end of definitions
    keydef_t* next = next_keydef(def);
    assert(next->keycode == 0);

    printf("OK (structure only)\n");
}

void test_simple_report() {
    printf("Running test: test_simple_report... ");
    setup_test();

    const char* macro_file =
        "# Define Ctrl+C for key 'c'\n"
        "define 0x06\n"
        "  report mod=0x01 keys=0x06\n"
        "  report mod=0x00 keys=0x00\n"
        "end\n";

    bool success = parse_macros(macro_file, test_store);
    assert(success);

    keydef_t* def = test_store->keydefs;
    assert(def->keycode == 0x06);
    assert(def->used == 2);

    assert(def->reports[0].modifier == 0x01);
    assert(def->reports[0].keycode[0] == 0x06);
    assert(def->reports[1].modifier == 0x00);
    assert(def->reports[1].keycode[0] == 0x00);
    
    keydef_t* next = next_keydef(def);
    assert(next->keycode == 0);

    printf("OK\n");
}

int main() {
    printf("Starting parser tests...\n");

    // These tests are placeholders. They will not compile and run until
    // the parse_macros function and its dependencies (like the HID lookup tables)
    // are implemented in a new 'macros.c' file.

    test_simple_type();
    test_simple_report();

    printf("Test file created. Implementation of parser needed for tests to run.\n");

    return 0;
}
