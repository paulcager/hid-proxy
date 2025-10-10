//
// Save / restore state from flash..
//

#include <pico/flash.h>
#include "hid_proxy.h"
#include "encryption.h"

static void safe_save_state(void *store) {
    flash_range_erase(FLASH_STORE_OFFSET, FLASH_STORE_SIZE);
    flash_range_program(FLASH_STORE_OFFSET, store, FLASH_STORE_SIZE);
}

void save_state(kb_t *kb) {
    // TODO - we only need to write used portion (save on erases).
    // Note that we encrypt+decrypt the buffer in place, to save allocating temporary store.

    assert_sane(kb);

    absolute_time_t start = get_absolute_time();
    store_encrypt(kb);
    absolute_time_t end = get_absolute_time();
    LOG_INFO("Encrypt took %lld μs (%ld millis)\n", to_us_since_boot(end) - to_us_since_boot(start), to_ms_since_boot(end) - to_ms_since_boot(start));

    start = get_absolute_time();
    int ret = flash_safe_execute(safe_save_state, kb->local_store, 20);
    end = get_absolute_time();
    LOG_INFO("Store took %lld μs (%ld millis)\n", to_us_since_boot(end) - to_us_since_boot(start), to_ms_since_boot(end) - to_ms_since_boot(start));

    if (ret != PICO_OK) {
        panic("flash_safe_execute returned %d", ret);
    }

    if (memcmp(kb->local_store, FLASH_STORE_ADDRESS, FLASH_STORE_SIZE) != 0) {
        panic("Didn't write what we thought we wrote");
    }

    // Now restore to unencrypted contents.
    store_decrypt(kb);

    assert_sane(kb);
}

void read_state(kb_t *kb) {
    // Check if flash is blank (magic is not expected value)
    bool has_magic = memcmp(FLASH_STORE_ADDRESS, FLASH_STORE_MAGIC, 8) == 0;

    if (!has_magic) {
        LOG_INFO("Flash appears blank/corrupt - initializing\n");
        init_state(kb);
    } else {
        memcpy(kb->local_store, FLASH_STORE_ADDRESS, FLASH_STORE_SIZE);
        if (!store_decrypt(kb)){
            LOG_ERROR("Could not decrypt\n");
            kb->status = locked;
        } else {
            kb->status = normal;
            LOG_INFO("Unlocked\n");
        }
    }

    assert_sane(kb);
}

void init_state(kb_t *kb) {
    // "Format" with an empty key.
    memset(kb->local_store, 0, FLASH_STORE_SIZE);
    memcpy(kb->local_store->magic, FLASH_STORE_MAGIC, sizeof(kb->local_store->magic));
    enc_clear_key();

    save_state(kb);

    kb->status = blank;

    assert_sane(kb);
}

