/*
 * kvstore_init.h
 *
 * Initialization and management of the pico-kvstore system
 * Simplified version: NO ENCRYPTION (plain logkvs only)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Use 128KB for kvstore at end of 2MB flash
#define KVSTORE_SIZE (128 * 1024)
#define KVSTORE_OFFSET (0x1E0000)  // 2MB - 128KB = 0x200000 - 0x20000

/*! \brief Initialize the kvstore system
 *
 * Creates unencrypted log-structured KVS on flash.
 *
 * This should be called early in main() before any kvstore operations.
 *
 * \return true on success, false on failure
 */
bool kvstore_init(void);

/*! \brief Provide encryption key after passphrase unlock
 *
 * Simplified version: Always accepts password, just sets unlocked flag.
 * We keep the PBKDF2 key derivation but don't actually use the key yet.
 *
 * \param key 16-byte key (ignored for now)
 * \return true (always succeeds)
 */
bool kvstore_set_encryption_key(const uint8_t key[16]);

/*! \brief Clear encryption key (on lock)
 *
 * Clears the unlocked flag.
 */
void kvstore_clear_encryption_key(void);

/*! \brief Check if device is unlocked
 *
 * \return true if device unlocked, false otherwise
 */
bool kvstore_is_unlocked(void);
