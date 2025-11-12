/*
 * keydef_store.c
 *
 * Key definition storage using kvstore
 * Simplified version: Direct kvs_get/kvs_set calls, no encryption
 */

#include "keydef_store.h"
#include "kvstore.h"
#include "kvstore_init.h"
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

    printf("keydef_save: Saving keydef '%s' (0x%02X, %u reports, %zu bytes, %s)\n",
              key, keydef->trigger, keydef->count, size,
              keydef->require_unlock ? "PRIVATE" : "PUBLIC");

    // Private keydefs are encrypted, public keydefs are not
    bool encrypt = keydef->require_unlock;
    int ret = kvstore_set_value(key, keydef, size, encrypt);

    if (ret != 0) {
        printf("keydef_save: FAILED to save keydef '%s': %s\n",
                  key, kvs_strerror(ret));
        return false;
    }

    printf("keydef_save: Successfully saved keydef '%s'\n", key);
    return true;
}

keydef_t *keydef_load(uint8_t trigger) {
    char key[16];
    keydef_make_key(trigger, key);

    printf("keydef_load: Attempting to load keydef '%s' (0x%02X)\n", key, trigger);

    // Allocate a reasonable buffer to read into
    // (Max keydef: 6 bytes header + 64 reports * 8 bytes = 518 bytes)
    size_t max_size = sizeof(keydef_t) + 64 * sizeof(hid_keyboard_report_t);
    uint8_t *temp_buffer = (uint8_t *)malloc(max_size);
    if (temp_buffer == NULL) {
        printf("keydef_load: Failed to allocate temp buffer\n");
        return NULL;
    }

    // Read the keydef using wrapper (handles header)
    size_t actual_size;
    bool is_encrypted;
    int ret = kvstore_get_value(key, temp_buffer, max_size, &actual_size, &is_encrypted);

    if (ret != 0) {
        if (ret == KVSTORE_ERROR_ITEM_NOT_FOUND) {
            printf("keydef_load: Keydef '%s' NOT FOUND in kvstore\n", key);
        } else {
            printf("keydef_load: Failed to read keydef '%s': %s\n",
                      key, kvs_strerror(ret));
        }
        free(temp_buffer);
        return NULL;
    }

    printf("keydef_load: Successfully read keydef '%s', size=%zu bytes (%s)\n",
           key, actual_size, is_encrypted ? "ENCRYPTED" : "UNENCRYPTED");

    // Sanity check the size
    if (actual_size < sizeof(keydef_t)) {
        printf("keydef_load: Invalid size %zu (too small, minimum is %zu)\n",
               actual_size, sizeof(keydef_t));
        free(temp_buffer);
        return NULL;
    }

    // Allocate the right-sized keydef and copy data
    keydef_t *keydef = (keydef_t *)malloc(actual_size);
    if (keydef == NULL) {
        printf("keydef_load: malloc failed for %zu bytes\n", actual_size);
        free(temp_buffer);
        return NULL;
    }

    memcpy(keydef, temp_buffer, actual_size);
    free(temp_buffer);

    printf("keydef_load: Loaded keydef data (trigger=0x%02X, count=%u)\n",
              keydef->trigger, keydef->count);

    // Verify the loaded data makes sense
    if (keydef->trigger != trigger) {
        printf("keydef_load: ERROR - trigger mismatch! Expected 0x%02X, got 0x%02X\n",
               trigger, keydef->trigger);
        free(keydef);
        return NULL;
    }

    if (keydef->count > 1024) {
        printf("keydef_load: ERROR - invalid count %u (too large)\n", keydef->count);
        free(keydef);
        return NULL;
    }

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

size_t keydef_list(uint8_t *triggers, size_t max_count) {
    kvs_find_t ctx;
    char key[32];
    size_t count = 0;

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
