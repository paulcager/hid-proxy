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

#ifdef PICO_CYW43_SUPPORTED
#include "pico/cyw43_arch.h"
#include "wifi_config.h"
#include "http_server.h"
#include "mqtt_client.h"
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

    LOG_INFO("Resumed to normal operation (%d MHz)\n", clock_get_hz(clk_sys) / 1000000);
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
#else
    LOG_INFO("WiFi not supported on this hardware\n");
    // Initialize built-in LED (GPIO25) on non-WiFi boards
    gpio_init(BUILTIN_LED_PIN);
    gpio_set_dir(BUILTIN_LED_PIN, GPIO_OUT);
    gpio_put(BUILTIN_LED_PIN, 0);  // Start with LED off
    LOG_INFO("Built-in LED initialized on GPIO%d\n", BUILTIN_LED_PIN);
#endif

    LOG_INFO("Entering main loop\n");
    absolute_time_t last_interaction = get_absolute_time();
    status_t previous_status = locked;
#ifdef PICO_CYW43_SUPPORTED
    bool http_server_started = false;
    bool mqtt_client_started = false;
#endif

    LOG_INFO("Starting main event loop (first iteration)\n");
    while (true) {
        if (kb.status != previous_status) {
            LOG_INFO("State changed from %s to %s\n", status_string(previous_status), status_string(kb.status));
            previous_status = kb.status;
        }

        // Always run USB device task (handles suspend/resume internally)
        tud_task();
        tud_cdc_write_flush();

        // Skip non-critical tasks when suspended to save power
        if (!usb_suspended) {
            update_status_led();  // Update LED status feedback
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
