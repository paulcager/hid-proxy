#ifndef HID_PROXY_HID_PROXY_H
#define HID_PROXY_HID_PROXY_H

#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "hardware/flash.h"
#include "pico/sync.h"

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

// Action types for mixed macro sequences
typedef enum {
    ACTION_HID_REPORT = 0,   // Send keyboard HID report
    ACTION_MQTT_PUBLISH,     // Publish MQTT message
    ACTION_DELAY,            // Future: delay in milliseconds
    ACTION_MOUSE_MOVE,       // Future: mouse movement
} action_type_t;

// Action data union - supports HID, MQTT, and future action types
typedef struct {
    action_type_t type;
    union {
        hid_keyboard_report_t hid;     // ACTION_HID_REPORT
        struct {
            char topic[64];
            char message[64];
        } mqtt;                         // ACTION_MQTT_PUBLISH
        uint16_t delay_ms;              // ACTION_DELAY (future)
        // Future: mouse movement data
    } data;
} action_t;

// Unified keydef structure with mixed action types
typedef struct keydef {
    uint8_t trigger;           // HID keycode that triggers this macro (was 'keycode')
    uint16_t count;            // Number of actions in the sequence (was number of HID reports)
    bool require_unlock;       // Does this keydef require device unlock?
    action_t actions[0];       // Variable-length array of actions (was reports[])
} keydef_t;

/*
 * LEGACY/DEPRECATED: store_t is the old flash storage format
 * This struct is NO LONGER USED in production code - kvstore is used instead.
 * Kept only for compatibility with unit tests (test/test_macros.c).
 */
#define FLASH_STORE_MAGIC ("hidprox6")
typedef struct {
    char magic[8];
    uint8_t iv[16];
    char encrypted_magic[8];
    keydef_t keydefs[0];
} store_t;

typedef struct {
    status_t status;
    keydef_t *key_being_defined;
    uint8_t key_being_replayed;
    keydef_t *next_to_replay;
} kb_t;

extern kb_t kb;

extern queue_t keyboard_to_tud_queue;
extern queue_t tud_to_physical_host_queue;
extern queue_t leds_queue;

// Diagnostic counters for keystroke tracking
extern volatile uint32_t keystrokes_received_from_physical;  // Total reports from physical keyboard
extern volatile uint32_t keystrokes_sent_to_host;            // Total reports sent to host computer
extern volatile uint32_t queue_drops_realtime;               // Times we dropped oldest item in realtime queue

// Diagnostic cyclic buffer for keystroke history
#define DIAG_BUFFER_SIZE 256

typedef struct {
    uint32_t sequence;           // Monotonic sequence number
    uint32_t timestamp_us;       // Microseconds timestamp (wraps after ~71 minutes)
    uint8_t modifier;            // HID modifier byte
    uint8_t keycode[6];          // HID keycodes
} diag_keystroke_t;

typedef struct {
    diag_keystroke_t entries[DIAG_BUFFER_SIZE];
    volatile uint32_t head;      // Next write position
    volatile uint32_t count;     // Number of entries (up to DIAG_BUFFER_SIZE)
    spin_lock_t *lock;           // Protects concurrent access from both cores
} diag_buffer_t;

extern diag_buffer_t diag_received_buffer;  // Keystrokes received from physical keyboard
extern diag_buffer_t diag_sent_buffer;      // Keystrokes sent to host

// Add keystroke to diagnostic buffer
void diag_log_keystroke(diag_buffer_t *buffer, uint32_t sequence, const hid_keyboard_report_t *report);

// Dump diagnostic buffers to console
void diag_dump_buffers(void);

// LED control for visual status feedback (asymmetric on/off times)
extern uint32_t led_on_interval_ms;   // How long LED stays on (ms)
extern uint32_t led_off_interval_ms;  // How long LED stays off (ms)
extern void update_status_led(void);

extern void init_state(kb_t *kb);

// Queue management functions
extern void queue_add_with_backpressure(queue_t *q, const void *data);
extern void queue_add_realtime(queue_t *q, const void *data);

extern void print_keydef(const keydef_t *def);

extern void print_keydefs();

extern void print_key_report(const hid_keyboard_report_t *report);

extern void next_report(hid_report_t report);

extern void handle_keyboard_report(hid_keyboard_report_t *report);

extern void send_report_to_host(send_data_t to_send);

extern void hex_dump(void const *p, size_t len);

extern void lock();
extern void unlock();

/*! \brief Add HID report to host output queue (with backpressure)
 *
 * This is primarily used for macro playback, so it uses backpressure
 * to ensure all keystrokes are sent without data loss.
 *
 * For real-time passthrough, use add_to_host_queue_realtime() instead
 * to avoid blocking on queue full conditions.
 */
inline void add_to_host_queue(uint8_t instance, uint8_t report_id, uint16_t len, void *data) {
    send_data_t item = {.instance = instance, .report_id = report_id, .len = len};

    if (len > sizeof(item.data)) {
        panic("Asked to send %d bytes of data", len);
    }

    memcpy(item.data, data, sizeof(item.data));
    queue_add_with_backpressure(&tud_to_physical_host_queue, &item);
}

/*! \brief Add HID report to host output queue (realtime, non-blocking)
 *
 * Used for real-time keyboard/mouse passthrough where blocking is unacceptable.
 * If queue is full, drops oldest item to make room (with warning log).
 */
inline void add_to_host_queue_realtime(uint8_t instance, uint8_t report_id, uint16_t len, void *data) {
    send_data_t item = {.instance = instance, .report_id = report_id, .len = len};

    if (len > sizeof(item.data)) {
        panic("Asked to send %d bytes of data", len);
    }

    memcpy(item.data, data, sizeof(item.data));
    queue_add_realtime(&tud_to_physical_host_queue, &item);
}

#endif //HID_PROXY_HID_PROXY_H
