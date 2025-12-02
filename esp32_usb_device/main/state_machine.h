/**
 * State Machine for HID Proxy
 *
 * Minimal Phase 1: State transitions and double-shift detection only
 * Future: Storage, macros, encryption
 */

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>
#include <stdbool.h>
#include "class/hid/hid.h"

// State machine states
typedef enum {
    blank = 0,                    // No password set yet
    blank_seen_magic,             // Both shifts pressed (blank state)
    locked,                       // Password set but not entered
    locked_seen_magic,            // Both shifts pressed (locked state)
    locked_expecting_command,     // Both shifts released (locked), waiting for command
    entering_password,            // User entering password to unlock
    normal,                       // Unlocked, normal operation
    seen_magic,                   // Both shifts pressed (normal state)
    expecting_command,            // Both shifts released (normal), waiting for command
    seen_assign,                  // '=' pressed, waiting for key to define
    defining,                     // Recording macro definition
    entering_new_password         // User entering new password
} status_t;

// Convert state to string for debugging
const char *status_string(status_t s);

// Global keyboard state
typedef struct {
    status_t status;
    // Future: keydef_t *key_being_defined;
    // Future: keydef_t *next_to_replay;
} kb_t;

extern kb_t kb;

/**
 * Main state machine entry point
 *
 * Processes incoming keyboard reports and handles state transitions
 * Replaces queue-based communication with direct USB device calls
 *
 * @param kb_report HID keyboard report from UART
 */
void handle_keyboard_report(hid_keyboard_report_t *kb_report);

/**
 * Initialize state machine
 */
void state_machine_init(void);

/**
 * Lock the device (clear password from memory)
 */
void lock(void);

/**
 * Unlock the device (set to normal state)
 */
void unlock(void);

#endif // STATE_MACHINE_H
