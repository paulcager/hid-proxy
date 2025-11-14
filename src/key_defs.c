#include <stdlib.h>
#include <pico/multicore.h>
#include <pico/bootrom.h>
#include "hid_proxy.h"
#include "usb_descriptors.h"
#include "encryption.h"
#include "macros.h"
#include "keydef_store.h"
#include "kvstore_init.h"
#include "kvstore.h"
#ifdef ENABLE_NFC
#include "nfc_tag.h"
#endif

#ifdef PICO_CYW43_SUPPORTED
#include "wifi_config.h"
#endif

static hid_keyboard_report_t release_all_keys = {0, 0, {0, 0, 0, 0, 0, 0}};

void evaluate_keydef(hid_keyboard_report_t *report, uint8_t key0);

void start_define(uint8_t key0);

// next_keydef() is now defined in macros.h


void handle_keyboard_report(hid_keyboard_report_t *kb_report) {
#ifdef DEBUG
    print_key_report(kb_report);
#endif

    uint8_t key0 = kb_report->keycode[0];

    // No matter what state we are in "double-shift + HOME" means reboot into upload mode.
    if (kb_report->modifier == 0x22 && key0 == HID_KEY_HOME) {
        multicore_reset_core1();
        reset_usb_boot(0,0);
        return;
    }

    switch (kb.status) {

        case blank:
            if (kb_report->modifier == 0x22 && key0 == 0) {
                kb.status = blank_seen_magic;
            } else {
                add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t), kb_report);
            }

            return;

        case blank_seen_magic:
            switch (key0) {
                case 0:
                    return;

                case HID_KEY_ESCAPE:
                    kb.status = blank;
                    return;

                case HID_KEY_INSERT:
                    // Set first password
                    kb.status = entering_new_password;
                    led_on_interval_ms = 50;   // Fast flash during password entry
                    led_off_interval_ms = 50;
                    enc_clear_password();
                    printf("Enter new password\n");
                    return;

                case HID_KEY_DELETE:
                    // Already blank, just re-initialize to be safe
                    init_state(&kb);
                    return;

                default:
                    // Any other key returns to blank and forwards keystroke
                    kb.status = blank;
                    add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t), kb_report);
                    return;
            }

        case locked:
            if (kb_report->modifier == 0x22 && key0 == 0) {
                kb.status = locked_seen_magic;
            } else {
                add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t), kb_report);
            }

            return;

        case locked_seen_magic:
            // Wait for all keys to be released before accepting commands
            if (kb_report->modifier == 0x00 && key0 == 0) {
                kb.status = locked_expecting_command;
            }
            return;

        case locked_expecting_command:
            switch (key0) {
                case 0:
                    return;

                case HID_KEY_ESCAPE:
                    kb.status = locked;
                    return;

                case HID_KEY_ENTER:
                    kb.status = entering_password;
                    led_on_interval_ms = 50;   // Fast flash during password entry
                    led_off_interval_ms = 50;
                    enc_clear_password();
                    printf("Enter password\n");
                    return;

                case HID_KEY_INSERT:
                    // Change password while locked (will re-encrypt)
                    kb.status = entering_new_password;
                    led_on_interval_ms = 50;   // Fast flash during password entry
                    led_off_interval_ms = 50;
                    enc_clear_password();
                    printf("Enter new password\n");
                    return;

                case HID_KEY_DELETE:
                    // Erase everything and return to blank state
                    init_state(&kb);
                    return;

                default:
                    // Try to evaluate public keydefs even when locked
                    // evaluate_keydef() will only succeed for public (unencrypted) keydefs
                    kb.status = locked;
                    evaluate_keydef(kb_report, key0);
                    return;
            }

        case entering_password:
        case entering_new_password:
            if (key0 == 0) {
                // Ignore keyups.
                return;
            }

            if (key0 != HID_KEY_ENTER) {
                enc_add_password_byte(key0);
            } else {
                enc_derive_key_from_password();

                // Update kvstore encryption key
                uint8_t key[16];
                enc_get_key(key, sizeof(key));
                bool password_ok = kvstore_set_encryption_key(key);

                if (!password_ok) {
                    // Wrong password
                    printf("Incorrect password\n");
                    kb.status = locked;
                    led_on_interval_ms = 0;   // LED off when locked
                    led_off_interval_ms = 0;
                    enc_clear_key();
                } else if (kb.status == entering_password) {
                    // Unlocking - no need to re-save anything
                    unlock();
                    led_on_interval_ms = 100;    // Slow pulse when unlocked
                    led_off_interval_ms = 2400;
                    printf("Unlocked\n");
                } else {
                    // Changing/setting password
                    // Only re-encrypt if there are keydefs AND we can read them
                    // (i.e., this is a password change, not first-time setup)
                    printf("Password set successfully\n");
                    unlock();
                    led_on_interval_ms = 100;    // Slow pulse when unlocked
                    led_off_interval_ms = 2400;

                    // TODO: Add password change support later
                    // For now, password can only be set once on blank device
                    // To change password, user must erase device (double-shift DEL) and start over
                }
            }

            return;

        case normal:
            if (kb_report->modifier == 0x22 && key0 == 0) {
                kb.status = seen_magic;
                led_on_interval_ms = 50;   // Fast flash during command mode
                led_off_interval_ms = 50;
            } else {
                LOG_TRACE("Adding to host Q: instance=%d, report_id=%d, len=%d\n", 0, REPORT_ID_KEYBOARD, sizeof(hid_keyboard_report_t));
                add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t), kb_report);
            }

            return;

        case seen_magic:
            if (kb_report->modifier == 0x00 && key0 == 0) {
                kb.status = expecting_command;
                // Keep fast flash (already set to 100ms)
            }
            return;

        case expecting_command:
            switch (key0) {
                case 0:
                    return;

                case HID_KEY_PRINT_SCREEN:
#ifdef ENABLE_NFC
                {
                    // Write full 32-byte AES-256 key to NFC (stored across 2 blocks: 0x3A and 0x3B)
                    uint8_t key[32];
                    enc_get_key(key, sizeof(key));
                    nfc_write_key(key, sizeof(key), 30 * 1000);
                    unlock();
                    return;
                }
#else
                    // NFC not enabled - ignore PRINT_SCREEN command
                    unlock();
                    return;
#endif

                case HID_KEY_ESCAPE:
                    unlock();
                    led_on_interval_ms = 100;    // Back to slow pulse
                    led_off_interval_ms = 2400;
                    return;

                case HID_KEY_EQUAL:
                    kb.status = seen_assign;
                    return;

                case HID_KEY_SPACE:
                    unlock();
                    print_keydefs();
#ifdef PICO_CYW43_SUPPORTED
                    web_access_enable();
#endif
                    return;

                case HID_KEY_ENTER:
                    // When unlocked, ENTER now starts capturing keystrokes to unlock (if locked).
                    // Re-encryption moved to INSERT to avoid accidental data loss.
                    unlock();
                    return;

                case HID_KEY_INSERT:
                            kb.status = entering_new_password;
                            enc_clear_password();
                            printf("Enter new password\n");
                    return;

                case HID_KEY_DELETE:
                    init_state(&kb);
                    return;

                case HID_KEY_END:
                    lock();  // Sets LED to off (0ms) internally
                    return;

                default:
                    unlock();
                    led_on_interval_ms = 100;    // Back to slow pulse
                    led_off_interval_ms = 2400;
                    evaluate_keydef(kb_report, key0);
                    return;
            }

        case seen_assign:
            if (key0 == 0) {
                // Ignore keyups.
                return;
            }

            start_define(key0);
            return;

        case defining:
            if (kb_report->modifier == 0x22 && key0 == 0) {
                LOG_INFO("End of definition: about to save\n");

                // Save the keydef to kvstore
                if (kb.key_being_defined != NULL) {
                    if (keydef_save(kb.key_being_defined)) {
                        LOG_INFO("Saved keydef 0x%02x with %d reports\n",
                                kb.key_being_defined->trigger, kb.key_being_defined->count);
                    } else {
                        LOG_ERROR("Failed to save keydef 0x%02x\n", kb.key_being_defined->trigger);
                    }
                    free(kb.key_being_defined);
                    kb.key_being_defined = NULL;
                }

                unlock();
                led_on_interval_ms = 100;    // Back to slow pulse after defining
                led_off_interval_ms = 2400;
                return;
            }

            keydef_t *this_def = kb.key_being_defined;

            // Check if we have space in the allocated buffer
            // The buffer was allocated with initial capacity, check if we need to grow it
            if (this_def->count >= 64) {
                LOG_ERROR("Maximum macro length reached (%d reports) for keycode %02x. Ignoring report.\n",
                         this_def->count, this_def->trigger);
                return; // Ignore the report to prevent overflow
            }

            this_def->reports[this_def->count] = *kb_report;
            this_def->count++;
            print_keydef(this_def);
    }
}

void start_define(uint8_t key0) {
    LOG_INFO("Defining keycode %02x\n", key0);

    // Delete any existing definition for this keycode
    keydef_delete(key0);

    // Allocate a temporary keydef for recording (start with space for 64 reports)
    // We'll grow this as needed during recording
    kb.key_being_defined = keydef_alloc(key0, 64);
    if (kb.key_being_defined == NULL) {
        panic("Failed to allocate keydef");
    }

    kb.key_being_defined->count = 0;  // No reports recorded yet

    kb.status = defining;
    led_on_interval_ms = 50;   // Fast flash during definition
    led_off_interval_ms = 50;
    LOG_INFO("Defining keycode %d\n", key0);
    LOG_DEBUG("New def for %x is at %p\n", key0, kb.key_being_defined);
}

void evaluate_keydef(hid_keyboard_report_t *report, uint8_t key0) {
    // Load keydef from kvstore on-demand
    printf("evaluate_keydef: Looking for keydef 0x%02X, device %s\n",
           key0, kvstore_is_unlocked() ? "UNLOCKED" : "LOCKED");

    keydef_t *def = keydef_load(key0);

    if (def == NULL) {
        printf("evaluate_keydef: No sequence defined for keycode 0x%02X\n", key0);
        add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t), report);
        add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t),&release_all_keys);
        return;
    }

    printf("evaluate_keydef: Executing keycode 0x%02X with %d sequences (%s)\n",
           key0, def->count, def->require_unlock ? "PRIVATE" : "PUBLIC");

    // Send the macro sequence
    add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t),&release_all_keys);
    for (int i = 0; i < def->count; i++) {
        hid_keyboard_report_t next_report = def->reports[i];
        LOG_TRACE("> %x %x\n", next_report.modifier, next_report.keycode[0]);
        add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t),&next_report);
    }

    // Free the loaded keydef
    free(def);
}


void print_keydefs() {
    // Simple version: just list keys stored in kvstore without loading/printing values
    // This avoids potential crashes from malformed keydefs

    printf("\n=== KVStore Contents ===\n");
    printf("DEBUG: About to call kvs_find...\n");

    kvs_find_t ctx;
    char key[64];
    int ret = kvs_find("", &ctx);  // Find all keys

    printf("DEBUG: kvs_find returned %d (%s)\n", ret, ret == 0 ? "SUCCESS" : kvs_strerror(ret));

    if (ret != 0) {
        printf("Error listing kvstore: %s\n", kvs_strerror(ret));
        return;
    }

    int count = 0;
    printf("DEBUG: Starting kvs_find_next loop...\n");
    while (kvs_find_next(&ctx, key, sizeof(key)) == 0) {
        printf("DEBUG: Found key '%s'\n", key);

        // For keydefs, try to parse and show info
        if (strncmp(key, "keydef.", 7) == 0) {
            // Load the keydef to get accurate info
            unsigned int trigger;
            if (sscanf(key, "keydef.0x%X", &trigger) == 1) {
                keydef_t *def = keydef_load((uint8_t)trigger);
                if (def != NULL) {
                    printf("  %s: %u reports (%s)\n", key, def->count,
                           def->require_unlock ? "PRIVATE/ENCRYPTED" : "PUBLIC/UNENCRYPTED");
                    free(def);
                } else {
                    printf("  %s: (failed to load)\n", key);
                }
            } else {
                printf("  %s: (invalid key format)\n", key);
            }
        } else {
            // For non-keydef keys, just show they exist
            printf("  %s\n", key);
        }
        count++;

        if (count > 100) {
            printf("DEBUG: Safety break - too many keys!\n");
            break;
        }
    }

    printf("DEBUG: Exited loop, closing find context...\n");
    kvs_find_close(&ctx);
    printf("Total: %d keys\n");
    printf("========================\n\n");

    // Now serialize all keydefs in human-readable format
    printf("=== Human-Readable Macros ===\n");
    char output[8192];  // Buffer for serialized output
    if (serialize_macros_from_kvstore(output, sizeof(output))) {
        printf("%s", output);
    } else {
        printf("Error: Failed to serialize macros\n");
    }
    printf("==============================\n\n");
}

void print_keydef(const keydef_t *def) {
    printf("%02x @%p: count = %d\n", def->trigger, def, def->count);
    for (int i = 0; i < def->count; i++) {
        printf("> %3d ", i);
        print_key_report(&(def->reports[i]));
    }
    printf("--------------\n");
}

void print_key_report(const hid_keyboard_report_t *report) {
    printf("[%02x] %02x %02x ...\n", report->modifier, report->keycode[0], report->keycode[1]);
}
