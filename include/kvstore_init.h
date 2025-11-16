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

// Value format: [header_byte][data...]
// Header byte indicates encryption status
#define KVSTORE_HEADER_UNENCRYPTED 0x00
#define KVSTORE_HEADER_ENCRYPTED   0x01

// Kvstore key for password hash
#define PASSWORD_HASH_KEY "auth.password_hash"

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
 * Validates the provided key against stored password hash.
 * - First-time setup: If no hash exists, accepts any password and stores its hash
 * - Subsequent unlocks: Validates key by comparing SHA256(key) with stored hash
 *
 * \param key 16-byte encryption key derived from password via PBKDF2
 * \return true if password correct (or first-time setup), false if incorrect
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

/*! \brief Change password and re-encrypt all encrypted data
 *
 * This function:
 * 1. Loads all encrypted keydefs using the OLD key
 * 2. Updates the password hash with the NEW key
 * 3. Re-saves all keydefs using the NEW key
 *
 * \param new_key 16-byte encryption key derived from new password via PBKDF2
 * \return true on success, false on failure
 */
bool kvstore_change_password(const uint8_t new_key[16]);

/*! \brief Store a value with encryption header
 *
 * Prepends a header byte indicating encryption status, then stores to kvstore.
 * For now, all values are stored unencrypted (header = KVSTORE_HEADER_UNENCRYPTED).
 *
 * \param key KVStore key name
 * \param data Pointer to data to store
 * \param size Size of data in bytes
 * \param encrypted true to mark as encrypted (not yet implemented), false for unencrypted
 * \return 0 on success, error code on failure
 */
int kvstore_set_value(const char *key, const void *data, size_t size, bool encrypted);

/*! \brief Read a value and check encryption header
 *
 * Reads value from kvstore, checks header byte, and returns data without header.
 * If is_encrypted is non-NULL, sets it to true if header indicates encrypted data.
 *
 * \param key KVStore key name
 * \param buffer Buffer to receive data (without header)
 * \param bufsize Size of buffer
 * \param actual_size Receives actual size of data (without header)
 * \param is_encrypted Optional: receives encryption status (can be NULL)
 * \return 0 on success, error code on failure
 */
int kvstore_get_value(const char *key, void *buffer, size_t bufsize, size_t *actual_size, bool *is_encrypted);
