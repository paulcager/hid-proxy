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
#include <string.h>
#include <stdio.h>

// Encryption key storage
static uint8_t encryption_key[16];
static bool key_available = false;

/*! \brief Secret key loader callback for secure KVS
 *
 * This function is called by the secure KVS layer when it needs
 * the encryption key to encrypt/decrypt data.
 *
 * \param key Buffer to receive the 16-byte AES key
 * \return 0 on success, -1 if no key is available
 */
static int secretkey_loader(uint8_t *key) {
    if (!key_available) {
        return -1;  // No key available (device locked)
    }
    memcpy(key, encryption_key, 16);
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

void kvstore_set_encryption_key(const uint8_t key[16]) {
    memcpy(encryption_key, key, 16);
    key_available = true;
    printf("kvstore_init: Encryption key loaded (device unlocked)\n");
}

void kvstore_clear_encryption_key(void) {
    // Clear key from memory for security
    memset(encryption_key, 0, sizeof(encryption_key));
    key_available = false;
    printf("kvstore_init: Encryption key cleared (device locked)\n");
}

bool kvstore_is_unlocked(void) {
    return key_available;
}
