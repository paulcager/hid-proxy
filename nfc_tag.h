//
// Created by paul on 05/12/23.
//

#ifndef HID_PROXY_NFC_TAG_H
#define HID_PROXY_NFC_TAG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

void nfc_setup();
void nfc_task(bool key_required);

void nfc_write_key(uint8_t *key, size_t key_length, unsigned long timeout_millis);
bool nfc_key_available();
bool nfc_get_key(uint8_t key[16]);

#endif //HID_PROXY_NFC_TAG_H
