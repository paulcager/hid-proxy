#include <pico/multicore.h>
#include <pico/bootrom.h>
#include "hid_proxy.h"
#include"usb_descriptors.h"
#include "encryption.h"
#include "nfc_tag.h"

static hid_keyboard_report_t release_all_keys = {0, 0, {0, 0, 0, 0, 0, 0}};

void evaluate_keydef(hid_keyboard_report_t *report, uint8_t key0);
char keycode_to_letter_or_digit(uint8_t keycode);

void start_define(uint8_t key0);

static inline keydef_t *next_keydef(keydef_t *this) {
    void *t = this;
    t += sizeof(keydef_t) + (this->used * sizeof(hid_keyboard_report_t));
    LOG_TRACE("Next(%p) -> %p\n", this, t);
    return t;
}


void handle_keyboard_report(hid_keyboard_report_t *kb_report) {
#ifdef DEBUG
    print_key_report(kb_report);
#endif

    uint8_t key0 = kb_report->keycode[0];

    // No matter what state we are in "double-shift + Pause" means reboot into upload mode.
    if (kb_report->modifier == 0x22 && key0 == HID_KEY_PAUSE) {
        multicore_reset_core1();
        reset_usb_boot(0,0);
        return;
    }

    switch (kb.status) {

        case locked:
            if (kb_report->modifier == 0x22 && key0 == 0) {
                kb.status = locked_seen_magic;
            } else {
                add_to_host_queue(0, REPORT_ID_KEYBOARD, sizeof(hid_keyboard_report_t), kb_report);
            }

            return;

        case locked_seen_magic:
            switch (key0) {
                case 0:
                    return;

                case HID_KEY_ESCAPE:
                    lock();
                    return;

                case HID_KEY_ENTER:
                    kb.status = entering_password;
                    enc_start_key_derivation();
                    printf("Enter password\n");
                    return;

                case HID_KEY_DELETE:
                    init_state(&kb);
                    return;

                default:
                    kb.status = locked;
                    add_to_host_queue(0, REPORT_ID_KEYBOARD, sizeof(hid_keyboard_report_t), kb_report);
                    return;
            }

        case entering_password:
        case entering_new_password:
            if (key0 == 0) {
                // Ignore keyups.
                return;
            }

            if (key0 != HID_KEY_ENTER) {
                enc_add_key_derivation_byte(key0);
            } else {
                enc_end_key_derivation();
                if (kb.status == entering_password) {
                    read_state(&kb);
                } else {
                    // Changing password.
                    save_state(&kb);
                    kb.status = normal;
                }
            }

            return;

        case normal:
            if (kb_report->modifier == 0x22 && key0 == 0) {
                kb.status = seen_magic;
            } else {
                LOG_TRACE("Adding to host Q: instance=%d, report_id=%d, len=%d\n", 0, REPORT_ID_KEYBOARD, sizeof(hid_keyboard_report_t));
                add_to_host_queue(0, REPORT_ID_KEYBOARD, sizeof(hid_keyboard_report_t), kb_report);
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
                {
                    uint8_t key[16];
                    enc_get_key(key, sizeof(key));
                    nfc_write_key(key, sizeof(key), 30 * 1000);
                    kb.status = normal;
                    return;
                }

                case HID_KEY_ESCAPE:
                    kb.status = normal;
                    return;

                case HID_KEY_EQUAL:
                    kb.status = seen_assign;
                    return;

                case HID_KEY_SPACE:
                    kb.status = normal;
                    print_keydefs();
                    return;

                case HID_KEY_ENTER:
                    kb.status = entering_new_password;
                    enc_start_key_derivation();
                    printf("Enter password\n");
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
                save_state(&kb);
                LOG_INFO("Setn status = normal\n");
                kb.status = normal;
                kb.key_being_defined = NULL;
                return;
            }

            keydef_t *this_def = kb.key_being_defined;

            // TODO - check remaining space

            this_def->reports[this_def->used] = *kb_report;
            this_def->used++;
            print_keydef(this_def);
    }
}

void start_define(uint8_t key0) {
    LOG_INFO("Defining keycode %02x\n", key0);

    kb.key_being_defined = NULL;

    void *ptr = kb.local_store->keydefs;
    void *limit = ptr + FLASH_STORE_SIZE;

    while(true) {
        keydef_t *def = ptr;
        if (ptr >= limit) {
            break;
        }

        if (def->keycode == 0) {
            // We've found the end of the list.
            kb.key_being_defined = def;
            break;
        }

        void *next = ptr + sizeof(keydef_t) + (def->used * sizeof(hid_keyboard_report_t));

        if (def->keycode == key0) {
            // We are replacing a definition. Shuffle down the buffer and let's not worry about efficiency.
            memmove(ptr, next, limit - next);
            continue;
        }

        ptr = next;
    }

    if (kb.key_being_defined == NULL) {
        panic("No space left");
    }

    kb.key_being_defined->keycode = key0;
    kb.key_being_defined->used = 0;

    // If def already existed, delete it.

    kb.status = defining;
    LOG_INFO("Defining keycode %d\n", key0);
    LOG_DEBUG("New def for %x is at %p\n", key0, kb.key_being_defined);
}

void evaluate_keydef(hid_keyboard_report_t *report, uint8_t key0) {
    keydef_t *def = NULL;
    for (keydef_t *ptr = kb.local_store->keydefs; ptr->keycode != 0; ptr = next_keydef(ptr)) {
        if (ptr->keycode == key0) {
            def = ptr;
            break;
        }
    }

    if (def == NULL) {
        LOG_INFO("No sequence defined for keycode %x\n", key0);
        add_to_host_queue(0, REPORT_ID_KEYBOARD, sizeof(hid_keyboard_report_t), report);
        add_to_host_queue(0, REPORT_ID_KEYBOARD, sizeof(hid_keyboard_report_t),&release_all_keys);
        return;
    }

    LOG_INFO("Executing keycode %x with %d sequences\n", key0, def->used);

    // TODO
    add_to_host_queue(0, REPORT_ID_KEYBOARD, sizeof(hid_keyboard_report_t),&release_all_keys);
    for (int i = 0; i < def->used; i++) {
        hid_keyboard_report_t next_report = def->reports[i];
        LOG_TRACE("> %x %x\n", next_report.modifier, next_report.keycode[0]);
        add_to_host_queue(0, REPORT_ID_KEYBOARD, sizeof(hid_keyboard_report_t),&next_report);
    }
}


void print_keydefs() {
    int count = 0;
    for (keydef_t *ptr = kb.local_store->keydefs; ptr->keycode != 0; ptr = next_keydef(ptr)) {
        print_keydef(ptr);
        count++;
    }
    printf("There are %d keydefs\n", count);
}

void print_keydef(const keydef_t *def) {
    printf("%02x @%p: used = %d\n", def->keycode, def, def->used);
    for (int i = 0; i < def->used; i++) {
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