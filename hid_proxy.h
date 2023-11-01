#ifndef HID_PROXY_HID_PROXY_H
#define HID_PROXY_HID_PROXY_H

#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "hardware/flash.h"

#include "tusb.h"
#include "logging.h"

// The amount of flash available to us to save data.
#define FLASH_STORE_SIZE (FLASH_SECTOR_SIZE)

// The offset within the flash we will use to store data.
// TODO - maybe choose flashEnd-size?
#define FLASH_STORE_OFFSET (512 * 1024)

#define FLASH_STORE_ADDRESS ((void*)(XIP_BASE + FLASH_STORE_OFFSET))

// The number of seconds without any keyboard input after which we'll
// clear the plain-text storage, requiring re-input of the passphrase.
#define IDLE_TIMEOUT_MILLIS (30 * 60 * 1000)

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
    locked = 0,
    locked_seen_magic,
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
        case locked:
            return "locked";
        case locked_seen_magic:
            return "locked_seen_magic";
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

typedef struct keydef {
    int keycode;
    int used;
    hid_keyboard_report_t reports[0];
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
    // TODO - overflow.
    // TODO - will be replaced by 16 bytes of pure entropy when link to card.
    uint8_t encryption_key[16];
    uint8_t encryption_key_len;
    keydef_t *key_being_defined;
    uint8_t key_being_replayed;
    keydef_t *next_to_replay;
    bool send_to_host_in_progress;
} kb_t;

extern kb_t kb;

extern queue_t keyboard_to_tud_queue;
extern queue_t tud_to_physical_host_queue;
extern queue_t leds_queue;

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
