#ifndef HID_PROXY_MACROS_H
#define HID_PROXY_MACROS_H

#include <stdbool.h>
#include "hid_proxy.h"

// Parses a string buffer containing the content of macros.txt into a temporary store buffer.
bool parse_macros(const char* input_buffer, store_t* temp_store);

// Serializes the key definitions from a store into a string buffer.
bool serialize_macros(const store_t* store, char* output_buffer, size_t buffer_size);

#endif //HID_PROXY_MACROS_H
