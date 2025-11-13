#ifndef HID_PROXY_MACROS_H
#define HID_PROXY_MACROS_H

#include <stdbool.h>
#include "hid_proxy.h"

// Parses macros.txt format and saves directly to kvstore (replaces all existing keydefs).
bool parse_macros_to_kvstore(const char* input_buffer);

// Serializes the key definitions from kvstore into a string buffer.
bool serialize_macros_from_kvstore(char* output_buffer, size_t buffer_size);

#endif //HID_PROXY_MACROS_H
