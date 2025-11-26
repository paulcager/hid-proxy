/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *                    sekigon-gonnoc
 * Copyright (c) 2023 paul.cager@gmail.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>

#include "pico.h"
#include <pico/flash.h>
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "hardware/gpio.h"

#include "pio_usb.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include "hid_proxy.h"
#ifdef ENABLE_NFC
#include "nfc_tag.h"
#endif
#include "encryption.h"
#include "usb_host.h"
#include "kvstore_init.h"
#include "keydef_store.h"
#include "macros.h"

#ifdef PICO_CYW43_SUPPORTED
#include "pico/cyw43_arch.h"
#include "wifi_config.h"
#include "http_server.h"
#include "mqtt_client.h"
#endif

#ifdef ENABLE_USB_STDIO
#include "cdc_stdio_lib.h"
#endif

#ifdef BOARD_WS_2350
#include "ws2812_led.h"
#endif

// Reminders:
// Latest is ~/pico/hid-proxy2/build
// make && openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program $(ls *.elf) verify reset exit"
// minicom -b 115200 -o -D /dev/ttyACM0
// UART0 on standard ports and PIO-USB on GPIO2/3 (see CMakeLists.txt to change).


kb_t kb;

// A queue of events (hid_report_t) from the physical keyboard (PIO) to be processed
// in the device CPU.
// Elements: hid_report_t;
queue_t keyboard_to_tud_queue;

// A queue of events from the tud process to the real host.
// Elements: send_data_t
queue_t tud_to_physical_host_queue;

// A queue of events from the physical host, to be sent to the physical keyboard.
queue_t leds_queue;

// Diagnostic counters for keystroke tracking
volatile uint32_t keystrokes_received_from_physical = 0;  // Total reports from physical keyboard
volatile uint32_t keystrokes_sent_to_host = 0;            // Total reports sent to host computer
volatile uint32_t queue_drops_realtime = 0;               // Times we dropped oldest item in realtime queue

// Diagnostic cyclic buffers for keystroke history
diag_buffer_t diag_received_buffer = {0};
diag_buffer_t diag_sent_buffer = {0};

// LED control for visual status feedback (Num Lock)
static uint8_t current_led_state = 0;       // Current LED state to send to keyboard
static absolute_time_t next_led_toggle;     // When to toggle LED next
uint32_t led_on_interval_ms = 0;            // How long LED stays on (ms)
uint32_t led_off_interval_ms = 0;           // How long LED stays off (ms)

// USB suspend/resume state
volatile bool usb_suspended = false;
static uint32_t pre_suspend_clock_khz = 0;

// Built-in LED pin (GPIO25 on Pico/Pico2, CYW43 on Pico W/Pico2 W)
#ifndef PICO_CYW43_SUPPORTED
#define BUILTIN_LED_PIN 25
#endif

/*------------- LED Status Feedback -------------*/

void update_status_led(void) {
    // Keep LED ON until a keyboard is connected
    extern volatile bool usb_device_ever_mounted;
    if (!usb_device_ever_mounted) {
        // No keyboard connected yet - keep LED ON
#ifdef PICO_CYW43_SUPPORTED
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
#else
        gpio_put(BUILTIN_LED_PIN, 1);
#endif
        return;
    }

    if (led_on_interval_ms == 0 && led_off_interval_ms == 0) {
        // LED off (locked/blank state)
        current_led_state = 0;
    } else if (time_reached(next_led_toggle)) {
        // Toggle Num Lock LED (bit 0)
        current_led_state ^= 0x01;

        // Use different interval based on new state
        uint32_t next_interval = (current_led_state & 0x01) ? led_on_interval_ms : led_off_interval_ms;
        next_led_toggle = make_timeout_time_ms(next_interval);
    }

    // Update built-in LED to mirror NumLock state (bit 0)
    bool led_on = (current_led_state & 0x01) != 0;
#ifdef PICO_CYW43_SUPPORTED
    // Use CYW43 LED on Pico W/Pico2 W
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
#else
    // Use GPIO25 on Pico/Pico2
    gpio_put(BUILTIN_LED_PIN, led_on);
#endif

    // Send to physical keyboard (non-blocking)
    queue_try_add(&leds_queue, &current_led_state);
}

/*------------- USB Suspend/Resume Callbacks -------------*/

// Invoked when USB bus is suspended
// remote_wakeup_en: if true, host allows us to send wakeup signal
void tud_suspend_cb(bool remote_wakeup_en) {
    usb_suspended = true;
    LOG_INFO("USB suspended (remote_wakeup=%d)\n", remote_wakeup_en);

    // Save current clock speed
    pre_suspend_clock_khz = clock_get_hz(clk_sys) / 1000;

#ifdef PICO_CYW43_SUPPORTED
    // Shut down WiFi to save power
    if (wifi_is_initialized()) {
        wifi_suspend();
    }
#endif

    // Lower CPU clock to minimum stable frequency
    set_sys_clock_khz(48000, true);  // 48MHz

    LOG_INFO("Entering low-power mode (48MHz)\n");
}

// Invoked when USB bus is resumed
void tud_resume_cb(void) {
    LOG_INFO("USB resumed\n");
    usb_suspended = false;

    // Restore CPU clock
    if (pre_suspend_clock_khz > 0) {
        set_sys_clock_khz(pre_suspend_clock_khz, true);
    }

#ifdef PICO_CYW43_SUPPORTED
    // Resume WiFi
    if (wifi_is_initialized()) {
        wifi_resume();
    }
#endif

    LOG_INFO("Resumed to normal operation (%lu MHz)\n", clock_get_hz(clk_sys) / 1000000);
}

/*------------- MAIN -------------*/

// core0: handle device events
int main(void) {
    // default 125MHz is not appropriate for PIO. Sysclock should be multiple of 12MHz.
    set_sys_clock_khz(120000, true);

#ifdef ENABLE_NFC
    // NFC setup - DMA conflict resolved by configuring PIO-USB to use DMA ch 2
    nfc_setup();
#endif

    // init device stack on native usb (roothub port0)
    // Needs to be done before stdio_init_all();
    tud_init(0);

#ifdef ENABLE_USB_STDIO
    // Initialize USB CDC stdio (custom driver for TinyUSB host compatibility)
    // This provides printf/scanf over USB CDC for debugging
    cdc_stdio_lib_init();
    printf("USB CDC stdio initialized\n");
#endif

    stdio_init_all();

    flash_safe_execute_core_init();
    LOG_INFO("flash_safe_execute_core_init() complete\n");

    // Initialize kvstore EARLY, before launching Core 1
    LOG_INFO("Starting kvstore_init() (before Core 1 launch)\n");
    if (!kvstore_init()) {
        LOG_ERROR("Failed to initialize kvstore!\n");
        // Continue anyway - device will work without persistent storage
    }
    LOG_INFO("kvstore_init() complete\n");

    queue_init(&keyboard_to_tud_queue, sizeof(hid_report_t), 12);
    queue_init(&tud_to_physical_host_queue, sizeof(send_data_t), 256);
    queue_init(&leds_queue, 1, 4);

    // Initialize diagnostic buffer spin locks (must be before Core 1 launch)
    diag_received_buffer.lock = spin_lock_init(spin_lock_claim_unused(true));
    diag_sent_buffer.lock = spin_lock_init(spin_lock_claim_unused(true));
    LOG_INFO("Diagnostic buffer spin locks initialized\n");

    LOG_INFO("Setting initial state to locked\n");
    kb.status = locked;

    LOG_INFO("\n\nCore 0 (tud) running\n");

    LOG_INFO("Resetting and launching Core 1\n");
    multicore_reset_core1();
    // Launch Core 1 AFTER kvstore is initialized to avoid flash contention
    multicore_launch_core1(core1_main);
    LOG_INFO("Core 1 launched\n");

#ifdef PICO_CYW43_SUPPORTED
    // Initialize WiFi (if configured) - only on Pico W
    wifi_config_init();
    wifi_init();
    LOG_INFO("WiFi initialization complete\n");
    // CYW43 LED is automatically initialized by wifi_init()
    // Turn LED ON until keyboard connects
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    LOG_INFO("Built-in LED ON (will turn off when keyboard connects)\n");
#else
    LOG_INFO("WiFi not supported on this hardware\n");
    // Initialize built-in LED (GPIO25) on non-WiFi boards
    gpio_init(BUILTIN_LED_PIN);
    gpio_set_dir(BUILTIN_LED_PIN, GPIO_OUT);
    gpio_put(BUILTIN_LED_PIN, 1);  // Start with LED ON (will turn off when keyboard connects)
    LOG_INFO("Built-in LED initialized on GPIO%d (ON until keyboard connects)\n", BUILTIN_LED_PIN);
#endif

#ifdef BOARD_WS_2350
    // Initialize WS2812 RGB LED for status indication
    if (ws2812_led_init()) {
        LOG_INFO("WS2812 RGB LED initialized successfully\n");
        // Set initial color based on locked state
        ws2812_led_update_status(locked, false);
    } else {
        LOG_ERROR("Failed to initialize WS2812 RGB LED\n");
    }
#endif

    LOG_INFO("Entering main loop\n");
    absolute_time_t last_interaction = get_absolute_time();
    absolute_time_t start_time = get_absolute_time();
    status_t previous_status = locked;
#ifdef PICO_CYW43_SUPPORTED
    bool http_server_started = false;
    bool mqtt_client_started = false;
#endif

    LOG_INFO("Starting main event loop (first iteration)\n");
    bool status_message_printed = false;

    while (true) {
        // Print comprehensive status message after 5 seconds (when USB CDC is ready)
        if (!status_message_printed && absolute_time_diff_us(start_time, get_absolute_time()) > 5000000) {
            status_message_printed = true;

            // Count keydefs
            uint8_t triggers[256];
            int keydef_count = keydef_list(triggers, 256);
            int public_count = 0, private_count = 0;
            for (int i = 0; i < keydef_count; i++) {
                keydef_t *def = keydef_load(triggers[i]);
                if (def) {
                    if (!def->require_unlock) public_count++;
                    else private_count++;
                    free(def);
                }
            }

            printf("\n");
            printf("=== HID Proxy Status (5s uptime) ===\n");
#ifdef BOARD_WS_2350
            printf("Board: Waveshare RP2350-USB-A\n");
            printf("USB-A: GPIO12 (D+), GPIO13 (D-)\n");
#elif defined(PICO_CYW43_SUPPORTED)
            printf("Board: Raspberry Pi Pico W\n");
            printf("PIO-USB: GPIO2 (D+), GPIO3 (D-)\n");
#else
            printf("Board: Raspberry Pi Pico\n");
            printf("PIO-USB: GPIO2 (D+), GPIO3 (D-)\n");
#endif
            printf("State: %s\n", status_string(kb.status));
            printf("Keydefs: %d defined (%d public, %d private)\n", keydef_count, public_count, private_count);
            printf("Keystrokes: %lu received, %lu sent, %lu dropped\n",
                   (unsigned long)keystrokes_received_from_physical,
                   (unsigned long)keystrokes_sent_to_host,
                   (unsigned long)queue_drops_realtime);
            printf("Queue depths: keyboard_to_tud=%d, tud_to_host=%d\n",
                   queue_get_level(&keyboard_to_tud_queue),
                   queue_get_level(&tud_to_physical_host_queue));
            printf("USB HID ready: kbd=%s mouse=%s\n",
                   tud_hid_n_ready(ITF_NUM_KEYBOARD) ? "yes" : "NO",
                   tud_hid_n_ready(ITF_NUM_MOUSE) ? "yes" : "NO");
#ifdef PICO_CYW43_SUPPORTED
            if (wifi_is_connected()) {
                printf("WiFi: Connected\n");
            } else {
                printf("WiFi: Not connected\n");
            }
#endif
            printf("Uptime: 5 seconds\n");
            printf("====================================\n");
            printf("\n");
        }

        if (kb.status != previous_status) {
            LOG_INFO("State changed from %s to %s\n", status_string(previous_status), status_string(kb.status));
            previous_status = kb.status;

#ifdef BOARD_WS_2350
            // Update RGB LED when status changes
#ifdef PICO_CYW43_SUPPORTED
            ws2812_led_update_status(kb.status, web_access_is_enabled());
#else
            ws2812_led_update_status(kb.status, false);
#endif
#endif
        }

        // Always run USB device task (handles suspend/resume internally)
        tud_task();
        tud_cdc_write_flush();

        // Skip non-critical tasks when suspended to save power
        if (!usb_suspended) {
            update_status_led();  // Update LED status feedback

#ifdef BOARD_WS_2350
            // Update RGB LED animations (e.g. rainbow pulse for web access)
            ws2812_led_task();
#endif

#ifdef ENABLE_NFC
            nfc_task(kb.status == locked);
#endif

#ifdef PICO_CYW43_SUPPORTED
            // WiFi tasks (only on Pico W, and only when not suspended)
            if (!wifi_is_suspended()) {
                wifi_task();
                if (wifi_is_connected() && !http_server_started) {
                    http_server_init();
                    http_server_started = true;
                }
                if (wifi_is_connected() && !mqtt_client_started) {
                    mqtt_client_started = mqtt_client_init();
                }
                http_server_task();
                mqtt_client_task();
            }
#endif
        }

        // Process keyboard reports even when suspended (needed for remote wakeup)
        hid_report_t report;
        if (queue_try_remove(&keyboard_to_tud_queue, &report)) {
            last_interaction = get_absolute_time();
            next_report(report);

            // If suspended, try to send remote wakeup signal
            if (usb_suspended && tud_remote_wakeup()) {
                LOG_INFO("Sent remote wakeup signal\n");
            }
        }

        // Anything waiting to be sent to the (real) host?
        // Use tud_hid_n_ready() to check if USB interface can accept new report
        send_data_t to_send;
        if (queue_try_peek(&tud_to_physical_host_queue, &to_send)) {
            // Check if the specific HID interface (keyboard or mouse) is ready
            if (tud_hid_n_ready(to_send.report_id)) {
                queue_try_remove(&tud_to_physical_host_queue, &to_send);
                send_report_to_host(to_send);
            }
        }

        if (kb.status == locked) {
#ifdef ENABLE_NFC
            if (nfc_key_available()) {
                uint8_t key[32];  // Full 32-byte AES-256 key (use first 16 bytes for AES-128)
                nfc_get_key(key);
                printf("Setting 16-byte key from NFC\n");
                hex_dump(key, 16);
                enc_set_key(key, 16);  // Use first 16 bytes for AES-128
                kvstore_set_encryption_key(key);

                // Try to verify the key by loading any keydef
                uint8_t triggers[1];
                if (keydef_list(triggers, 1) > 0) {
                    keydef_t *test_def = keydef_load(triggers[0]);
                    if (test_def != NULL) {
                        // Key is valid
                        free(test_def);
                        unlock();
                        printf("NFC authentication successful\n");
                    } else {
                        // Key is invalid
                        nfc_bad_key();
                    }
                } else {
                    // No keydefs stored yet - assume key is valid
                    unlock();
                    printf("NFC key accepted (no keydefs to verify)\n");
                }
            }
#endif
        }

        if (kb.status != locked && absolute_time_diff_us(last_interaction, get_absolute_time()) > 1000 * IDLE_TIMEOUT_MILLIS) {
            LOG_INFO("Timed out - clearing encrypted data\n");
            lock();
        }

        // When suspended, use __wfe() to sleep until interrupt (saves power)
        if (usb_suspended) {
            __wfe();
        }
    }

    return 0;
}

void send_report_to_host(send_data_t to_send) {
    bool ok = tud_hid_n_report(to_send.instance, to_send.report_id, to_send.data, to_send.len);
    if (ok) {
#ifdef DEBUG
        LOG_DEBUG("Sent to host instance=%x report_id=%x (len=%d): ", to_send.instance, to_send.report_id, to_send.len);
        hex_dump(to_send.data, to_send.len);
#endif
        // Count keyboard reports sent to host
        if (to_send.report_id == ITF_NUM_KEYBOARD) {
            keystrokes_sent_to_host++;
            // Log to diagnostic buffer
            diag_log_keystroke(&diag_sent_buffer, keystrokes_sent_to_host, (const hid_keyboard_report_t *)to_send.data);
        }
    } else {
        LOG_ERROR("tud_hid_n_report FAILED: %x\n", to_send.report_id);
    }
}

/*! \brief Add item to queue with backpressure (blocking)
 *
 * Used for macro playback where we want to ensure ALL keystrokes are sent.
 * If queue is full, this function blocks and processes USB events (tud_task)
 * to drain the queue. This naturally throttles macro playback to USB speed.
 *
 * \param q Queue to add to
 * \param data Pointer to data to add
 */
void queue_add_with_backpressure(queue_t *q, const void *data) {
    while (!queue_try_add(q, data)) {
        // Queue full - process USB to drain it
        tud_task();
        tight_loop_contents();  // Yield to other core
    }
}

/*! \brief Add item to queue with graceful degradation (non-blocking)
 *
 * Used for real-time keyboard input where we don't want to block.
 * If queue is full, drops the OLDEST item to make room for the newest.
 * This ensures real-time input never blocks, but may lose data under extreme load.
 *
 * \param q Queue to add to
 * \param data Pointer to data to add
 */
void queue_add_realtime(queue_t *q, const void *data) {
    if (!queue_try_add(q, data)) {
        // Queue full - drop oldest item to make room
        queue_drops_realtime++;
        uint8_t discard_buffer[sizeof(send_data_t)];  // Max size of any queue item
        if (queue_try_remove(q, discard_buffer)) {
            LOG_WARNING("Queue overflow - dropped oldest report to make room (total drops: %lu)\n",
                       (unsigned long)queue_drops_realtime);
            // Try again (should succeed now)
            if (!queue_try_add(q, data)) {
                LOG_ERROR("Queue add failed even after drop - this shouldn't happen (total drops: %lu)\n",
                         (unsigned long)queue_drops_realtime);
            }
        } else {
            LOG_ERROR("Queue full but can't remove item - concurrent access issue? (total drops: %lu)\n",
                     (unsigned long)queue_drops_realtime);
        }
    }
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
    (void) instance;

    LOG_INFO("tud_hid_set_report_cb[%x]  %x %x (size=%d)\n", instance, report_id, report_type, bufsize);

    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        // Set keyboard LED e.g Capslock, Numlock etc...
        if (report_id == ITF_NUM_KEYBOARD) {
            // bufsize should be (at least) 1
            if (bufsize < 1) return;

            LOG_DEBUG("leds: %x\n", buffer[0]);
            // LED queue is small (4 items) and low-frequency, use try_add
            if (!queue_try_add(&leds_queue, buffer)) {
                LOG_WARNING("LED queue full - dropping LED update\n");
            }
        }
    }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    (void) instance;

    LOG_DEBUG("tud_hid_get_report_cb: instance=%x report_id=%x report_type=%x reqlen=%d\n",
             instance, report_id, report_type, reqlen);

    // For keyboard interface, return empty keyboard report when idle
    if (report_type == HID_REPORT_TYPE_INPUT) {
        if (report_id == ITF_NUM_KEYBOARD && reqlen >= sizeof(hid_keyboard_report_t)) {
            hid_keyboard_report_t empty_report = {0};
            memcpy(buffer, &empty_report, sizeof(hid_keyboard_report_t));
            return sizeof(hid_keyboard_report_t);
        }
        // For mouse interface
        else if (report_id == ITF_NUM_MOUSE && reqlen >= sizeof(hid_mouse_report_t)) {
            hid_mouse_report_t empty_report = {0};
            memcpy(buffer, &empty_report, sizeof(hid_mouse_report_t));
            return sizeof(hid_mouse_report_t);
        }
    }
    // For output reports (e.g., LED status), return current LED state
    else if (report_type == HID_REPORT_TYPE_OUTPUT) {
        if (report_id == ITF_NUM_KEYBOARD && reqlen >= 1) {
            buffer[0] = current_led_state;
            return 1;
        }
    }

    // Unsupported report type/id - STALL the request
    LOG_DEBUG("tud_hid_get_report_cb: Unsupported report_id=%x report_type=%x\n", report_id, report_type);
    return 0;
}

/*------------- Diagnostic Buffer Functions -------------*/

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

void lock() {
    kb.status = locked;
    led_on_interval_ms = 0;   // LED off when locked
    led_off_interval_ms = 0;
    kvstore_clear_encryption_key();  // Clear encryption key from memory
    enc_clear_key();  // Also clear the key in encryption.c

#ifdef PICO_CYW43_SUPPORTED
    // Publish lock event to MQTT
    mqtt_publish_lock_state(true);
#endif
}

void unlock() {
    kb.status = normal;
    led_on_interval_ms = 100;    // Slow pulse when unlocked
    led_off_interval_ms = 2400;

#ifdef PICO_CYW43_SUPPORTED
    // Publish unlock event to MQTT
    mqtt_publish_lock_state(false);
#endif
}

void hex_dump(void const *ptr, size_t len) {
    uint8_t const *p = ptr;
    for (size_t i = 0; i < len; i += 16) {
        printf("%04x  ", i);
        for (int byte = 0; byte < 16 && i + byte < len; byte++) {
            printf(" %02x", p[i + byte]);
            if ((byte + 1) % 4 == 0) {
                printf(" ");
            }
        }
        printf("\n");
    }
}
