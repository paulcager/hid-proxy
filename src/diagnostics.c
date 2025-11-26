//
// Diagnostic system for tracking keystroke history and debugging USB issues
//
// This file is only compiled when ENABLE_DIAGNOSTICS is defined.
// Memory cost: ~16KB RAM (2 × 256 × 32 bytes for cyclic buffers)
//

#include "diagnostics.h"

#ifdef ENABLE_DIAGNOSTICS

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "macros.h"

// Diagnostic counters for keystroke tracking
volatile uint32_t keystrokes_received_from_physical = 0;
volatile uint32_t keystrokes_sent_to_host = 0;
volatile uint32_t queue_drops_realtime = 0;

// Diagnostic cyclic buffers for keystroke history
diag_buffer_t diag_received_buffer = {0};
diag_buffer_t diag_sent_buffer = {0};

void diagnostics_init(void) {
    // Initialize diagnostic buffer spin locks (must be before Core 1 launch)
    diag_received_buffer.lock = spin_lock_init(spin_lock_claim_unused(true));
    diag_sent_buffer.lock = spin_lock_init(spin_lock_claim_unused(true));
    printf("Diagnostic system initialized (16KB RAM allocated)\n");
}

void diag_log_keystroke(diag_buffer_t *buffer, uint32_t sequence, const hid_keyboard_report_t *report) {
    // Acquire spin lock to protect against concurrent access from other core
    uint32_t save = spin_lock_blocking(buffer->lock);

    uint32_t pos = buffer->head;
    diag_keystroke_t *entry = &buffer->entries[pos];

    entry->sequence = sequence;
    entry->timestamp_us = (uint32_t)to_us_since_boot(get_absolute_time());
    entry->modifier = report->modifier;
    memcpy(entry->keycode, report->keycode, 6);

    // Advance head pointer (circular)
    buffer->head = (pos + 1) % DIAG_BUFFER_SIZE;

    // Update count (saturates at buffer size)
    if (buffer->count < DIAG_BUFFER_SIZE) {
        buffer->count++;
    }

    // Release spin lock
    spin_unlock(buffer->lock, save);
}

// Format a keystroke into human-readable form
static void format_keystroke(char *buf, size_t bufsize, uint8_t modifier, const uint8_t keycode[6]) {
    buf[0] = '\0';

    // Handle no keys pressed
    bool has_keys = false;
    for (int i = 0; i < 6; i++) {
        if (keycode[i] != 0) {
            has_keys = true;
            break;
        }
    }

    if (!has_keys && modifier == 0) {
        snprintf(buf, bufsize, "(none)");
        return;
    }

    // Build modifier prefix
    char mod_str[20] = "";
    if (modifier & 0x01) strcat(mod_str, "Ctrl+");       // Left Ctrl
    if (modifier & 0x02) strcat(mod_str, "Shift+");      // Left Shift
    if (modifier & 0x04) strcat(mod_str, "Alt+");        // Left Alt
    if (modifier & 0x08) strcat(mod_str, "GUI+");        // Left GUI
    if (modifier & 0x10) strcat(mod_str, "RCtrl+");      // Right Ctrl
    if (modifier & 0x20) strcat(mod_str, "RShift+");     // Right Shift
    if (modifier & 0x40) strcat(mod_str, "RAlt+");       // Right Alt
    if (modifier & 0x80) strcat(mod_str, "RGUI+");       // Right GUI

    strncat(buf, mod_str, bufsize - strlen(buf) - 1);

    // Format each key
    for (int i = 0; i < 6 && keycode[i] != 0; i++) {
        if (i > 0) {
            strncat(buf, "+", bufsize - strlen(buf) - 1);
        }

        // Try ASCII first (without shift, since we already showed Shift+ in modifier)
        char ascii = keycode_to_ascii(keycode[i], 0);
        if (ascii >= 32 && ascii < 127) {
            char temp[2] = {ascii, '\0'};
            strncat(buf, temp, bufsize - strlen(buf) - 1);
            continue;
        }

        // Try mnemonic
        const char *mnemonic = keycode_to_mnemonic(keycode[i]);
        if (mnemonic) {
            strncat(buf, mnemonic, bufsize - strlen(buf) - 1);
            continue;
        }

        // Fall back to hex
        char hex[8];
        snprintf(hex, sizeof(hex), "0x%02x", keycode[i]);
        strncat(buf, hex, bufsize - strlen(buf) - 1);
    }

    // If only modifiers, remove trailing '+'
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '+') {
        buf[len - 1] = '\0';
    }
}

void diag_dump_buffers(void) {
    printf("\n");
    printf("================================================================================\n");
    printf("DIAGNOSTIC KEYSTROKE HISTORY\n");
    printf("================================================================================\n");
    printf("Total received: %lu, Total sent: %lu, Drops: %lu\n",
           (unsigned long)keystrokes_received_from_physical,
           (unsigned long)keystrokes_sent_to_host,
           (unsigned long)queue_drops_realtime);
    printf("\n");

    // Snapshot buffer metadata under lock to get consistent view
    uint32_t save_recv = spin_lock_blocking(diag_received_buffer.lock);
    uint32_t recv_count = diag_received_buffer.count;
    uint32_t recv_head = diag_received_buffer.head;
    spin_unlock(diag_received_buffer.lock, save_recv);

    uint32_t save_sent = spin_lock_blocking(diag_sent_buffer.lock);
    uint32_t sent_count = diag_sent_buffer.count;
    uint32_t sent_head = diag_sent_buffer.head;
    spin_unlock(diag_sent_buffer.lock, save_sent);

    // Determine which buffer has more entries to know how many rows to print
    uint32_t max_count = recv_count > sent_count ? recv_count : sent_count;

    if (max_count == 0) {
        printf("No keystroke data captured yet.\n");
        printf("================================================================================\n\n");
        return;
    }

    printf("Showing last %lu keystrokes (buffer holds %d max)\n\n", (unsigned long)max_count, DIAG_BUFFER_SIZE);

    // Print header
    printf("%-70s | %-70s\n", "RECEIVED FROM KEYBOARD", "SENT TO HOST");
    printf("%-70s-+-%-70s\n",
           "----------------------------------------------------------------------",
           "----------------------------------------------------------------------");

    // Calculate starting positions for both buffers (using snapshot values)
    uint32_t recv_start = (recv_head + DIAG_BUFFER_SIZE - recv_count) % DIAG_BUFFER_SIZE;
    uint32_t sent_start = (sent_head + DIAG_BUFFER_SIZE - sent_count) % DIAG_BUFFER_SIZE;

    // Print entries side by side
    for (uint32_t i = 0; i < max_count; i++) {
        char recv_line[80] = "";
        char sent_line[80] = "";

        // Format received entry if available
        if (i < recv_count) {
            uint32_t pos = (recv_start + i) % DIAG_BUFFER_SIZE;

            // Take snapshot of entry under lock to avoid torn reads
            uint32_t save = spin_lock_blocking(diag_received_buffer.lock);
            diag_keystroke_t entry_copy = diag_received_buffer.entries[pos];
            spin_unlock(diag_received_buffer.lock, save);

            // Format keystroke into human-readable form
            char keys[24];
            format_keystroke(keys, sizeof(keys), entry_copy.modifier, entry_copy.keycode);

            // Format with raw hex bytes and human-readable
            snprintf(recv_line, sizeof(recv_line), "#%-5lu [%02x:%02x:%02x:%02x:%02x:%02x:%02x] %-12s",
                     (unsigned long)entry_copy.sequence,
                     entry_copy.modifier,
                     entry_copy.keycode[0], entry_copy.keycode[1], entry_copy.keycode[2],
                     entry_copy.keycode[3], entry_copy.keycode[4], entry_copy.keycode[5],
                     keys);
        }

        // Format sent entry if available
        if (i < sent_count) {
            uint32_t pos = (sent_start + i) % DIAG_BUFFER_SIZE;

            // Take snapshot of entry under lock to avoid torn reads
            uint32_t save = spin_lock_blocking(diag_sent_buffer.lock);
            diag_keystroke_t entry_copy = diag_sent_buffer.entries[pos];
            spin_unlock(diag_sent_buffer.lock, save);

            // Format keystroke into human-readable form
            char keys[24];
            format_keystroke(keys, sizeof(keys), entry_copy.modifier, entry_copy.keycode);

            // Format with raw hex bytes and human-readable
            snprintf(sent_line, sizeof(sent_line), "#%-5lu [%02x:%02x:%02x:%02x:%02x:%02x:%02x] %-12s",
                     (unsigned long)entry_copy.sequence,
                     entry_copy.modifier,
                     entry_copy.keycode[0], entry_copy.keycode[1], entry_copy.keycode[2],
                     entry_copy.keycode[3], entry_copy.keycode[4], entry_copy.keycode[5],
                     keys);
        }

        printf("%-70s | %-70s\n", recv_line, sent_line);
    }

    printf("================================================================================\n\n");

    // Analysis: Look for sequence number gaps
    printf("ANALYSIS:\n");

    // Check for missing sequence numbers in sent buffer
    uint32_t missing_count = 0;
    if (sent_count > 1) {
        for (uint32_t i = 1; i < sent_count; i++) {
            uint32_t prev_pos = (sent_start + i - 1) % DIAG_BUFFER_SIZE;
            uint32_t curr_pos = (sent_start + i) % DIAG_BUFFER_SIZE;

            // Read sequence numbers under lock
            uint32_t save = spin_lock_blocking(diag_sent_buffer.lock);
            uint32_t prev_seq = diag_sent_buffer.entries[prev_pos].sequence;
            uint32_t curr_seq = diag_sent_buffer.entries[curr_pos].sequence;
            spin_unlock(diag_sent_buffer.lock, save);

            if (curr_seq > prev_seq + 1) {
                uint32_t gap = curr_seq - prev_seq - 1;
                missing_count += gap;
                printf("  Gap detected: %lu keystroke(s) missing between seq #%lu and #%lu\n",
                       (unsigned long)gap,
                       (unsigned long)prev_seq,
                       (unsigned long)curr_seq);
            }
        }
    }

    if (missing_count == 0) {
        printf("  No gaps detected in sequence numbers (within buffer window)\n");
    } else {
        printf("  Total missing: %lu keystroke(s)\n", (unsigned long)missing_count);
    }

    printf("================================================================================\n\n");
}

#else  // ENABLE_DIAGNOSTICS not defined

// Minimal stub implementations when diagnostics are disabled
// Counters still exist to avoid breaking existing code
volatile uint32_t keystrokes_received_from_physical = 0;
volatile uint32_t keystrokes_sent_to_host = 0;
volatile uint32_t queue_drops_realtime = 0;

// Dummy buffers (zero-sized) to avoid compilation errors
// The stub inline functions in diagnostics.h won't actually use these
// Type is defined in diagnostics.h
diag_buffer_stub_t diag_received_buffer;
diag_buffer_stub_t diag_sent_buffer;

#endif  // ENABLE_DIAGNOSTICS
