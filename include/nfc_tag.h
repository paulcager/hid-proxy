//
// Created by paul on 05/12/23.
//

#ifndef HID_PROXY_NFC_TAG_H
#define HID_PROXY_NFC_TAG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PN532_HOSTTOPN532                   (0xD4)
#define PN532_PN532TOHOST                   (0xD5)

#define PN532_COMMAND_GETFIRMWAREVERSION    (0x02)
#define PN532_COMMAND_SAMCONFIGURATION      (0x14)
#define PN532_COMMAND_INDATAEXCHANGE        (0x40)
#define PN532_COMMAND_INLISTPASSIVETARGET   (0x4A)
#define PN532_COMMAND_INLISTPASSIVETARGET   (0x4A)

void nfc_setup();
void nfc_task(bool key_required);

// NFC key storage uses 2 consecutive 16-byte blocks to store 32-byte AES-256 keys
void nfc_write_key(uint8_t *key, size_t key_length, unsigned long timeout_millis);
void nfc_bad_key();
bool nfc_key_available();
bool nfc_get_key(uint8_t key[32]);  // Changed from key[16] to key[32] for AES-256

#endif //HID_PROXY_NFC_TAG_H
