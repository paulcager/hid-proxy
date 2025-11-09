/*
 * kvstore_init.c
 *
 * Initialization and management of the pico-kvstore system
 * With AES-128-GCM encryption support
 */

#include "kvstore_init.h"
#include "kvstore.h"
#include "kvstore_logkvs.h"
#include "blockdevice/flash.h"
#include "mbedtls/gcm.h"
#include "pico/rand.h"
#include "tinycrypt/sha256.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Encryption constants
#define IV_SIZE 12    // GCM recommended IV size
#define TAG_SIZE 16   // GCM authentication tag size

// Encryption key and state
static bool device_unlocked = false;
static uint8_t encryption_key[16];  // AES-128 key from PBKDF2
static bool encryption_key_available = false;

bool kvstore_init(void) {
    printf("kvstore_init: Initializing pico-kvstore system with AES-128-GCM...\n");
    printf("kvstore_init: Flash offset=0x%08X, size=%u bytes\n",
           KVSTORE_OFFSET, KVSTORE_SIZE);

    // Create block device using onboard flash (last 128KB)
    blockdevice_t *blockdev = blockdevice_flash_create(
        KVSTORE_OFFSET,
        KVSTORE_SIZE
    );
    if (blockdev == NULL) {
        printf("kvstore_init: Failed to create flash block device\n");
        return false;
    }
    printf("kvstore_init: Flash block device created\n");

    // Create log-structured KVS for wear leveling (NO ENCRYPTION LAYER)
    kvs_t *logkvs = kvs_logkvs_create(blockdev);
    if (logkvs == NULL) {
        printf("kvstore_init: Failed to create log-structured KVS\n");
        blockdevice_flash_free(blockdev);
        return false;
    }
    printf("kvstore_init: Log-structured KVS created\n");

    // Assign as global instance (no securekvs wrapper)
    printf("kvstore_init: Assigning global KVS instance\n");
    kvs_assign(logkvs);
    printf("kvstore_init: Global KVS assigned\n");

    printf("kvstore_init: Initialization complete\n");
    return true;
}

bool kvstore_set_encryption_key(const uint8_t key[16]) {
    // Hash the derived key with SHA256
    struct tc_sha256_state_struct sha_ctx;
    tc_sha256_init(&sha_ctx);
    tc_sha256_update(&sha_ctx, key, 16);

    uint8_t computed_hash[TC_SHA256_DIGEST_SIZE];  // 32 bytes
    tc_sha256_final(computed_hash, &sha_ctx);

    printf("kvstore_init: Computed hash (first 8 bytes): %02X %02X %02X %02X %02X %02X %02X %02X\n",
           computed_hash[0], computed_hash[1], computed_hash[2], computed_hash[3],
           computed_hash[4], computed_hash[5], computed_hash[6], computed_hash[7]);

    // Check if a password hash already exists (device has been initialized)
    uint8_t stored_hash[TC_SHA256_DIGEST_SIZE];
    size_t hash_size;
    int ret = kvstore_get_value(PASSWORD_HASH_KEY, stored_hash, sizeof(stored_hash), &hash_size, NULL);

    if (ret == KVSTORE_ERROR_ITEM_NOT_FOUND) {
        // First-time setup: no password hash exists yet
        // Accept this password and store its hash
        printf("kvstore_init: First-time setup - storing password hash\n");

        ret = kvstore_set_value(PASSWORD_HASH_KEY, computed_hash, sizeof(computed_hash), false);
        if (ret != 0) {
            printf("kvstore_init: ERROR - Failed to store password hash: %s\n", kvs_strerror(ret));
            return false;
        }

        // Store the encryption key and unlock
        memcpy(encryption_key, key, 16);
        encryption_key_available = true;
        device_unlocked = true;

        printf("kvstore_init: Password set successfully (device unlocked)\n");
        return true;

    } else if (ret != 0) {
        // Error reading hash
        printf("kvstore_init: ERROR - Failed to read password hash: %s\n", kvs_strerror(ret));
        return false;

    } else {
        // Password hash exists - validate the provided key
        printf("kvstore_init: Stored hash (first 8 bytes): %02X %02X %02X %02X %02X %02X %02X %02X\n",
               stored_hash[0], stored_hash[1], stored_hash[2], stored_hash[3],
               stored_hash[4], stored_hash[5], stored_hash[6], stored_hash[7]);

        if (hash_size != TC_SHA256_DIGEST_SIZE) {
            printf("kvstore_init: ERROR - Invalid hash size %zu (expected %d)\n",
                   hash_size, TC_SHA256_DIGEST_SIZE);
            return false;
        }

        // Compare hashes using constant-time comparison
        int match = 1;
        for (size_t i = 0; i < TC_SHA256_DIGEST_SIZE; i++) {
            match &= (computed_hash[i] == stored_hash[i]);
        }

        if (match) {
            // Password correct - unlock device
            memcpy(encryption_key, key, 16);
            encryption_key_available = true;
            device_unlocked = true;

            printf("kvstore_init: Password correct (device unlocked)\n");
            return true;
        } else {
            // Password incorrect - reject
            printf("kvstore_init: Password incorrect (device remains locked)\n");
            return false;
        }
    }
}

void kvstore_clear_encryption_key(void) {
    // Clear the encryption key from memory
    memset(encryption_key, 0, sizeof(encryption_key));
    encryption_key_available = false;
    device_unlocked = false;
    printf("kvstore_init: Encryption key cleared (device locked)\n");
}

bool kvstore_is_unlocked(void) {
    return device_unlocked;
}

/*! \brief Encrypt data using AES-128-GCM
 *
 * \param plaintext Input data to encrypt
 * \param plaintext_len Length of plaintext
 * \param iv Output buffer for IV (12 bytes)
 * \param ciphertext Output buffer for ciphertext (same size as plaintext)
 * \param tag Output buffer for authentication tag (16 bytes)
 * \return 0 on success, negative on error
 */
static int encrypt_gcm(const uint8_t *plaintext, size_t plaintext_len,
                       uint8_t *iv, uint8_t *ciphertext, uint8_t *tag) {
    if (!encryption_key_available) {
        printf("encrypt_gcm: No encryption key available\n");
        return -1;
    }

    // Generate random IV
    rng_128_t rand_data;
    get_rand_128(&rand_data);
    // Copy first 12 bytes to IV (we only need 12 for GCM)
    memcpy(iv, &rand_data, IV_SIZE);

    // Initialize GCM context
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, encryption_key, 128);
    if (ret != 0) {
        printf("encrypt_gcm: Failed to set key: %d\n", ret);
        mbedtls_gcm_free(&gcm);
        return ret;
    }

    // Encrypt and generate tag
    ret = mbedtls_gcm_crypt_and_tag(
        &gcm,
        MBEDTLS_GCM_ENCRYPT,
        plaintext_len,
        iv, IV_SIZE,
        NULL, 0,          // No additional authenticated data
        plaintext,
        ciphertext,
        TAG_SIZE,
        tag
    );

    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        printf("encrypt_gcm: Encryption failed: %d\n", ret);
        return ret;
    }

    return 0;
}

/*! \brief Decrypt data using AES-128-GCM
 *
 * \param ciphertext Input ciphertext
 * \param ciphertext_len Length of ciphertext
 * \param iv IV used for encryption (12 bytes)
 * \param tag Authentication tag (16 bytes)
 * \param plaintext Output buffer for decrypted data
 * \return 0 on success, negative on error (including authentication failure)
 */
static int decrypt_gcm(const uint8_t *ciphertext, size_t ciphertext_len,
                       const uint8_t *iv, const uint8_t *tag, uint8_t *plaintext) {
    if (!encryption_key_available) {
        printf("decrypt_gcm: No encryption key available\n");
        return KVSTORE_ERROR_AUTHENTICATION_FAILED;
    }

    // Initialize GCM context
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, encryption_key, 128);
    if (ret != 0) {
        printf("decrypt_gcm: Failed to set key: %d\n", ret);
        mbedtls_gcm_free(&gcm);
        return ret;
    }

    // Decrypt and verify tag
    ret = mbedtls_gcm_auth_decrypt(
        &gcm,
        ciphertext_len,
        iv, IV_SIZE,
        NULL, 0,          // No additional authenticated data
        tag, TAG_SIZE,
        ciphertext,
        plaintext
    );

    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        if (ret == MBEDTLS_ERR_GCM_AUTH_FAILED) {
            printf("decrypt_gcm: Authentication failed (wrong key or tampered data)\n");
            return KVSTORE_ERROR_AUTHENTICATION_FAILED;
        }
        printf("decrypt_gcm: Decryption failed: %d\n", ret);
        return ret;
    }

    return 0;
}

int kvstore_set_value(const char *key, const void *data, size_t size, bool encrypted) {
    if (!encrypted) {
        // Unencrypted path - just add header
        size_t total_size = 1 + size;
        uint8_t *buffer = (uint8_t *)malloc(total_size);
        if (buffer == NULL) {
            printf("kvstore_set_value: malloc failed for %zu bytes\n", total_size);
            return KVSTORE_ERROR_WRITE_FAILED;
        }

        buffer[0] = KVSTORE_HEADER_UNENCRYPTED;
        memcpy(buffer + 1, data, size);

        int ret = kvs_set(key, buffer, total_size);
        free(buffer);
        return ret;
    }

    // Encrypted path
    // Format: [header(1)][IV(12)][ciphertext(size)][tag(16)]
    size_t total_size = 1 + IV_SIZE + size + TAG_SIZE;
    uint8_t *buffer = (uint8_t *)malloc(total_size);
    if (buffer == NULL) {
        printf("kvstore_set_value: malloc failed for %zu bytes\n", total_size);
        return KVSTORE_ERROR_WRITE_FAILED;
    }

    // Header byte
    buffer[0] = KVSTORE_HEADER_ENCRYPTED;

    // Pointers to components
    uint8_t *iv = buffer + 1;
    uint8_t *ciphertext = buffer + 1 + IV_SIZE;
    uint8_t *tag = buffer + 1 + IV_SIZE + size;

    // Encrypt the data
    int ret = encrypt_gcm((const uint8_t *)data, size, iv, ciphertext, tag);
    if (ret != 0) {
        printf("kvstore_set_value: Encryption failed\n");
        free(buffer);
        return KVSTORE_ERROR_WRITE_FAILED;
    }

    printf("kvstore_set_value: Encrypted %zu bytes (total stored: %zu bytes)\n", size, total_size);
    printf("kvstore_set_value: IV: %02X %02X %02X %02X...\n", iv[0], iv[1], iv[2], iv[3]);

    // Store to kvstore
    ret = kvs_set(key, buffer, total_size);

    free(buffer);
    return ret;
}

int kvstore_get_value(const char *key, void *buffer, size_t bufsize, size_t *actual_size, bool *is_encrypted) {
    // Allocate temp buffer for header + encrypted data
    // Max encrypted size: 1 (header) + 12 (IV) + bufsize (ciphertext) + 16 (tag)
    size_t temp_size = 1 + IV_SIZE + bufsize + TAG_SIZE;
    uint8_t *temp_buffer = (uint8_t *)malloc(temp_size);
    if (temp_buffer == NULL) {
        printf("kvstore_get_value: malloc failed for %zu bytes\n", temp_size);
        return KVSTORE_ERROR_READ_FAILED;
    }

    // Read from kvstore
    size_t read_size;
    int ret = kvs_get(key, temp_buffer, temp_size, &read_size);

    if (ret != 0) {
        free(temp_buffer);
        return ret;
    }

    // Check minimum size (must have at least header byte)
    if (read_size < 1) {
        printf("kvstore_get_value: Invalid size %zu (no header)\n", read_size);
        free(temp_buffer);
        return KVSTORE_ERROR_READ_FAILED;
    }

    // Parse header
    uint8_t header = temp_buffer[0];
    bool encrypted_flag = (header == KVSTORE_HEADER_ENCRYPTED);

    // Return encryption status if requested
    if (is_encrypted != NULL) {
        *is_encrypted = encrypted_flag;
    }

    if (!encrypted_flag) {
        // Unencrypted data - just strip header
        size_t data_size = read_size - 1;

        if (data_size > bufsize) {
            printf("kvstore_get_value: Buffer too small (need %zu, have %zu)\n", data_size, bufsize);
            free(temp_buffer);
            return KVSTORE_ERROR_READ_FAILED;
        }

        if (buffer != NULL && data_size > 0) {
            memcpy(buffer, temp_buffer + 1, data_size);
        }

        if (actual_size != NULL) {
            *actual_size = data_size;
        }

        free(temp_buffer);
        return 0;
    }

    // Encrypted data - need to decrypt
    // Format: [header(1)][IV(12)][ciphertext(N)][tag(16)]
    size_t min_encrypted_size = 1 + IV_SIZE + TAG_SIZE;
    if (read_size < min_encrypted_size) {
        printf("kvstore_get_value: Encrypted data too small (%zu bytes)\n", read_size);
        free(temp_buffer);
        return KVSTORE_ERROR_READ_FAILED;
    }

    // Extract components
    uint8_t *iv = temp_buffer + 1;
    size_t ciphertext_len = read_size - 1 - IV_SIZE - TAG_SIZE;
    uint8_t *ciphertext = temp_buffer + 1 + IV_SIZE;
    uint8_t *tag = temp_buffer + read_size - TAG_SIZE;

    printf("kvstore_get_value: Decrypting %zu bytes (ciphertext=%zu)\n", read_size, ciphertext_len);
    printf("kvstore_get_value: IV: %02X %02X %02X %02X...\n", iv[0], iv[1], iv[2], iv[3]);

    // Check buffer size
    if (ciphertext_len > bufsize) {
        printf("kvstore_get_value: Buffer too small for decrypted data (need %zu, have %zu)\n",
               ciphertext_len, bufsize);
        free(temp_buffer);
        return KVSTORE_ERROR_READ_FAILED;
    }

    // Decrypt
    ret = decrypt_gcm(ciphertext, ciphertext_len, iv, tag, (uint8_t *)buffer);
    if (ret != 0) {
        printf("kvstore_get_value: Decryption failed\n");
        free(temp_buffer);
        return ret;  // Will be KVSTORE_ERROR_AUTHENTICATION_FAILED if auth failed
    }

    if (actual_size != NULL) {
        *actual_size = ciphertext_len;
    }

    free(temp_buffer);
    return 0;
}
