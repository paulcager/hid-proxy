/*
 * kvstore_init.c
 *
 * Initialization and management of the pico-kvstore system
 * Simplified version: NO ENCRYPTION (plain logkvs only)
 */

#include "kvstore_init.h"
#include "kvstore.h"
#include "kvstore_logkvs.h"
#include "blockdevice/flash.h"
#include <string.h>
#include <stdio.h>

// Simple unlocked flag (not tied to encryption anymore)
static bool device_unlocked = false;

bool kvstore_init(void) {
    printf("kvstore_init: Initializing pico-kvstore system (NO ENCRYPTION)...\n");
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
    printf("kvstore_init: Log-structured KVS created (unencrypted)\n");

    // Assign as global instance (no securekvs wrapper)
    printf("kvstore_init: Assigning global KVS instance\n");
    kvs_assign(logkvs);
    printf("kvstore_init: Global KVS assigned\n");

    printf("kvstore_init: Initialization complete\n");
    return true;
}

bool kvstore_set_encryption_key(const uint8_t key[16]) {
    // Simplified: Always accept password, just set unlocked flag
    // We keep PBKDF2 key derivation in encryption.c but don't use the key
    printf("kvstore_init: Password accepted (validation disabled for now)\n");
    device_unlocked = true;
    return true;
}

void kvstore_clear_encryption_key(void) {
    // Just clear the unlocked flag
    device_unlocked = false;
    printf("kvstore_init: Device locked\n");
}

bool kvstore_is_unlocked(void) {
    return device_unlocked;
}
