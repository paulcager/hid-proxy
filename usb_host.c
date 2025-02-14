//
// Created by paul on 14/02/25.
//

#include "nfc_tag.h"
#include "hid_proxy.h"
#include "tusb.h"
#include "pio_usb.h"
#include "pico/util/queue.h"
#include <pico/flash.h>
#include "pico.h"
#include <stdio.h>
#include "usb_host.h"
#include "hid_proxy.h"

_Noreturn void core1_loop();

void handle_mouse_report(hid_report_t *report);

static void handle_generic_report(hid_report_t report);

#define MAX_REPORT  2
static struct {
    uint8_t report_count;
    tuh_hid_report_info_t report_info[MAX_REPORT];
} hid_info[CFG_TUH_HID];



// core1: handle host events
void core1_main() {
    flash_safe_execute_core_init();

    // Use tuh_configure() to pass pio configuration to the host stack
    // Note: tuh_configure() must be called before

    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    // Use GPIO2/3 for USB, leaving 0/1 available for UART if needed.
    pio_cfg.pin_dp = 2;
    LOG_INFO("pio_cfg.pin_dp = %d\n", pio_cfg.pin_dp);
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

    bool ok = tuh_init(1);
    if (!ok) {
        panic("tuh_init");
    }

    LOG_INFO("tuh running\n");
    core1_loop();
}

_Noreturn void core1_loop() {
    while (true) {
        tuh_task(); // tinyusb host task
        uint8_t leds;
        if (queue_try_remove(&leds_queue, &leds)) {
            tuh_hid_set_report(1, 0, 0, HID_REPORT_TYPE_OUTPUT, &leds, sizeof(leds));
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

    // Interface protocol (hid_interface_protocol_enum_t)
    const char *protocol_str[] = {"None", "Keyboard", "Mouse"};
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    LOG_INFO("[%04x:%04x][%u] HID Interface%u, Protocol = %s\r\n", vid, pid, dev_addr, instance, protocol_str[itf_protocol]);

    // By default host stack will use activate boot protocol on supported interface.
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
}

void handle_mouse_report(hid_report_t *report) {
    // Log, but otherwise send straight to the host.
    hid_mouse_report_t mouse = report->data.mouse;
    char l = mouse.buttons & MOUSE_BUTTON_LEFT ? 'L' : '-';
    char m = mouse.buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-';
    char r = mouse.buttons & MOUSE_BUTTON_RIGHT ? 'R' : '-';

    (void) l; (void) m; (void) r;

    LOG_DEBUG("[%u] %c%c%c %4d %4d %4d\n", (*report).dev_addr, l, m, r, (*report).data.mouse.x, (*report).data.mouse.y, (*report).data.mouse.wheel);
    // TODO add_to_host_queue(report->instance, REPORT_ID_MOUSE, len, &report->data);
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
                add_to_host_queue(report.instance, 99, report.len, bytes);
                break;
            }
        }
    }
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    (void) len;

    LOG_DEBUG("tuh_hid_report_received_cb: itf_protocol=%d on core %d\n", tuh_hid_interface_protocol(dev_addr, instance), get_core_num());

    if (len > sizeof(union hid_reports)) {
        LOG_ERROR("Discarding report with size %d (max is %d)\n", len, sizeof(union hid_reports));
        return;
    }

    hid_report_t to_tud;
    to_tud.instance = instance;
    to_tud.dev_addr = dev_addr;
    to_tud.len = len;
    memcpy(&to_tud.data, report, len);

    queue_add_blocking(&keyboard_to_tud_queue, &to_tud);

    // continue to request to receive report
    if (!tuh_hid_receive_report(dev_addr, instance)) {
        LOG_ERROR("Error: cannot request report\r\n");
    }
}


void next_report(hid_report_t report) {
    uint8_t const itf_protocol = tuh_hid_interface_protocol(report.dev_addr, report.instance);

    LOG_TRACE("next_report: itf_protocol=%d on core %d\n", itf_protocol, get_core_num());
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
