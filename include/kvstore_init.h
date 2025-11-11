/*
 * kvstore_init.h
 *
 * Initialization and management of the pico-kvstore three-layer stack:
 * - Flash block device (last 128KB of onboard flash)
 * - Log-structured KVS (wear leveling, transaction safety)
 * - Secure KVS (AES-128-GCM encryption for sensitive data)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Use 128KB for kvstore at end of 2MB flash (substantial increase from current 8KB total)
#define KVSTORE_SIZE (128 * 1024)
#define KVSTORE_OFFSET (0x1E0000)  // 2MB - 128KB = 0x200000 - 0x20000

/*! \brief Initialize the kvstore system
 *
 * Creates the three-layer kvstore stack:
 * 1. Flash block device using last 128KB of onboard flash
 * 2. Log-structured KVS for wear leveling
 * 3. Secure KVS with AES-128-GCM encryption
 *
 * This should be called early in main() before any kvstore operations.
 *
 * \return true on success, false on failure
 */
bool kvstore_init(void);

/*! \brief Provide encryption key after passphrase unlock
 *
 * After the user unlocks the device (via passphrase or NFC),
 * call this to make encrypted keydefs accessible.
 *
 * Validates the key against stored password hash. If no hash exists,
 * stores a new hash (first password set).
 *
 * \param key 16-byte AES-128 key (derived from passphrase via PBKDF2)
 * \return true if password validated/set successfully, false if incorrect password
 */
bool kvstore_set_encryption_key(const uint8_t key[16]);

/*! \brief Clear encryption key (on lock)
 *
 * When the device is locked, this clears the encryption key from memory
 * and makes encrypted keydefs inaccessible.
 */
void kvstore_clear_encryption_key(void);

/*! \brief Check if encryption key is available
 *
 * \return true if encryption key is loaded (device unlocked), false otherwise
 */
bool kvstore_is_unlocked(void);

/*! \brief Switch to default (public) encryption key
 *
 * Use this before writing/reading public data (WiFi config, public keydefs).
 * The default key is always available.
 */
void kvstore_use_default_key(void);

/*! \brief Switch to secure (private) encryption key
 *
 * Use this before writing/reading private data (private keydefs).
 * Only works if device is unlocked; otherwise stays on default key.
 */
void kvstore_use_secure_key(void);

/*! \brief Read a value, trying both public and private keys
 *
 * Attempts to read with current key context. If authentication fails,
 * retries with the opposite key. This allows transparent access to both
 * public and private data when unlocked.
 *
 * \param key KVStore key name
 * \param buffer Buffer to receive value
 * \param bufsize Size of buffer
 * \param actual_size Receives actual size of value
 * \return 0 on success, error code on failure
 */
int kvs_get_any(const char *key, void *buffer, size_t bufsize, size_t *actual_size);
