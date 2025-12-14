//
// Created by paul on 18/11/23.
//

#ifndef HID_PROXY_ENCRYPTION_H
#define HID_PROXY_ENCRYPTION_H

void enc_set_key(uint8_t *data, size_t length);

void enc_get_key(uint8_t *data, size_t length);

void enc_add_password_byte(uint8_t b);

void enc_clear_password();

void enc_derive_key_from_password();

void enc_clear_key();

// Helper function to derive key from password string and attempt unlock
bool enc_unseal_with_password(const char *password);

#endif //HID_PROXY_ENCRYPTION_H
