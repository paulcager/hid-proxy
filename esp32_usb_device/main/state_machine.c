/**
 * State Machine Implementation (Phase 1: Minimal)
 *
 * Phase 1: State transitions, double-shift detection, in-memory password
 * Phase 2: NVS storage, password validation
 * Phase 3: Macro expansion
 */

#include "state_machine.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"
#include <string.h>

static const char *TAG = "state_machine";

// Global state
kb_t kb = {
    .status = blank,
};

// In-memory password (Phase 1 - no storage yet)
#define MAX_PASSWORD_LEN 32
static uint8_t password_buffer[MAX_PASSWORD_LEN];
static uint8_t password_len = 0;
static bool password_is_set = false;
static uint8_t stored_password[MAX_PASSWORD_LEN];
static uint8_t stored_password_len = 0;

// HID key codes (from USB HID spec)
#define HID_KEY_ESCAPE       0x29
#define HID_KEY_ENTER        0x28
#define HID_KEY_INSERT       0x49
#define HID_KEY_DELETE       0x4C
#define HID_KEY_HOME         0x4A
#define HID_KEY_END          0x4D
#define HID_KEY_EQUAL        0x2E
#define HID_KEY_SPACE        0x2C
#define HID_KEY_F12          0x45
#define HID_KEY_PRINT_SCREEN 0x46

// Helper: Send keyboard report to USB device
static void send_to_usb(hid_keyboard_report_t *report) {
    if (tud_hid_n_ready(0)) {  // Interface 0 = keyboard
        tud_hid_n_keyboard_report(0, 0, report->modifier, &report->keycode[0]);
    } else {
        ESP_LOGW(TAG, "USB not ready, dropping keystroke");
    }
}

// Helper: Send all-keys-released report
static void send_release_all() {
    hid_keyboard_report_t release = {0, 0, {0, 0, 0, 0, 0, 0}};
    send_to_usb(&release);
}

// Initialize state machine
void state_machine_init(void) {
    kb.status = blank;
    password_len = 0;
    password_is_set = false;
    ESP_LOGI(TAG, "State machine initialized: status=%s", status_string(kb.status));
}

// Lock device
void lock(void) {
    kb.status = locked;
    ESP_LOGI(TAG, "Device locked");
    // TODO: Publish MQTT lock event
}

// Unlock device
void unlock(void) {
    kb.status = normal;
    ESP_LOGI(TAG, "Device unlocked");
    // TODO: Publish MQTT unlock event
}

// Convert status to string
const char *status_string(status_t s) {
    switch (s) {
        case blank: return "blank";
        case blank_seen_magic: return "blank_seen_magic";
        case locked: return "locked";
        case locked_seen_magic: return "locked_seen_magic";
        case locked_expecting_command: return "locked_expecting_command";
        case entering_password: return "entering_password";
        case normal: return "normal";
        case seen_magic: return "seen_magic";
        case expecting_command: return "expecting_command";
        case seen_assign: return "seen_assign";
        case defining: return "defining";
        case entering_new_password: return "entering_new_password";
        default: return "unknown";
    }
}

// Main state machine
void handle_keyboard_report(hid_keyboard_report_t *kb_report) {
    uint8_t key0 = kb_report->keycode[0];

    // Debug logging
    ESP_LOGD(TAG, "State=%s, mod=0x%02X, key0=0x%02X",
             status_string(kb.status), kb_report->modifier, key0);

    // Global: Double-shift + HOME = reboot (ESP32 version: just log for now)
    if (kb_report->modifier == 0x22 && key0 == HID_KEY_HOME) {
        ESP_LOGI(TAG, "Double-shift + HOME detected (reboot not implemented yet)");
        // TODO: esp_restart();
        return;
    }

    switch (kb.status) {
        // ========== BLANK STATE (no password set) ==========
        case blank:
            if (kb_report->modifier == 0x22 && key0 == 0) {
                // Both shifts, no other keys
                kb.status = blank_seen_magic;
                ESP_LOGI(TAG, "-> blank_seen_magic");
            } else {
                // Pass through
                send_to_usb(kb_report);
            }
            return;

        case blank_seen_magic:
            switch (key0) {
                case 0:  // Keys still held, wait for release
                    return;

                case HID_KEY_ESCAPE:
                    kb.status = blank;
                    ESP_LOGI(TAG, "-> blank (cancelled)");
                    return;

                case HID_KEY_INSERT:
                    // Set first password
                    kb.status = entering_new_password;
                    password_len = 0;
                    ESP_LOGI(TAG, "-> entering_new_password (first password)");
                    ESP_LOGI(TAG, "Enter new password (ENTER to finish)");
                    return;

                case HID_KEY_DELETE:
                    // Already blank, just stay blank
                    kb.status = blank;
                    ESP_LOGI(TAG, "-> blank (already blank)");
                    return;

                default:
                    // Any other key returns to blank and forwards
                    kb.status = blank;
                    ESP_LOGI(TAG, "-> blank (other key)");
                    send_to_usb(kb_report);
                    return;
            }

        // ========== LOCKED STATE (password set but not entered) ==========
        case locked:
            if (kb_report->modifier == 0x22 && key0 == 0) {
                kb.status = locked_seen_magic;
                ESP_LOGI(TAG, "-> locked_seen_magic");
            } else {
                // Pass through (allow typing when locked)
                send_to_usb(kb_report);
            }
            return;

        case locked_seen_magic:
            // Wait for keys to be released
            if (kb_report->modifier == 0x00 && key0 == 0) {
                kb.status = locked_expecting_command;
                ESP_LOGI(TAG, "-> locked_expecting_command");
            }
            return;

        case locked_expecting_command:
            switch (key0) {
                case 0:  // Key released, waiting
                    return;

                case HID_KEY_ESCAPE:
                    kb.status = locked;
                    ESP_LOGI(TAG, "-> locked (cancelled)");
                    return;

                case HID_KEY_ENTER:
                    // Unlock with password
                    kb.status = entering_password;
                    password_len = 0;
                    ESP_LOGI(TAG, "-> entering_password");
                    ESP_LOGI(TAG, "Enter password (ENTER to finish)");
                    return;

                case HID_KEY_INSERT:
                    // Change password (re-encrypt in future)
                    kb.status = entering_new_password;
                    password_len = 0;
                    ESP_LOGI(TAG, "-> entering_new_password (change password)");
                    ESP_LOGI(TAG, "Enter new password (ENTER to finish)");
                    return;

                case HID_KEY_DELETE:
                    // Erase everything
                    state_machine_init();  // Back to blank
                    password_is_set = false;
                    ESP_LOGI(TAG, "-> blank (erased)");
                    return;

                default:
                    // Try public keydefs (future)
                    kb.status = locked;
                    ESP_LOGI(TAG, "-> locked (no public keydef yet)");
                    return;
            }

        // ========== PASSWORD ENTRY ==========
        case entering_password:
        case entering_new_password:
            if (key0 == 0) {
                // Ignore key releases
                return;
            }

            if (key0 != HID_KEY_ENTER) {
                // Add key to password buffer
                if (password_len < MAX_PASSWORD_LEN) {
                    password_buffer[password_len++] = key0;
                    ESP_LOGD(TAG, "Password char %d added", password_len);
                }
                return;
            }

            // ENTER pressed - finish password entry
            if (kb.status == entering_new_password) {
                // Setting new password
                memcpy(stored_password, password_buffer, password_len);
                stored_password_len = password_len;
                password_is_set = true;
                ESP_LOGI(TAG, "Password set (%d chars)", password_len);

                // TODO: Save password hash to NVS
                unlock();
            } else {
                // Checking existing password
                if (password_len == stored_password_len &&
                    memcmp(password_buffer, stored_password, password_len) == 0) {
                    ESP_LOGI(TAG, "Password correct");
                    unlock();
                } else {
                    ESP_LOGI(TAG, "Password incorrect");
                    kb.status = locked;
                }
            }

            // Clear password buffer
            memset(password_buffer, 0, sizeof(password_buffer));
            password_len = 0;
            return;

        // ========== NORMAL STATE (unlocked) ==========
        case normal:
            if (kb_report->modifier == 0x22 && key0 == 0) {
                kb.status = seen_magic;
                ESP_LOGI(TAG, "-> seen_magic");
            } else {
                // Pass through
                send_to_usb(kb_report);
            }
            return;

        case seen_magic:
            // Wait for keys to be released
            if (kb_report->modifier == 0x00 && key0 == 0) {
                kb.status = expecting_command;
                ESP_LOGI(TAG, "-> expecting_command");
            }
            return;

        case expecting_command:
            switch (key0) {
                case 0:  // Key released, waiting
                    return;

                case HID_KEY_ESCAPE:
                    unlock();  // Back to normal
                    ESP_LOGI(TAG, "-> normal (cancelled)");
                    return;

                case HID_KEY_EQUAL:
                    // Start defining macro
                    kb.status = seen_assign;
                    ESP_LOGI(TAG, "-> seen_assign (macro definition not implemented yet)");
                    // TODO: kb.status = seen_assign;
                    unlock();  // For now, just return to normal
                    return;

                case HID_KEY_SPACE:
                    // Print keydefs / enable web access
                    ESP_LOGI(TAG, "Print keydefs (not implemented yet)");
                    // TODO: print_keydefs();
                    // TODO: web_access_enable();
                    unlock();
                    return;

                case HID_KEY_INSERT:
                    // Change password
                    kb.status = entering_new_password;
                    password_len = 0;
                    ESP_LOGI(TAG, "-> entering_new_password");
                    ESP_LOGI(TAG, "Enter new password (ENTER to finish)");
                    return;

                case HID_KEY_DELETE:
                    // Erase everything
                    state_machine_init();
                    password_is_set = false;
                    ESP_LOGI(TAG, "-> blank (erased)");
                    return;

                case HID_KEY_END:
                    // Lock device
                    lock();
                    return;

                case HID_KEY_F12:
                    // WiFi config (future)
                    ESP_LOGI(TAG, "WiFi config (not implemented yet)");
                    unlock();
                    return;

                case HID_KEY_PRINT_SCREEN:
                    // NFC write (future)
                    ESP_LOGI(TAG, "NFC write (not implemented yet)");
                    unlock();
                    return;

                default:
                    // Try to evaluate keydef (future)
                    ESP_LOGI(TAG, "Keydef 0x%02X (not implemented yet)", key0);
                    unlock();
                    return;
            }

        // ========== MACRO DEFINITION (future) ==========
        case seen_assign:
        case defining:
            // TODO: Macro definition
            ESP_LOGI(TAG, "Macro definition not implemented yet");
            unlock();
            return;
    }
}
