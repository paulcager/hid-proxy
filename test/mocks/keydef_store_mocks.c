// Mock implementations of keydef_store.h functions for testing
// These are stubs that return errors - not used by the old API tests

#include "hid_proxy.h"
#include <stddef.h>

// Mock stubs for keydef_store functions (not used by old API tests)
keydef_t* keydef_alloc(uint8_t trigger, uint16_t count, bool require_unlock) {
    return NULL; // Not implemented for old API tests
}

bool keydef_save(const keydef_t* def) {
    return false; // Not implemented for old API tests
}

keydef_t* keydef_load(uint8_t trigger) {
    return NULL; // Not implemented for old API tests
}

bool keydef_delete(uint8_t trigger) {
    return false; // Not implemented for old API tests
}

int keydef_list(uint8_t* triggers, int max_triggers) {
    return 0; // Not implemented for old API tests
}
