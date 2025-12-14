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
#include "led_control.h"
#ifdef ENABLE_NFC
#include "nfc_tag.h"
#endif

#ifdef PICO_CYW43_SUPPORTED
#include "wifi_config.h"
#include "wifi_console.h"
#include "mqtt_client.h"
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

    if (kb_report->modifier == 0x22 && key0 == HID_KEY_D) {
                    // Dump diagnostic keystroke buffers
                    unseal();
                    diag_dump_buffers();
                    return;
    }

    switch (kb.status) {

        case blank:
            if (kb_report->modifier == 0x22 && key0 == 0) {
                kb.status = blank_seen_magic;
            } else {
                add_to_host_queue_realtime(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t), kb_report);
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
                    led_set_intervals(50, 50);   // Fast flash during password entry
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
                    add_to_host_queue_realtime(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t), kb_report);
                    return;
            }

        case sealed:
            if (kb_report->modifier == 0x22 && key0 == 0) {
                kb.status = sealed_seen_magic;
            } else {
                add_to_host_queue_realtime(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t), kb_report);
            }

            return;

        case sealed_seen_magic:
            // Wait for all keys to be released before accepting commands
            if (kb_report->modifier == 0x00 && key0 == 0) {
                kb.status = sealed_expecting_command;
            }
            return;

        case sealed_expecting_command:
            switch (key0) {
                case 0:
                    return;

                case HID_KEY_ESCAPE:
                    kb.status = sealed;
                    return;

                case HID_KEY_ENTER:
                    kb.status = entering_password;
                    led_set_intervals(50, 50);   // Fast flash during password entry
                    enc_clear_password();
                    printf("Enter password\n");
                    return;

                case HID_KEY_INSERT:
                    // Change password while sealed (will re-encrypt)
                    kb.status = entering_new_password;
                    led_set_intervals(50, 50);   // Fast flash during password entry
                    enc_clear_password();
                    printf("Enter new password\n");
                    return;

                case HID_KEY_DELETE:
                    // Erase everything and return to blank state
                    init_state(&kb);
                    return;

                default:
                    // Try to evaluate public keydefs even when sealed
                    // evaluate_keydef() will only succeed for public (unencrypted) keydefs
                    kb.status = sealed;
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
                    kb.status = sealed;
                    led_set_intervals(0, 0);   // LED off when sealed
                    enc_clear_key();
                } else if (kb.status == entering_password) {
                    // Unlocking - no need to re-save anything
                    unseal();
                    printf("Unsealed\n");
                } else {
                    // Changing password while unsealed
                    // Re-encrypt all encrypted keydefs with new password
                    if (kvstore_change_password(key)) {
                        printf("Password changed successfully - all data re-encrypted\n");
                        unseal();
                    } else {
                        printf("Password change failed\n");
                        kb.status = sealed;
                        led_set_intervals(0, 0);   // LED off when sealed
                        enc_clear_key();
                    }
                }
            }

            return;

        case unsealed:
            if (kb_report->modifier == 0x22 && key0 == 0) {
                kb.status = seen_magic;
                led_set_intervals(50, 50);   // Fast flash during command mode
            } else {
                LOG_TRACE("Adding to host Q: instance=%d, report_id=%d, len=%d\n", 0, REPORT_ID_KEYBOARD, sizeof(hid_keyboard_report_t));
                add_to_host_queue_realtime(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t), kb_report);
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
                    unseal();
                    return;
                }
#else
                    // NFC not enabled - ignore PRINT_SCREEN command
                    unseal();
                    return;
#endif

                case HID_KEY_ESCAPE:
                    unseal();
                    return;

                case HID_KEY_EQUAL:
                    kb.status = seen_assign;
                    return;

                case HID_KEY_SPACE:
                    unseal();
                    print_keydefs();
#ifdef PICO_CYW43_SUPPORTED
                    web_access_enable();
#endif
                    return;

                case HID_KEY_ENTER:
                    // When unsealed, ENTER just unseals (password entry moved to when sealed).
                    // Re-encryption moved to INSERT to avoid accidental data loss.
                    unseal();
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
                    seal();  // Sets LED to off (0ms) internally
                    return;

                case HID_KEY_F12:
#ifdef PICO_CYW43_SUPPORTED
                    // WiFi configuration console
                    printf("\nStarting WiFi configuration...\n");
                    wifi_console_setup();
                    unseal();
                    return;
#else
                    // WiFi not supported on this hardware
                    printf("WiFi not supported on this hardware\n");
                    unseal();
                    return;
#endif

                default:
                    unseal();
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

                unseal();
                return;
            }

            keydef_t *this_def = kb.key_being_defined;

            // Check if we have space in the allocated buffer
            // The buffer was allocated with initial capacity, check if we need to grow it
            if (this_def->count >= 64) {
                LOG_ERROR("Maximum macro length reached (%d actions) for keycode %02x. Ignoring action.\n",
                         this_def->count, this_def->trigger);
                return; // Ignore the action to prevent overflow
            }

            // Record as HID action (interactive definition only records keyboard input)
            this_def->actions[this_def->count].type = ACTION_HID_REPORT;
            this_def->actions[this_def->count].data.hid = *kb_report;
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
    led_set_intervals(50, 50);   // Fast flash during definition
    LOG_INFO("Defining keycode %d\n", key0);
    LOG_DEBUG("New def for %x is at %p\n", key0, kb.key_being_defined);
}

void evaluate_keydef(hid_keyboard_report_t *report, uint8_t key0) {
    // Load keydef from kvstore on-demand
    printf("evaluate_keydef: Looking for keydef 0x%02X, device %s\n",
           key0, kvstore_is_unsealed() ? "UNSEALED" : "SEALED");

    keydef_t *def = keydef_load(key0);

    if (def == NULL) {
        printf("evaluate_keydef: No sequence defined for keycode 0x%02X\n", key0);
        add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t), report);
        add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t),&release_all_keys);
        return;
    }

    printf("evaluate_keydef: Executing keycode 0x%02X with %d actions (%s)\n",
           key0, def->count, def->require_unlock ? "PRIVATE" : "PUBLIC");

    // Execute the macro action sequence
    add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t),&release_all_keys);
    for (int i = 0; i < def->count; i++) {
        action_t *action = &def->actions[i];

        switch (action->type) {
            case ACTION_HID_REPORT:
                LOG_TRACE("> HID %x %x\n", action->data.hid.modifier, action->data.hid.keycode[0]);
                add_to_host_queue(0, ITF_NUM_KEYBOARD, sizeof(hid_keyboard_report_t), &action->data.hid);
                break;

            case ACTION_MQTT_PUBLISH:
#ifdef PICO_CYW43_SUPPORTED
                LOG_INFO("> MQTT %s = %s\n", action->data.mqtt.topic, action->data.mqtt.message);
                mqtt_publish_custom(action->data.mqtt.topic, action->data.mqtt.message);
#else
                LOG_WARNING("> MQTT action skipped (WiFi not supported)\n");
#endif
                break;

            case ACTION_DELAY:
                // Future implementation
                LOG_WARNING("> DELAY action not yet implemented\n");
                break;

            case ACTION_MOUSE_MOVE:
                // Future implementation
                LOG_WARNING("> MOUSE_MOVE action not yet implemented\n");
                break;

            default:
                LOG_ERROR("> Unknown action type: %d\n", action->type);
                break;
        }
    }

    // Free the loaded keydef
    free(def);
}


void print_keydefs() {
    // Simple version: just list keys stored in kvstore without loading/printing values
    // This avoids potential crashes from malformed keydefs

    printf("\n=== KVStore Contents ===\n");
    printf("Firmware: " GIT_COMMIT_HASH "\n");
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
    printf("Total: %d keys\n", count);
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

    // Print diagnostic counters
    printf("=== Diagnostic Counters ===\n");
    printf("Keystrokes received from physical keyboard: %lu\n", (unsigned long)keystrokes_received_from_physical);
    printf("Keystrokes sent to host computer: %lu\n", (unsigned long)keystrokes_sent_to_host);
    printf("Queue drops (realtime): %lu\n", (unsigned long)queue_drops_realtime);
    printf("Queue depths: keyboard_to_tud=%d, tud_to_host=%d\n",
           queue_get_level(&keyboard_to_tud_queue),
           queue_get_level(&tud_to_physical_host_queue));
    printf("USB HID ready: kbd=%s mouse=%s\n",
           tud_hid_n_ready(ITF_NUM_KEYBOARD) ? "yes" : "NO",
           tud_hid_n_ready(ITF_NUM_MOUSE) ? "yes" : "NO");
    printf("===========================\n\n");
}

void print_keydef(const keydef_t *def) {
    printf("%02x @%p: count = %d actions\n", def->trigger, def, def->count);
    for (int i = 0; i < def->count; i++) {
        printf("> %3d ", i);
        action_t *action = (action_t*)&def->actions[i];
        switch (action->type) {
            case ACTION_HID_REPORT:
                printf("HID: ");
                print_key_report(&action->data.hid);
                break;
            case ACTION_MQTT_PUBLISH:
                printf("MQTT: topic='%s' msg='%s'\n", action->data.mqtt.topic, action->data.mqtt.message);
                break;
            case ACTION_DELAY:
                printf("DELAY: %u ms\n", action->data.delay_ms);
                break;
            case ACTION_MOUSE_MOVE:
                printf("MOUSE_MOVE: (not yet implemented)\n");
                break;
            default:
                printf("UNKNOWN ACTION TYPE: %d\n", action->type);
                break;
        }
    }
    printf("--------------\n");
}

void print_key_report(const hid_keyboard_report_t *report) {
    printf("[%02x] %02x %02x ...\n", report->modifier, report->keycode[0], report->keycode[1]);
}
