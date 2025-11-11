#ifndef HID_PROXY_HID_PROXY_H
#define HID_PROXY_HID_PROXY_H

#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "hardware/flash.h"

#include "tusb.h"
#include "logging.h"

// The amount of flash available to us to save data.
#define FLASH_STORE_SIZE ((size_t)(__flash_storage_end - __flash_storage_start))

extern uint8_t __flash_storage_start[];
extern uint8_t __flash_storage_end[];

#define FLASH_STORE_OFFSET (__flash_storage_start - (uint8_t*)XIP_BASE)
#define FLASH_STORE_ADDRESS ((void*)__flash_storage_start)

// The number of milliseconds without any keyboard input after which we'll
// clear the plain-text storage, requiring re-input of the passphrase.
#define IDLE_TIMEOUT_MILLIS ((int64_t)(120 * 60 * 1000))

union hid_reports {
    hid_keyboard_report_t kb;
    hid_mouse_report_t mouse;
    hid_gamepad_report_t game;
    uint8_t bytes[0];
};

typedef struct {
    uint8_t dev_addr;
    uint8_t instance;
    uint16_t len;
    union hid_reports data;
} hid_report_t;

typedef struct {
    uint8_t instance;
    uint8_t report_id;
    uint16_t len;
    uint8_t data[sizeof(union hid_reports)];
} send_data_t;

typedef enum {
    blank = 0,
    blank_seen_magic,
    locked,
    locked_seen_magic,
    locked_expecting_command,
    entering_password,
    normal,
    seen_magic,
    expecting_command,
    seen_assign,
    defining,
    entering_new_password
} status_t;

inline const char *status_string(status_t s) {
    switch (s) {
        case blank:
            return "blank";
        case blank_seen_magic:
            return "blank_seen_magic";
        case locked:
            return "locked";
        case locked_seen_magic:
            return "locked_seen_magic";
        case locked_expecting_command:
            return "locked_expecting_command";
        case entering_password:
            return "entering_password";
        case normal:
            return "normal";
        case seen_magic:
            return "seen_magic";
        case expecting_command:
            return "expecting_command";
        case seen_assign:
            return "seen_assign";
        case defining:
            return "defining";
        case entering_new_password:
            return "entering_new_password";
        default :
            return "unknown";
    }
}

// Unified keydef structure (used by both kvstore and legacy flash parsing)
typedef struct keydef {
    uint8_t trigger;           // HID keycode that triggers this macro (was 'keycode')
    uint16_t count;            // Number of HID reports in the sequence (was 'used')
    bool require_unlock;       // Does this keydef require device unlock? (Phase 4)
    hid_keyboard_report_t reports[0];  // Variable-length array of HID reports
} keydef_t;

#define FLASH_STORE_MAGIC ("hidprox6")
typedef struct {
    char magic[8];
    uint8_t iv[16];
    char encrypted_magic[8];
    keydef_t keydefs[0];
} store_t;

typedef struct {
    status_t status;
    store_t *local_store;
    keydef_t *key_being_defined;
    uint8_t key_being_replayed;
    keydef_t *next_to_replay;
    bool send_to_host_in_progress;
} kb_t;

extern kb_t kb;

extern queue_t keyboard_to_tud_queue;
extern queue_t tud_to_physical_host_queue;
extern queue_t leds_queue;

// Synchronization flag: Core 1 waits for this before starting USB host stack
// This prevents flash access conflicts during kvstore initialization
extern volatile bool kvstore_init_complete;

extern void init_state(kb_t *kb);

extern void save_state(kb_t *kb);

extern void read_state(kb_t *kb);

extern void queue_add_or_panic(queue_t *q, const void *data);

extern void print_keydef(const keydef_t *def);

extern void print_keydefs();

extern void print_key_report(const hid_keyboard_report_t *report);

extern void next_report(hid_report_t report);

extern void handle_keyboard_report(hid_keyboard_report_t *report);

extern void send_report_to_host(send_data_t to_send);

extern void hex_dump(void const *p, size_t len);

extern void lock();

inline void add_to_host_queue(uint8_t instance, uint8_t report_id, uint16_t len, void *data) {
    send_data_t item = {.instance = instance, .report_id = report_id, .len = len};

    if (len > sizeof(item.data)) {
        panic("Asked to send %d bytes of data", len);
    }

    memcpy(item.data, data, sizeof(item.data));
    queue_add_or_panic(&tud_to_physical_host_queue, &item);
}

#ifdef NDEBUG
# define assert_sane(__kb) ((void)0)
#else
# define assert_sane(__kb) assert_sane_func(__FILE__, __LINE__, __kb)

void assert_sane_func(char *file, int line, kb_t *k);

#endif

#endif //HID_PROXY_HID_PROXY_H
