//
// Implementation of the `assert_sane` macro, used in debug builds to verify the
// structures look to be uncorrupted.
//

#include "hid_proxy.h"

#ifndef NDEBUG
static bool all_zero(uint8_t *ptr, size_t count) {
    for (; count > 0; ptr++, count--) {
        if (*ptr) return false;
    }

    return true;
}

void assert_sane_func(char *file, int line, kb_t *k) {
    bool sane = true;
    sane &= memcmp(k->local_store->magic, FLASH_STORE_MAGIC, sizeof(k->local_store->magic)) == 0;

    // Skip detailed validation for locked states and password entry states
    // Also skip for blank states (no password set yet, IV may be zeros)
    if (k->status != blank &&
        k->status != blank_seen_magic &&
        k->status != locked &&
        k->status != locked_seen_magic &&
        k->status != entering_password) {
        sane &= memcmp(k->local_store->encrypted_magic, FLASH_STORE_MAGIC, sizeof(k->local_store->encrypted_magic)) == 0;
        sane &= !all_zero(k->local_store->iv, sizeof(k->local_store->iv));

        // Check for uninitialized / corrupt keydefs. Sizes > 0xfff (or < 0) seem improbable.
        sane &= (k->local_store->keydefs[0].count & ~0x0fff) == 0;
        sane &= (k->local_store->keydefs[0].trigger & ~0x00ff) == 0;
    }

    if (sane) {
//        printf("Looks sane with status %d [%s:%d]\n", k->status, file, line);
//        hex_dump(k->local_store, 32);
    } else {
        printf("KB structure @%p looks to be corrupt [%s:%d]\n", k, file, line);
        printf("Status is %d\n", k->status);
        printf("keydef[0].count=0x%0x, keydef[0].trigger=0x%0x\n", k->local_store->keydefs[0].count, k->local_store->keydefs[0].trigger);
        printf("IV:              "); hex_dump(k->local_store->iv, 16);
        printf("Magic:           "); hex_dump(k->local_store->magic, sizeof(k->local_store->magic));
        printf("Encrypted Magic: "); hex_dump(k->local_store->encrypted_magic, sizeof(k->local_store->encrypted_magic));
        panic("Corrupt stuff");
    }
}
#endif