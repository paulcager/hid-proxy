#ifndef HID_PROXY_MACROS_H
#define HID_PROXY_MACROS_H

#include <stdbool.h>
#include <stdint.h>
#include "hid_proxy.h"

// Parses macros.txt format and saves directly to kvstore (replaces all existing keydefs).
bool parse_macros_to_kvstore(const char* input_buffer);

// Serializes the key definitions from kvstore into a string buffer.
bool serialize_macros_from_kvstore(char* output_buffer, size_t buffer_size);

// Convert HID keycode to mnemonic name (e.g., 0x28 -> "ENTER")
// Returns NULL if no mnemonic exists for this keycode
const char* keycode_to_mnemonic(uint8_t keycode);

// Convert HID keycode + modifier to ASCII character
// Returns 0 if no ASCII mapping exists
char keycode_to_ascii(uint8_t keycode, uint8_t modifier);

#endif //HID_PROXY_MACROS_H
