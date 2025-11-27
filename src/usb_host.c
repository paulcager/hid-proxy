//
// Created by paul on 14/02/25.
//

#include "nfc_tag.h"
#include "hid_proxy.h"
#include "led_control.h"
#include "tusb.h"
#include "pio_usb.h"
#include "pico/util/queue.h"
#include <pico/flash.h>
#include "pico.h"
#include <stdio.h>
#include "usb_host.h"
#include "usb_descriptors.h"
#include "hardware/gpio.h"
#ifdef PICO_CYW43_SUPPORTED
#include "pico/cyw43_arch.h"
#endif

_Noreturn void core1_loop();

void handle_mouse_report(hid_report_t *report);

static void handle_generic_report(hid_report_t report);

#define MAX_REPORT  4
static struct {
    uint8_t report_count;
    tuh_hid_report_info_t report_info[MAX_REPORT];
} hid_info[CFG_TUH_HID];

// Debug: track if any USB device has ever been detected
volatile bool usb_device_ever_mounted = false;
volatile uint32_t usb_mount_count = 0;



// core1: handle host events
void core1_main() {
    LOG_INFO("Core 1: Starting initialization\n");

    LOG_INFO("Core 1: Calling flash_safe_execute_core_init()\n");
    flash_safe_execute_core_init();
    LOG_INFO("Core 1: flash_safe_execute_core_init() complete\n");

    // Note: kvstore is already initialized on Core 0 before Core 1 launches
    LOG_INFO("Core 1: Starting USB host stack\n");

    // Use tuh_configure() to pass pio configuration to the host stack
    // Note: tuh_configure() must be called before

    LOG_INFO("Core 1: Configuring PIO-USB\n");
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
#ifdef BOARD_WS_2350
    // Waveshare RP2350-USB-A: USB-A socket is connected to GPIO12 (D+) and GPIO13 (D-) per schematic
    // Use GPIO12 for D+ (D- is automatically D+ + 1 = GPIO13)
    pio_cfg.pin_dp = 12;
#else
    // Standard Pico boards: USB host D+ pin (D- is automatically D+ + 1)
    // Default: GPIO6/7 (configurable at build time with --usb-pins flag)
    // Legacy: GPIO2/3 (use --usb-pins 2 for backwards compatibility)
    pio_cfg.pin_dp = USB_HOST_DP_PIN;
#endif
    // Use DMA channel 2 instead of 0 to avoid conflict with CYW43 WiFi
    pio_cfg.tx_ch = 2;
    LOG_INFO("Core 1: pio_cfg.pin_dp = %d, tx_ch = %d\n", pio_cfg.pin_dp, pio_cfg.tx_ch);

    LOG_INFO("Core 1: Calling tuh_configure()\n");
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    LOG_INFO("Core 1: tuh_configure() complete\n");

    LOG_INFO("Core 1: Calling tuh_init(1)\n");
    bool ok = tuh_init(1);
    if (!ok) {
        LOG_ERROR("Core 1: tuh_init(1) FAILED!\n");
        panic("tuh_init(1)");
    }
    LOG_INFO("Core 1: tuh_init(1) complete\n");

    // TODO - to support this properly, we'll have to determine what's been plugged into which port.
//    pio_cfg.pin_dp = 6;
//    LOG_INFO("pio_cfg.pin_dp = %d\n", pio_cfg.pin_dp);
//    tuh_configure(2, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
//    ok = tuh_init(2);
//    if (!ok) {
//        panic("tuh_init(2)");
//    }

    LOG_INFO("Core 1: tuh running, entering core1_loop\n");
    core1_loop();
}

_Noreturn void core1_loop() {
    extern volatile bool usb_suspended;

    while (true) {
        tuh_task(); // tinyusb host task

        // Only update LEDs when not suspended to save power
        if (!usb_suspended) {
            uint8_t leds;
            if (queue_try_remove(&leds_queue, &leds)) {
                tuh_hid_set_report(1, 0, 0, HID_REPORT_TYPE_OUTPUT, &leds, sizeof(leds));
            }
        } else {
            // Sleep when suspended (wake on keyboard input interrupt)
            __wfe();
        }
    }
}

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    (void) desc_report;
    (void) desc_len;

    // Track USB device mounts for debugging
    usb_device_ever_mounted = true;
    usb_mount_count++;

    // Interface protocol (hid_interface_protocol_enum_t)
    const char *protocol_str[] = {"None", "Keyboard", "Mouse"};
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    LOG_INFO("[%04x:%04x][%u] HID Interface%u, Protocol = %s\r\n", vid, pid, dev_addr, instance, protocol_str[itf_protocol]);
    hex_dump(desc_report, desc_len);

    // Turn off built-in LED when keyboard is connected
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        led_set(false);  // Turn off built-in LED (auto-detects CYW43 or GPIO25)
        LOG_INFO("Keyboard connected - built-in LED turned off\n");
    }

    // By default, host stack will use activate boot protocol on supported interface.
    // Therefore, for this simple example, we only need to parse generic report descriptor (with built-in parser)
    if (itf_protocol == HID_ITF_PROTOCOL_NONE) {
        hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
        for (int i = 0; i < hid_info[instance].report_count; i++) {
            tuh_hid_report_info_t *info = hid_info[instance].report_info + i;
            LOG_INFO("   report[%d]: report_id=%i, usage_page=%d, usage = %d\n", i, info->report_id, info->usage_page, info->usage);
        }
    }

    // request to receive report
    // tuh_hid_report_received_cb() will be invoked when report is available
    if (!tuh_hid_receive_report(dev_addr, instance)) {
        LOG_ERROR("Error: cannot request to receive report\r\n");
    }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    LOG_INFO("[%u] HID Interface%u is unmounted\r\n", dev_addr, instance);
    stdio_flush();
}

void handle_mouse_report(hid_report_t *report) {
    // Log and forward mouse reports to host
    hid_mouse_report_t mouse = report->data.mouse;
    char l = mouse.buttons & MOUSE_BUTTON_LEFT ? 'L' : '-';
    char m = mouse.buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-';
    char r = mouse.buttons & MOUSE_BUTTON_RIGHT ? 'R' : '-';

    (void) l; (void) m; (void) r;

    LOG_DEBUG("[%u] %c%c%c %4d %4d %4d\n", (*report).dev_addr, l, m, r, (*report).data.mouse.x, (*report).data.mouse.y, (*report).data.mouse.wheel);

    // Forward mouse report to host (realtime - mouse movement must not block)
    add_to_host_queue_realtime(report->instance, ITF_NUM_MOUSE, sizeof(hid_mouse_report_t), &report->data.mouse);
}

static void handle_generic_report(hid_report_t report) {

    uint8_t const rpt_count = hid_info[report.instance].report_count;
    tuh_hid_report_info_t *rpt_info_arr = hid_info[report.instance].report_info;
    tuh_hid_report_info_t *rpt_info = NULL;
    uint8_t *bytes = report.data.bytes;

    if (rpt_count == 1 && rpt_info_arr[0].report_id == 0) {
        // Simple report without report ID as 1st byte
        rpt_info = &rpt_info_arr[0];
    } else {
        // Composite report, 1st byte is report ID, data starts from 2nd byte
        uint8_t const rpt_id = bytes[0];

        // Find report id in the array
        for (uint8_t i = 0; i < rpt_count; i++) {
            if (rpt_id == rpt_info_arr[i].report_id) {
                rpt_info = &rpt_info_arr[i];
                break;
            }
        }

        bytes++;
    }

    if (!rpt_info) {
        LOG_ERROR("Couldn't find the report info for this report. rpt_count=%d, rpt_info_arr[0].report_id=%d\n", rpt_count, rpt_info_arr[0].report_id);
        printf("Report: ");
        hex_dump(&report, sizeof(report));
        printf("hid_info: ");
        hex_dump(&hid_info, sizeof(hid_info));
        return;
    }

    LOG_TRACE("usage_page=%x, usage=%x\n", rpt_info->usage_page, rpt_info->usage);

    // For complete list of Usage Page & Usage checkout src/class/hid/hid.h. For examples:
    // - Keyboard                     : Desktop, Keyboard
    // - Mouse                        : Desktop, Mouse
    // - Gamepad                      : Desktop, Gamepad
    // - Consumer Control (Media Key) : Consumer, Consumer Control
    // - System Control (Power key)   : Desktop, System Control
    // - Generic (vendor)             : 0xFFxx, xx
    if (rpt_info->usage_page == HID_USAGE_PAGE_DESKTOP) {
        switch (rpt_info->usage) {
            case HID_USAGE_DESKTOP_KEYBOARD:
                handle_keyboard_report((void *) bytes);
                break;

            case HID_USAGE_DESKTOP_MOUSE:
                handle_mouse_report((void *) bytes);
                break;

            default: {
                LOG_ERROR("TODO - I don't think this is going to work\n");
                // Generic HID passthrough - use realtime to avoid blocking
                add_to_host_queue_realtime(report.instance, 99, report.len, bytes);
                break;
            }
        }
    }
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    LOG_DEBUG("%d,%d: tuh_hid_report_received_cb: itf_protocol=%d on core %d\n", dev_addr, instance, tuh_hid_interface_protocol(dev_addr, instance), get_core_num());

    // Bounds check (defense in depth)
    if (len > sizeof(union hid_reports)) {
        LOG_ERROR("Discarding report with size %d (max is %d)\n", len, sizeof(union hid_reports));
        return;
    }

    // DIAGNOSTIC: Print raw report IMMEDIATELY from PIO-USB buffer (before any processing)
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD && len >= 8) {
        printf("USB_RX: [%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x]\n",
               report[0], report[1], report[2], report[3],
               report[4], report[5], report[6], report[7]);
        stdio_flush();  // Force immediate output
    }

    // CRITICAL: Copy data from USB buffer IMMEDIATELY with interrupt protection
    // This must be done FIRST to minimize window where buffer could be reused
    hid_report_t to_tud;
    to_tud.instance = instance;
    to_tud.dev_addr = dev_addr;
    to_tud.len = len;

    // Redundant bounds check right before copy (paranoid defense)
    if (len > sizeof(to_tud.data)) {
        LOG_ERROR("CRITICAL: len %d exceeds to_tud.data size %d\n", len, sizeof(to_tud.data));
        return;
    }

    // Copy data from USB buffer IMMEDIATELY to minimize window where buffer could be reused
    memcpy(&to_tud.data, report, len);

#if 0  // DIAGNOSTIC CODE - disabled to avoid spinlock contention in interrupt handler
    // Data validation: Check for suspicious/corrupted data
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD && len >= 8) {
        hid_keyboard_report_t *kb = &to_tud.data.kb;
        // Validate modifier byte (only lower 8 bits are valid modifiers)
        if (kb->modifier & 0x00) {  // All modifiers are in lower byte
            // Check keycodes are in valid range (0x00-0xE7)
            for (int i = 0; i < 6; i++) {
                if (kb->keycode[i] > 0xE7 && kb->keycode[i] != 0) {
                    LOG_ERROR("CORRUPT: Invalid keycode[%d]=0x%02x in report\n", i, kb->keycode[i]);
                }
            }
        }
    }
#endif

    // Count keyboard reports received from physical keyboard (lightweight counter, no locks)
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        keystrokes_received_from_physical++;
#if 0  // DIAGNOSTIC CODE - disabled to avoid spinlock contention in interrupt handler
        // Log to diagnostic buffer (from our copy)
        diag_log_keystroke(&diag_received_buffer, keystrokes_received_from_physical, &to_tud.data.kb);
#endif
    }

    // Queue operations (using our copy) - non-blocking to avoid stalling interrupt handler
    if (!queue_try_add(&keyboard_to_tud_queue, &to_tud)) {
        // Queue full - drop oldest entry to make room (realtime mode)
        hid_report_t discard;
        if (queue_try_remove(&keyboard_to_tud_queue, &discard)) {
            queue_drops_realtime++;
            LOG_ERROR("Queue full, dropped oldest report (drops=%lu)\n", (unsigned long)queue_drops_realtime);
        }
        // Try again
        if (!queue_try_add(&keyboard_to_tud_queue, &to_tud)) {
            LOG_ERROR("CRITICAL: Still can't add to queue after drop!\n");
        }
    }

    // continue to request to receive report
    if (!tuh_hid_receive_report(dev_addr, instance)) {
        LOG_ERROR("Error: cannot request report\r\n");
    }
}


void next_report(hid_report_t report) {
    uint8_t const itf_protocol = tuh_hid_interface_protocol(report.dev_addr, report.instance);

    LOG_DEBUG("next_report: %d,%d itf_protocol=%d on core %d\n", report.dev_addr, report.instance, itf_protocol, get_core_num());
    LOG_TRACE("report.data.kb.keycode[0]=%x\n", report.data.kb.keycode[0]);

    switch (itf_protocol) {
        case HID_ITF_PROTOCOL_KEYBOARD:
            handle_keyboard_report(&report.data.kb);
            return;

        case HID_ITF_PROTOCOL_MOUSE:
            handle_mouse_report(&report);
            return;

        default:
            // Generic report requires matching ReportID and contents with previous parsed report info
            handle_generic_report(report);
            break;
    }
}
