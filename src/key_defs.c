#include <stdlib.h>
#include <pico/multicore.h>
#include <pico/bootrom.h>
#include "hid_proxy.h"
#include "usb_descriptors.h"
#include "encryption.h"
#include "macros.h"
#include "keydef_store.h"
#include "kvstore_init.h"
#ifdef ENABLE_NFC
#include "nfc_tag.h"
#endif

#ifdef PICO_CYW43_SUPPORTED
#include "wifi_config.h"
#endif

static hid_keyboard_report_t release_all_keys = {0, 0, {0, 0, 0, 0, 0, 0}};

void evaluate_keydef(hid_keyboard_report_t *report, uint8_t key0);

__attribute__((unused)) char keycode_to_letter_or_digit(uint8_t keycode);

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
                    enc_clear_password();
                    printf("Enter password\n");
                    return;

                case HID_KEY_INSERT:
                    // Change password while locked (will re-encrypt)
                    kb.status = entering_new_password;
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
                kvstore_set_encryption_key(key);

                if (kb.status == entering_password) {
                    // Unlocking - no need to re-save anything
                    kb.status = normal;
                    printf("Unlocked\n");
                } else {
                    // Changing password - need to re-encrypt all keydefs
                    // Load all keydefs, they will be automatically re-encrypted with new key on save
                    uint8_t triggers[64];
                    int count = keydef_list(triggers, 64);

                    for (int i = 0; i < count; i++) {
                        keydef_t *def = keydef_load(triggers[i]);
                        if (def != NULL) {
                            keydef_save(def);  // Re-save with new encryption key
                            free(def);
                        }
                    }

                    kb.status = normal;
                    printf("Password changed and data re-encrypted\n");
                }
            }

            return;

        case normal:
            if (kb_report->modifier == 0x22 && key0 == 0) {
                kb.status = seen_magic;
            } else {
                LOG_TRACE("Adding to host Q: instance=%d, report_id=%d, len=%d\n", 0, REPORT_ID_KEYBOARD, sizeof(hid_keyboard_report_t));
                add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t), kb_report);
            }

            return;

        case seen_magic:
            if (kb_report->modifier == 0x00 && key0 == 0) {
                kb.status = expecting_command;
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
                    kb.status = normal;
                    return;
                }
#else
                    // NFC not enabled - ignore PRINT_SCREEN command
                    kb.status = normal;
                    return;
#endif

                case HID_KEY_ESCAPE:
                    kb.status = normal;
                    return;

                case HID_KEY_EQUAL:
                    kb.status = seen_assign;
                    return;

                case HID_KEY_SPACE:
                    kb.status = normal;
                    print_keydefs();
                    web_access_enable();
                    return;

                case HID_KEY_ENTER:
                    // When unlocked, ENTER now starts capturing keystrokes to unlock (if locked).
                    // Re-encryption moved to INSERT to avoid accidental data loss.
                    kb.status = normal;
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
                    lock();
                    return;

                default:
                    kb.status = normal;
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

                kb.status = normal;
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
    LOG_INFO("Defining keycode %d\n", key0);
    LOG_DEBUG("New def for %x is at %p\n", key0, kb.key_being_defined);
}

void evaluate_keydef(hid_keyboard_report_t *report, uint8_t key0) {
    // Load keydef from kvstore on-demand
    keydef_t *def = keydef_load(key0);

    if (def == NULL) {
        LOG_INFO("No sequence defined for keycode %x\n", key0);
        add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t), report);
        add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t),&release_all_keys);
        return;
    }

    LOG_INFO("Executing keycode %x with %d sequences\n", key0, def->count);

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
    // List all keydefs from kvstore
    uint8_t triggers[64];
    int count = keydef_list(triggers, 64);

    if (count == 0) {
        printf("No key definitions found\n");
        return;
    }

    printf("Key definitions (%d total):\n", count);
    for (int i = 0; i < count; i++) {
        keydef_t *def = keydef_load(triggers[i]);
        if (def != NULL) {
            print_keydef(def);
            free(def);
        }
    }
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

// Quick, dirty and must be replaced!
char keycode_to_letter_or_digit(uint8_t keycode) {
    static const char trans_table[] = "....abcdefghijklmnopqrstuvwxyz1234567890";
    if (keycode > strlen(trans_table)) {
        return '.';
    }

    return trans_table[keycode];
}