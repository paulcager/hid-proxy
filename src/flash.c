//
// Legacy flash functions - mostly obsolete with kvstore migration
// Kept for compatibility during transition
//

#include <pico/flash.h>
#include "hid_proxy.h"
#include "encryption.h"
#include "kvstore_init.h"
#include "keydef_store.h"

// OBSOLETE: save_state() is no longer needed with kvstore
// Individual keydefs are saved via keydef_save() on-demand
// This stub is kept for backward compatibility with http_server.c
void save_state(kb_t *kb) {
    // With kvstore, keydefs are saved individually via keydef_save()
    // This function is now a no-op
    LOG_INFO("save_state() is obsolete with kvstore migration\n");
    assert_sane(kb);
}

// OBSOLETE: read_state() is no longer needed with kvstore
// Keydefs are loaded on-demand via keydef_load()
void read_state(kb_t *kb) {
    // With kvstore, keydefs are loaded on-demand
    // This function is now a no-op
    LOG_INFO("read_state() is obsolete with kvstore migration\n");
    assert_sane(kb);
}

// Initialize device to blank/empty state
// Clears all keydefs and encryption key
void init_state(kb_t *kb) {
    LOG_INFO("Initializing to blank state\n");

    // Clear encryption key
    enc_clear_key();
    kvstore_clear_encryption_key();

    // Delete all keydefs from kvstore
    uint8_t triggers[64];
    int count = keydef_list(triggers, 64);
    for (int i = 0; i < count; i++) {
        keydef_delete(triggers[i]);
    }

    // Clear local store buffer
    memset(kb->local_store, 0, FLASH_STORE_SIZE);
    memcpy(kb->local_store->magic, FLASH_STORE_MAGIC, sizeof(kb->local_store->magic));

    kb->status = blank;

    LOG_INFO("Device initialized to blank state\n");
    assert_sane(kb);
}

