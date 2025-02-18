//
// Created by paul on 18/11/23.
//

#ifndef HID_PROXY_ENCRYPTION_H
#define HID_PROXY_ENCRYPTION_H

void enc_set_key(uint8_t *data, size_t length);

void enc_get_key(uint8_t *data, size_t length);

void enc_start_key_derivation();

void enc_add_key_derivation_byte(uint8_t b);

void enc_end_key_derivation();

void enc_clear_key();

bool store_encrypt(kb_t *kb);

bool store_decrypt(kb_t *kb);

#endif //HID_PROXY_ENCRYPTION_H
