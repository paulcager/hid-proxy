/*
 *
 * hid_proxy.c
 * 
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *                    sekigon-gonnoc
 * Copyright (c) 2023 paul.cager@gmail.com
 *
 *
 * Top-level application logic for the HID Proxy device.
 *
 * This firmware runs on the RP2040 (Raspberry Pi Pico) and acts as a USB
 * Human Interface Device (HID) proxy:
 *
 *   - On the USB host side, it connects to one or more physical HID devices
 *     (typically keyboards).
 *   - On the USB device side, it presents itself to a host computer as a
 *     standard USB HID keyboard.
 *   - HID reports received from the physical device are queued, optionally
 *     filtered or gated, and then forwarded to the upstream host.
 *
 * Architecture overview:
 *
 *   - Core 0 runs the TinyUSB *device* stack and presents the HID interface
 *     to the upstream host computer.
 *   - Core 1 runs the TinyUSB *host* stack and handles attached physical
 *     HID devices.
 *   - The two cores communicate via queues, which decouple USB timing from
 *     key event production and provide backpressure handling.
 *
 * HID Proxy High-Level Overview:
 *
 *  +----------------+     HID events     +-----------------+
 *  | Physical HID   | <- USB Host (Core1)| Queue           |
 *  | Devices        |                    | keyboard_to_tud |
 *  +----------------+                    +-----------------+
 *         |                                              |
 *         v        USB Device (Core0)                     v
 *  +----------------+       HID reports       +----------------+
 *  | Upstream Host  | <---------------------- | Proxy Device    |
 *  +----------------+                        +------------------+
 *
 *
 *
 * This file coordinates:
 *   - System initialization and main event loop
 *   - HID SET_REPORT / GET_REPORT callbacks
 *   - Queueing and forwarding of HID reports between host and device sides
 *   - High-level lock/unlock and gating logic (e.g. NFC, encryption, timeouts)
 *
 * Lower-level functionality such as LED/status indication, NFC handling,
 * cryptography, and networking is implemented in separate modules.
 *
 * The intent of this file is to describe the overall behaviour and data flow
 * of the HID proxy rather than hardware-specific details.
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
#include "led_control.h"

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
// make && openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program $(ls *.elf) verify reset exit"
// minicom -b 115200 -o -D /dev/ttyACM0
// UART0 on standard ports and PIO-USB on GPIO6/7 (see CMakeLists.txt to change).


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

// LED control moved to led_control.c

/*------------- LED Status Feedback -------------*/
// LED logic moved to led_control.c

/*------------- USB Suspend/Resume -------------*/
// NOTE: USB suspend/resume callbacks deliberately NOT implemented.
//
// Previously we had tud_suspend_cb() and tud_resume_cb() that would:
// - Lower CPU clock to save power (120MHz -> 48MHz)
// - Suspend WiFi to save power
// - Use __wfe() sleep mode
//
// This was REMOVED because:
// 1. Caused stuck-key bug (PC thought keys were held after resume)
// 2. Power savings negligible for wall-powered device (~0.5W)
// 3. Added unnecessary complexity and timing issues
// 4. Remote wakeup still works without callbacks (TinyUSB handles it)
//
// If you need to re-add suspend handling, make sure to send a
// release_all_keys HID report in tud_resume_cb() to clear stuck keys!

/*------------- INITIALIZATION PHASES -------------*/

/**
 * @brief Initialize core system components
 *
 * Sets up system clock, stdio, TinyUSB device stack, flash subsystem,
 * and kvstore (persistent storage).
 *
 * IMPORTANT: This must be called before launch_core1() to avoid flash
 * contention between cores.
 */
static void system_init(void) {
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

    // Initialize inter-queue communication
    queue_init(&keyboard_to_tud_queue, sizeof(hid_report_t), 12);
    queue_init(&tud_to_physical_host_queue, sizeof(send_data_t), 256);
    queue_init(&leds_queue, 1, 4);

    // Pass leds_queue to LED control module
    led_set_queue(&leds_queue);

    // Initialize diagnostic system (if enabled via ENABLE_DIAGNOSTICS)
    diagnostics_init();

    // Set initial state
    LOG_INFO("Setting initial state to sealed\n");
    kb.status = sealed;
}

/**
 * @brief Launch Core 1 (USB host stack)
 *
 * IMPORTANT: Must be called AFTER system_init() to avoid flash contention.
 * Core 1 runs the TinyUSB host stack and handles physical keyboard input.
 */
static void launch_core1(void) {
    LOG_INFO("\n\nCore 0 (tud) running\n");
    LOG_INFO("Resetting and launching Core 1\n");
    multicore_reset_core1();
    // Launch Core 1 AFTER kvstore is initialized to avoid flash contention
    multicore_launch_core1(core1_main);
    LOG_INFO("Core 1 launched\n");
}

/**
 * @brief Initialize network subsystems (WiFi, HTTP, MQTT)
 *
 * Only active on WiFi-enabled builds (PICO_CYW43_SUPPORTED).
 * IMPORTANT: Must be called BEFORE peripheral_init() because LED
 * initialization checks wifi_is_initialized().
 */
static void network_init(void) {
#ifdef PICO_CYW43_SUPPORTED
    // Initialize WiFi (if configured)
    // This attempts to initialize CYW43, which will succeed on Pico W and fail on plain Pico
    wifi_config_init();
    wifi_init();
    LOG_INFO("WiFi initialization complete (CYW43 present: %s)\n",
             wifi_is_initialized() ? "yes" : "no");
#endif
}

/**
 * @brief Initialize peripheral hardware (LEDs, NFC)
 *
 * IMPORTANT: Must be called AFTER network_init() because LED
 * initialization checks wifi_is_initialized() to detect Pico W vs Pico.
 */
static void peripheral_init(void) {
    // Initialize built-in LED (detects CYW43 vs GPIO25 at runtime)
    led_init();
    led_set(true);  // Start with LED ON (will turn off when keyboard connects)
    LOG_INFO("Built-in LED initialized and ON (will turn off when keyboard connects)\n");

#ifdef BOARD_WS_2350
    // Initialize WS2812 RGB LED for status indication
    if (ws2812_led_init()) {
        LOG_INFO("WS2812 RGB LED initialized successfully\n");
        // Set initial color based on sealed state
        ws2812_led_update_status(sealed, false);
    } else {
        LOG_ERROR("Failed to initialize WS2812 RGB LED\n");
    }
#endif
}

/**
 * @brief Main event loop
 *
 * Handles:
 * - USB device tasks (keyboard/mouse HID)
 * - Status message printing (after 5 seconds)
 * - LED updates
 * - Network tasks (WiFi, HTTP, MQTT)
 * - NFC authentication
 * - Idle timeout sealing
 * - Queue processing between cores
 */
static void main_loop(void) {
    LOG_INFO("Starting main event loop\n");
    absolute_time_t last_interaction = get_absolute_time();
    absolute_time_t start_time = get_absolute_time();
    status_t previous_status = sealed;
    bool status_message_printed = false;
#ifdef PICO_CYW43_SUPPORTED
    bool http_server_started = false;
    bool mqtt_client_started = false;
#endif

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
            printf("PIO-USB: GPIO%d (D+), GPIO%d (D-)\n", USB_HOST_DP_PIN, USB_HOST_DP_PIN + 1);
#else
            printf("Board: Raspberry Pi Pico\n");
            printf("PIO-USB: GPIO%d (D+), GPIO%d (D-)\n", USB_HOST_DP_PIN, USB_HOST_DP_PIN + 1);
#endif
            printf("Firmware: " GIT_COMMIT_HASH "\n");
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

            // Boot message complete - LEDs can now show normal status
            led_boot_complete();
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

        // Update LED status feedback
        update_status_led();

#ifdef BOARD_WS_2350
        // Update RGB LED animations (e.g. rainbow pulse for web access)
        ws2812_led_task();
#endif

#ifdef ENABLE_NFC
        nfc_task(kb.status == sealed);
#endif

#ifdef PICO_CYW43_SUPPORTED
        // WiFi tasks (only on Pico W)
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
#endif

        // Process keyboard reports from physical keyboard
        hid_report_t report;
        if (queue_try_remove(&keyboard_to_tud_queue, &report)) {
            last_interaction = get_absolute_time();
            next_report(report);
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

        if (kb.status == sealed) {
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
                        unseal();
                        printf("NFC authentication successful\n");
                    } else {
                        // Key is invalid
                        nfc_bad_key();
                    }
                } else {
                    // No keydefs stored yet - assume key is valid
                    unseal();
                    printf("NFC key accepted (no keydefs to verify)\n");
                }
            }
#endif
        }

        if (kb.status != sealed && absolute_time_diff_us(last_interaction, get_absolute_time()) > 1000 * IDLE_TIMEOUT_MILLIS) {
            LOG_INFO("Timed out - clearing encrypted data\n");
            seal();
        }
    }
    // Loop never exits
}

/*------------- MAIN ENTRY POINT -------------*/

/**
 * @brief Main entry point
 *
 * Initializes all subsystems and enters the main event loop.
 * Critical initialization ordering:
 *   1. system_init()      - MUST be before Core 1 (flash contention)
 *   2. launch_core1()     - Starts USB host stack on Core 1
 *   3. network_init()     - MUST be before peripheral_init() (LED needs wifi_is_initialized())
 *   4. peripheral_init()  - LED and RGB LED initialization
 *   5. main_loop()        - Never returns
 */
int main(void) {
    system_init();
    launch_core1();
    network_init();
    peripheral_init();
    main_loop();

    // Never reached
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

            LOG_DEBUG("leds from host: %x\n", buffer[0]);
            // Update the LED controller with host's LED state
            // It will merge this with our NUM_LOCK status indication
            led_set_host_state(buffer[0]);
        }
    }
}

/*
 * HID GET_REPORT callback.
 *
 * This device operates as a streaming HID proxy. Keyboard input is delivered
 * asynchronously via the interrupt IN endpoint as reports arrive from the
 * physical device.
 *
 * The proxy does not maintain a persistent snapshot of key state, so there is
 * no meaningful report to return in response to GET_REPORT. Returning an empty
 * report is sufficient and expected for keyboard-style HID devices.
 */
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
            buffer[0] = led_get_state();
            return 1;
        }
    }

    // Unsupported report type/id: return an empty report (no data)
    LOG_DEBUG("tud_hid_get_report_cb: Unsupported report_id=%x report_type=%x\n",
              report_id, report_type);
    return 0;
}

void seal() {
    kb.status = sealed;
    led_set_intervals(0, 0);   // LED off when sealed
    kvstore_clear_encryption_key();  // Clear encryption key from memory
    enc_clear_key();  // Also clear the key in encryption.c

#ifdef PICO_CYW43_SUPPORTED
    // Publish seal event to MQTT
    mqtt_publish_seal_state(true);
#endif
}

void unseal() {
    kb.status = unsealed;
    led_set_intervals(100, 2400);    // Slow pulse when unsealed

#ifdef PICO_CYW43_SUPPORTED
    // Publish unseal event to MQTT
    mqtt_publish_seal_state(false);
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
