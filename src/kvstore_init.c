/*
 * kvstore_init.c
 *
 * Initialization and management of the pico-kvstore system
 */

#include "kvstore_init.h"
#include "kvstore.h"
#include "kvstore_logkvs.h"
#include "kvstore_securekvs.h"
#include "blockdevice/flash.h"
#include "tinycrypt/sha256.h"
#include <string.h>
#include <stdio.h>

#define PASSWORD_HASH_KEY "password.hash"

// Dual-key encryption system:
// - default_key: hardcoded, always available, for public data (WiFi config, public keydefs)
// - secure_key: PBKDF2-derived from user password, for private keydefs
static const uint8_t default_key[16] = {
    0xDE, 0xFA, 0x17, 0x00,  0x00, 0x11, 0x22, 0x33,
    0x44, 0x55, 0x66, 0x77,  0x88, 0x99, 0xAA, 0xBB
};
static uint8_t secure_key[16];
static bool secure_key_available = false;
static bool use_secure_key = false;  // Controls which key secretkey_loader returns

/*! \brief Secret key loader callback for secure KVS
 *
 * This function is called by the secure KVS layer when it needs
 * the encryption key to encrypt/decrypt data.
 *
 * Returns secure_key if available and use_secure_key is true,
 * otherwise returns default_key.
 *
 * \param key Buffer to receive the 16-byte AES key
 * \return 0 on success (always succeeds - always have at least default key)
 */
static int secretkey_loader(uint8_t *key) {
    if (use_secure_key && secure_key_available) {
        printf("secretkey_loader: Providing SECURE key (first 4 bytes: %02X %02X %02X %02X)\n",
               secure_key[0], secure_key[1], secure_key[2], secure_key[3]);
        memcpy(key, secure_key, 16);
    } else {
        printf("secretkey_loader: Providing DEFAULT key (use_secure=%d, available=%d)\n",
               use_secure_key, secure_key_available);
        memcpy(key, default_key, 16);
    }
    return 0;
}

bool kvstore_init(void) {
    printf("kvstore_init: Initializing pico-kvstore system...\n");
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

    // Note: If flash is uninitialized, kvstore operations will fail gracefully
    // Use 'picotool load -o 0x101E0000 kvstore.bin' to format the flash region
    // Or use kvstore-util to create and flash a blank image

    // Create log-structured KVS for wear leveling
    kvs_t *logkvs = kvs_logkvs_create(blockdev);
    if (logkvs == NULL) {
        printf("kvstore_init: Failed to create log-structured KVS\n");
        blockdevice_flash_free(blockdev);
        return false;
    }
    printf("kvstore_init: Log-structured KVS created\n");

    // Create secure KVS with our key loader
    kvs_t *securekvs = kvs_securekvs_create(logkvs, secretkey_loader);
    if (securekvs == NULL) {
        printf("kvstore_init: Failed to create secure KVS\n");
        kvs_logkvs_free(logkvs);
        blockdevice_flash_free(blockdev);
        return false;
    }
    printf("kvstore_init: Secure KVS created\n");

    // Assign as global instance
    printf("kvstore_init: Assigning global KVS instance\n");
    kvs_assign(securekvs);
    printf("kvstore_init: Global KVS assigned\n");

    // Note: kvs_logkvs_create() already initializes everything during creation
    // There's no separate init() method - initialization happens in the create functions
    printf("kvstore_init: Initialization complete (no separate init needed)\n");
    return true;
}

bool kvstore_set_encryption_key(const uint8_t key[16]) {
    // First, check if password hash exists and validate
    uint8_t stored_hash[TC_SHA256_DIGEST_SIZE];
    size_t hash_size;

    kvstore_use_default_key();  // Hash is stored with default key
    int ret = kvs_get(PASSWORD_HASH_KEY, stored_hash, sizeof(stored_hash), &hash_size);

    if (ret == 0 && hash_size == TC_SHA256_DIGEST_SIZE) {
        // Password hash exists - validate the key
        struct tc_sha256_state_struct s;
        uint8_t computed_hash[TC_SHA256_DIGEST_SIZE];

        tc_sha256_init(&s);
        tc_sha256_update(&s, key, 16);
        tc_sha256_final(computed_hash, &s);

        if (memcmp(stored_hash, computed_hash, TC_SHA256_DIGEST_SIZE) != 0) {
            printf("kvstore_init: Password validation failed (incorrect password)\n");
            return false;  // Wrong password
        }
        printf("kvstore_init: Password validated successfully\n");
    } else {
        // No hash stored yet - this is first password set, create hash
        struct tc_sha256_state_struct s;
        uint8_t computed_hash[TC_SHA256_DIGEST_SIZE];

        tc_sha256_init(&s);
        tc_sha256_update(&s, key, 16);
        tc_sha256_final(computed_hash, &s);

        kvstore_use_default_key();
        ret = kvs_set(PASSWORD_HASH_KEY, computed_hash, TC_SHA256_DIGEST_SIZE);
        if (ret != 0) {
            printf("kvstore_init: Failed to store password hash: %s\n", kvs_strerror(ret));
        } else {
            printf("kvstore_init: Password hash stored (first password set)\n");
        }
    }

    memcpy(secure_key, key, 16);
    secure_key_available = true;
    use_secure_key = true;  // Switch to secure key mode
    printf("kvstore_init: Secure encryption key loaded (device unlocked)\n");
    return true;
}

void kvstore_clear_encryption_key(void) {
    // Clear key from memory for security
    memset(secure_key, 0, sizeof(secure_key));
    secure_key_available = false;
    use_secure_key = false;  // Switch back to default key mode
    printf("kvstore_init: Secure encryption key cleared (device locked, using default key)\n");
}

bool kvstore_is_unlocked(void) {
    return secure_key_available;
}

void kvstore_use_default_key(void) {
    use_secure_key = false;
}

void kvstore_use_secure_key(void) {
    if (secure_key_available) {
        use_secure_key = true;
    }
    // Note: If secure key not available, stays on default key
}

int kvs_get_any(const char *key, void *buffer, size_t bufsize, size_t *actual_size) {
    // Try with current key context first
    printf("kvs_get_any: Reading '%s', use_secure_key=%d, secure_key_available=%d\n",
           key, use_secure_key, secure_key_available);

    int ret = kvs_get(key, buffer, bufsize, actual_size);
    printf("kvs_get_any: First attempt with %s key: %s\n",
           use_secure_key ? "SECURE" : "DEFAULT",
           ret == 0 ? "SUCCESS" : kvs_strerror(ret));

    if (ret == KVSTORE_ERROR_AUTHENTICATION_FAILED) {
        // Authentication failed - try with opposite key
        bool was_secure = use_secure_key;
        use_secure_key = !was_secure;

        printf("kvs_get_any: Retrying with %s key...\n",
               use_secure_key ? "SECURE" : "DEFAULT");

        ret = kvs_get(key, buffer, bufsize, actual_size);
        printf("kvs_get_any: Second attempt: %s\n",
               ret == 0 ? "SUCCESS" : kvs_strerror(ret));

        // Restore original key context
        use_secure_key = was_secure;

        if (ret == 0) {
            printf("kvs_get_any: Successfully read '%s' with opposite key\n", key);
        }
    }

    return ret;
}
