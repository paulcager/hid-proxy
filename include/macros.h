#ifndef HID_PROXY_MACROS_H
#define HID_PROXY_MACROS_H

#include <stdbool.h>
#include "hid_proxy.h"

// Parses a string buffer containing the content of macros.txt into a temporary store buffer.
bool parse_macros(const char* input_buffer, store_t* temp_store);

// Parses macros.txt format and saves directly to kvstore (replaces all existing keydefs).
bool parse_macros_to_kvstore(const char* input_buffer);

// Serializes the key definitions from a store into a string buffer.
bool serialize_macros(const store_t* store, char* output_buffer, size_t buffer_size);

// Serializes the key definitions from kvstore into a string buffer.
bool serialize_macros_from_kvstore(char* output_buffer, size_t buffer_size);

// Helper function to iterate through keydefs
static inline keydef_t *next_keydef(const keydef_t *this) {
    const void *t = this;
    t += sizeof(keydef_t) + (this->count * sizeof(hid_keyboard_report_t));
    return (keydef_t*)t;
}

#endif //HID_PROXY_MACROS_H
