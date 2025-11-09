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

#ifdef PICO_CYW43_SUPPORTED
#include "wifi_config.h"
#include "http_server.h"
#endif

#ifdef ENABLE_USB_STDIO
#include "cdc_stdio_lib.h"
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

// Synchronization flag: Core 1 waits for this before starting USB host stack
volatile bool kvstore_init_complete = false;

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

    LOG_INFO("Setting initial state to locked\n");
    kb.status = locked;

    LOG_INFO("\n\nCore 0 (tud) running\n");

    LOG_INFO("Resetting and launching Core 1\n");
    multicore_reset_core1();
    // Launch Core 1 AFTER kvstore is initialized to avoid flash contention
    multicore_launch_core1(core1_main);
    LOG_INFO("Core 1 launched\n");

    // Signal Core 1 that initialization is complete (it can start immediately)
    kvstore_init_complete = true;
    LOG_INFO("kvstore_init_complete flag set\n");

#ifdef PICO_CYW43_SUPPORTED
    // Initialize WiFi (if configured) - only on Pico W
    LOG_INFO("Calling wifi_config_init()\n");
    wifi_config_init();
    LOG_INFO("Calling wifi_init()\n");
    wifi_init();
    LOG_INFO("WiFi initialization complete\n");
#else
    LOG_INFO("WiFi not supported on this hardware\n");
#endif

    LOG_INFO("Entering main loop\n");
    absolute_time_t last_interaction = get_absolute_time();
    status_t previous_status = locked;
#ifdef PICO_CYW43_SUPPORTED
    bool http_server_started = false;
#endif

    LOG_INFO("Starting main event loop (first iteration)\n");
    while (true) {
        if (kb.status != previous_status) {
            LOG_INFO("State changed from %s to %s\n", status_string(previous_status), status_string(kb.status));
            previous_status = kb.status;
        }

        tud_task(); // tinyusb device task
        tud_cdc_write_flush();
#ifdef ENABLE_NFC
        nfc_task(kb.status == locked);
#endif

#ifdef PICO_CYW43_SUPPORTED
        // WiFi tasks (only on Pico W)
        wifi_task();
        if (wifi_is_connected() && !http_server_started) {
            http_server_init();
            http_server_started = true;
        }
        http_server_task();
#endif

        hid_report_t report;
        // Anything sent to us from the keyboard process (PIO on core1)?
        if (queue_try_remove(&keyboard_to_tud_queue, &report)) {
            last_interaction = get_absolute_time();
            next_report(report);
        }

        // Anything waiting to be sent to the (real) host?
        // TODO - is there a ready function?
        send_data_t to_send;
        if (!kb.send_to_host_in_progress && queue_try_remove(&tud_to_physical_host_queue, &to_send)) {
            send_report_to_host(to_send);
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
                        kb.status = normal;
                        printf("NFC authentication successful\n");
                    } else {
                        // Key is invalid
                        nfc_bad_key();
                    }
                } else {
                    // No keydefs stored yet - assume key is valid
                    kb.status = normal;
                    printf("NFC key accepted (no keydefs to verify)\n");
                }
            }
#endif
        }

        if (kb.status != locked && absolute_time_diff_us(last_interaction, get_absolute_time()) > 1000 * IDLE_TIMEOUT_MILLIS) {
            LOG_INFO("Timed out - clearing encrypted data\n");
            lock();
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
        kb.send_to_host_in_progress = true;
    } else {
        LOG_ERROR("tud_hid_n_report FAILED: %x\n", to_send.report_id);
    }
}

void queue_add_or_panic(queue_t *q, const void *data) {
    if (!queue_try_add(q, data)) {
        // TODO - this is most likely to happen if we are sending large definitions
        // more quickly than they can be sent over USB. Fix this by interleaving
        // queue puts with tud_task and using blocking adds.
        panic("Queue is full");
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
            queue_add_or_panic(&leds_queue, buffer);
        }
    }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
    // TODO not Implemented
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    LOG_INFO("tud_hid_get_report_cb: %x %x %x\n", report_id, report_type, buffer[0]);

    return 0;
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len) {
    (void) instance;
    (void) report;
    (void) len;

    LOG_TRACE("tud_hid_report_complete_cb");
    kb.send_to_host_in_progress = false;
}

void lock() {
    kb.status = locked;
    kvstore_clear_encryption_key();  // Clear encryption key from memory
    enc_clear_key();  // Also clear the key in encryption.c
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
