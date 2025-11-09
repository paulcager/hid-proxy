/*
 * keydef_store.c
 *
 * Key definition storage using kvstore
 */

#include "keydef_store.h"
#include "kvstore.h"
#include "logging.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Key prefix for keydefs in kvstore
#define KEYDEF_PREFIX "keydef."

/*! \brief Generate kvstore key for a given HID keycode
 *
 * \param trigger HID keycode
 * \param key_buf Buffer to receive key string (must be at least 16 bytes)
 */
static void keydef_make_key(uint8_t trigger, char *key_buf) {
    snprintf(key_buf, 16, "%s0x%02X", KEYDEF_PREFIX, trigger);
}

keydef_t *keydef_alloc(uint8_t trigger, uint16_t count) {
    size_t size = sizeof(keydef_t) + count * sizeof(hid_keyboard_report_t);
    keydef_t *keydef = (keydef_t *)malloc(size);
    if (keydef == NULL) {
        LOG_ERROR("keydef_alloc: malloc failed for %u bytes\n", size);
        return NULL;
    }

    memset(keydef, 0, size);
    keydef->trigger = trigger;
    keydef->count = count;
    keydef->require_unlock = true;  // Phase 4: interactive keydefs default to private

    return keydef;
}

bool keydef_save(const keydef_t *keydef) {
    char key[16];
    keydef_make_key(keydef->trigger, key);

    size_t size = keydef_size(keydef);

    // Phase 4: Check require_unlock flag to decide whether to encrypt
    int ret;
    if (keydef->require_unlock) {
        // Private keydef - requires encryption
        ret = kvs_set_flag(key, keydef, size, KVSTORE_REQUIRE_CONFIDENTIALITY_FLAG);
        LOG_DEBUG("keydef_save: Saved PRIVATE keydef 0x%02X (%u reports)\n",
                  keydef->trigger, keydef->count);
    } else {
        // Public keydef - no encryption needed
        ret = kvs_set_flag(key, keydef, size, 0);
        LOG_DEBUG("keydef_save: Saved PUBLIC keydef 0x%02X (%u reports)\n",
                  keydef->trigger, keydef->count);
    }

    if (ret != 0) {
        LOG_ERROR("keydef_save: Failed to save keydef 0x%02X: %s\n",
                  keydef->trigger, kvs_strerror(ret));
        return false;
    }

    return true;
}

keydef_t *keydef_load(uint8_t trigger) {
    char key[16];
    keydef_make_key(trigger, key);

    // First, get the size
    size_t size;
    int ret = kvs_get(key, NULL, 0, &size);
    if (ret != 0) {
        if (ret == KVSTORE_ERROR_AUTHENTICATION_FAILED) {
            LOG_DEBUG("keydef_load: Keydef 0x%02X is encrypted, device locked\n", trigger);
        } else if (ret == KVSTORE_ERROR_ITEM_NOT_FOUND) {
            LOG_DEBUG("keydef_load: Keydef 0x%02X not found\n", trigger);
        } else {
            LOG_ERROR("keydef_load: Failed to get size for keydef 0x%02X: %s\n",
                      trigger, kvs_strerror(ret));
        }
        return NULL;
    }

    // Allocate and load
    keydef_t *keydef = (keydef_t *)malloc(size);
    if (keydef == NULL) {
        LOG_ERROR("keydef_load: malloc failed for %u bytes\n", size);
        return NULL;
    }

    ret = kvs_get(key, keydef, size, &size);
    if (ret != 0) {
        LOG_ERROR("keydef_load: Failed to load keydef 0x%02X: %s\n",
                  trigger, kvs_strerror(ret));
        free(keydef);
        return NULL;
    }

    LOG_DEBUG("keydef_load: Loaded keydef 0x%02X (%u reports)\n",
              keydef->trigger, keydef->count);
    return keydef;
}

bool keydef_delete(uint8_t trigger) {
    char key[16];
    keydef_make_key(trigger, key);

    int ret = kvs_delete(key);
    if (ret != 0) {
        LOG_ERROR("keydef_delete: Failed to delete keydef 0x%02X: %s\n",
                  trigger, kvs_strerror(ret));
        return false;
    }

    LOG_INFO("keydef_delete: Deleted keydef 0x%02X\n", trigger);
    return true;
}

int keydef_list(uint8_t *triggers, size_t max_count) {
    kvs_find_t ctx;
    char key[32];
    int count = 0;

    int ret = kvs_find(KEYDEF_PREFIX, &ctx);
    if (ret != 0) {
        LOG_DEBUG("keydef_list: No keydefs found or error: %s\n", kvs_strerror(ret));
        return 0;
    }

    while (kvs_find_next(&ctx, key, sizeof(key)) == 0 && count < max_count) {
        // Parse "keydef.0xXX" to extract trigger HID code
        unsigned int trigger;
        if (sscanf(key, KEYDEF_PREFIX "0x%X", &trigger) == 1) {
            triggers[count++] = (uint8_t)trigger;
            LOG_DEBUG("keydef_list: Found keydef %s (trigger=0x%02X)\n", key, trigger);
        } else {
            LOG_ERROR("keydef_list: Failed to parse keydef key: %s\n", key);
        }
    }

    kvs_find_close(&ctx);

    LOG_DEBUG("keydef_list: Found %d keydefs\n", count);
    return count;
}
