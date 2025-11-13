#ifndef MACROS_TEST_API_H
#define MACROS_TEST_API_H

#include "hid_proxy.h"
#include <stdbool.h>

// Test API for old store_t-based functions (for unit testing)
// These functions are kept in macros.c for testing but not in public API

// Helper to navigate variable-length keydef array
static inline keydef_t *next_keydef(const keydef_t *this) {
    return (keydef_t*)((uint8_t*)this + sizeof(keydef_t) + this->count * sizeof(hid_keyboard_report_t));
}

// Old API functions for testing
bool parse_macros(const char* input_buffer, store_t* temp_store);
bool serialize_macros(const store_t* store, char* output_buffer, size_t buffer_size);

#endif // MACROS_TEST_API_H
