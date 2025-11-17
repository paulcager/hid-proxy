//
// Flash utility functions for device state management
//

#include <pico/flash.h>
#include "hid_proxy.h"
#include "encryption.h"
#include "kvstore_init.h"
#include "kvstore.h"
#include "keydef_store.h"

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

    // Delete password hash to allow setting new password
    kvs_delete(PASSWORD_HASH_KEY);

    kb->status = blank;

    LOG_INFO("Device initialized to blank state\n");
}

