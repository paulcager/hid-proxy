/*
 * keydef_store.c
 *
 * Key definition storage using kvstore
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

    // Switch to appropriate encryption key based on privacy requirement
    if (keydef->require_unlock) {
        // Private keydef - use secure key (PBKDF2-derived from password)
        kvstore_use_secure_key();
        printf("keydef_save: Saving PRIVATE keydef '%s' (0x%02X, %u reports, %zu bytes) with SECURE key, device %s\n",
                  key, keydef->trigger, keydef->count, size,
                  kvstore_is_unlocked() ? "UNLOCKED" : "LOCKED");
    } else {
        // Public keydef - use default key (hardcoded, always available)
        kvstore_use_default_key();
        printf("keydef_save: Saving PUBLIC keydef '%s' (0x%02X, %u reports, %zu bytes) with DEFAULT key\n",
                  key, keydef->trigger, keydef->count, size);
    }

    // All keydefs are encrypted, but with different keys
    printf("keydef_save: About to call kvs_set('%s', %p, %zu)...\n", key, keydef, size);
    printf("keydef_save: keydef contents: trigger=0x%02X, count=%u, require_unlock=%d\n",
           keydef->trigger, keydef->count, keydef->require_unlock);

    // Hex dump first 32 bytes of what we're writing
    printf("keydef_save: Data hex dump (first %zu bytes): ", size < 32 ? size : 32);
    for (size_t i = 0; i < (size < 32 ? size : 32); i++) {
        printf("%02X ", ((uint8_t*)keydef)[i]);
    }
    printf("\n");

    int ret = kvs_set(key, keydef, size);
    printf("keydef_save: kvs_set returned: %d (%s)\n", ret, ret == 0 ? "SUCCESS" : kvs_strerror(ret));

    if (ret != 0) {
        printf("keydef_save: FAILED to save keydef '%s': %s\n",
                  key, kvs_strerror(ret));
        return false;
    }

    printf("keydef_save: Successfully saved keydef '%s'\n", key);

    // IMMEDIATE TEST: First try writing a simple string to verify kvstore works
    printf("keydef_save: KVSTORE BASIC TEST - writing simple string...\n");
    const char *test_val = "test123";
    ret = kvs_set("test.key", test_val, strlen(test_val) + 1);
    printf("keydef_save: KVSTORE BASIC TEST write: %s\n", ret == 0 ? "SUCCESS" : kvs_strerror(ret));

    if (ret == 0) {
        char read_buf[32];
        size_t read_size;
        ret = kvs_get("test.key", read_buf, sizeof(read_buf), &read_size);
        printf("keydef_save: KVSTORE BASIC TEST read: %s", ret == 0 ? "SUCCESS" : kvs_strerror(ret));
        if (ret == 0) {
            printf(" (value='%s')", read_buf);
        }
        printf("\n");
    }

    // Now try to read back the actual keydef
    printf("keydef_save: IMMEDIATE VERIFICATION - trying to read back keydef...\n");
    size_t verify_size;
    ret = kvs_get_any(key, NULL, 0, &verify_size);
    printf("keydef_save: IMMEDIATE VERIFICATION: %s", ret == 0 ? "SUCCESS" : kvs_strerror(ret));
    if (ret == 0) {
        printf(" (size=%zu, expected=%zu)", verify_size, size);
    }
    printf("\n");

    // If successful, try reading the actual data
    if (ret == 0 && verify_size == size) {
        uint8_t *verify_buf = malloc(verify_size);
        if (verify_buf) {
            ret = kvs_get_any(key, verify_buf, verify_size, &verify_size);
            printf("keydef_save: IMMEDIATE READ DATA: %s\n", ret == 0 ? "SUCCESS" : kvs_strerror(ret));
            if (ret == 0) {
                printf("keydef_save: Read back hex (first %zu bytes): ", verify_size < 32 ? verify_size : 32);
                for (size_t i = 0; i < (verify_size < 32 ? verify_size : 32); i++) {
                    printf("%02X ", verify_buf[i]);
                }
                printf("\n");
            }
            free(verify_buf);
        }
    }

    return true;
}

keydef_t *keydef_load(uint8_t trigger) {
    char key[16];
    keydef_make_key(trigger, key);

    printf("keydef_load: Attempting to load keydef '%s' (0x%02X), device %s\n",
           key, trigger, kvstore_is_unlocked() ? "UNLOCKED" : "LOCKED");

    // First, get the size - try with both keys (kvs_get_any handles fallback)
    size_t size;
    int ret = kvs_get_any(key, NULL, 0, &size);
    if (ret != 0) {
        if (ret == KVSTORE_ERROR_AUTHENTICATION_FAILED) {
            printf("keydef_load: Keydef '%s' requires secure key, device locked\n", key);
        } else if (ret == KVSTORE_ERROR_ITEM_NOT_FOUND) {
            printf("keydef_load: Keydef '%s' NOT FOUND in kvstore\n", key);
        } else {
            printf("keydef_load: Failed to get size for keydef '%s': %s\n",
                      key, kvs_strerror(ret));
        }
        return NULL;
    }

    printf("keydef_load: Found keydef '%s', size=%zu bytes\n", key, size);

    // Allocate and load
    keydef_t *keydef = (keydef_t *)malloc(size);
    if (keydef == NULL) {
        printf("keydef_load: malloc failed for %zu bytes\n", size);
        return NULL;
    }

    ret = kvs_get_any(key, keydef, size, &size);
    if (ret != 0) {
        printf("keydef_load: Failed to load keydef '%s': %s\n",
                  key, kvs_strerror(ret));
        free(keydef);
        return NULL;
    }

    printf("keydef_load: Successfully loaded keydef '%s' (trigger=0x%02X, %u reports, %s)\n",
              key, keydef->trigger, keydef->count,
              keydef->require_unlock ? "PRIVATE" : "PUBLIC");
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
