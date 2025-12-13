#ifndef HID_PROXY_DIAGNOSTICS_H
#define HID_PROXY_DIAGNOSTICS_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/sync.h"
#include "tusb.h"

// Compile-time flag to enable diagnostic infrastructure
// To enable: build with -DENABLE_DIAGNOSTICS or set in CMakeLists.txt
#ifdef ENABLE_DIAGNOSTICS

// Diagnostic counters (lightweight, always enabled when ENABLE_DIAGNOSTICS is set)
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

// Initialize diagnostic system (call before launching Core 1)
void diagnostics_init(void);

// Add keystroke to diagnostic buffer
void diag_log_keystroke(diag_buffer_t *buffer, uint32_t sequence, const hid_keyboard_report_t *report);

// Dump diagnostic buffers to console (triggered by Double-shift+D)
void diag_dump_buffers(void);

#else  // ENABLE_DIAGNOSTICS not defined

// Stub implementations when diagnostics are disabled
static inline void diagnostics_init(void) {}
static inline void diag_log_keystroke(void *buffer, uint32_t sequence, const void *report) { (void)buffer; (void)sequence; (void)report; }
static inline void diag_dump_buffers(void) {}

// Counters still exist (minimal overhead) but buffers are just stubs
extern volatile uint32_t keystrokes_received_from_physical;
extern volatile uint32_t keystrokes_sent_to_host;
extern volatile uint32_t queue_drops_realtime;

// Dummy buffer types (zero-sized stubs to avoid compilation errors)
typedef struct { int dummy; } diag_buffer_stub_t;
extern diag_buffer_stub_t diag_received_buffer;
extern diag_buffer_stub_t diag_sent_buffer;

#endif  // ENABLE_DIAGNOSTICS

#endif  // HID_PROXY_DIAGNOSTICS_H
