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
#include <hardware/spi.h>
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "pico/multicore.h"

#include "pio_usb.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include "hid_proxy.h"
#include "pn532-lib/pn532_rp2040.h"
#include "nfc_tag.h"
#include "encryption.h"


// Reminders:
// Latest is ~/pico/hid-proxy2/build
// make && openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000" -c "program $(ls *.elf) verify reset exit"
// minicom -b 115200 -o -D /dev/ttyACM0
// UART0 on standard ports and PIO-USB on GPIO2/3 (see CMakeLists.txt to change).

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+


#define MAX_REPORT  2
static struct {
    uint8_t report_count;
    tuh_hid_report_info_t report_info[MAX_REPORT];
} hid_info[CFG_TUH_HID];

kb_t kb;

static void handle_generic_report(hid_report_t report);

void core1_loop();

void handle_mouse_report(hid_report_t *report);

// A queue of events (hid_report_t) from the physical keyboard (PIO) to be processed
// in the device CPU.
// Elements: hid_report_t;
queue_t keyboard_to_tud_queue;

// A queue of events from the tud process to the real host.
// Elements: send_data_t
queue_t tud_to_physical_host_queue;

// A queue of events from the physical host, to be sent to the physical keyboard.
queue_t leds_queue;

/*------------- MAIN -------------*/

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

void core1_loop() {
    while (true) {
        tuh_task(); // tinyusb host task
        uint8_t leds;
        if (queue_try_remove(&leds_queue, &leds)) {
            tuh_hid_set_report(1, 0, 0, HID_REPORT_TYPE_OUTPUT, &leds, sizeof(leds));
        }
    }
}

// core0: handle device events
int main(void) {
    // default 125MHz is not appropriate. Sysclock should be multiple of 12MHz.
    set_sys_clock_khz(120000, true);

    stdio_init_all();

    flash_safe_execute_core_init();

    queue_init(&keyboard_to_tud_queue, sizeof(hid_report_t), 12);
    queue_init(&tud_to_physical_host_queue, sizeof(send_data_t), 256);
    queue_init(&leds_queue, 1, 4);

    kb.local_store = malloc(FLASH_STORE_SIZE);
    kb.status = locked;

    LOG_INFO("\n\nCore 0 (tud) running\n");

    nfc_setup();

    multicore_reset_core1();
    // all USB task run in core1
    multicore_launch_core1(core1_main);

    // init device stack on native usb (roothub port0)
    tud_init(0);

    absolute_time_t last_interaction = get_absolute_time();
    status_t previous_status = locked;


    while (true) {
        if (kb.status != previous_status) {
            LOG_INFO("State changed from %s to %s\n", status_string(previous_status), status_string(kb.status));
            previous_status = kb.status;
        }

        tud_task(); // tinyusb device task
        nfc_task(kb.status == locked);

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
            if (nfc_key_available()) {
                uint8_t key[16];
                nfc_get_key(key);
                printf("Setting key\n");
                hex_dump(key, 16);
                enc_set_key(key, sizeof(key));
                read_state(&kb);
            }
        }

        if (kb.status != locked && absolute_time_diff_us(last_interaction, get_absolute_time()) > 1000 * IDLE_TIMEOUT_MILLIS) {
            LOG_INFO("Timedout - clearing encrypted data\n");
            memset(kb.local_store, 0, FLASH_STORE_SIZE);
            kb.status = locked;
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

//--------------------------------------------------------------------+
// Host HID
//--------------------------------------------------------------------+

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
    // Therefore for this simple example, we only need to parse generic report descriptor (with built-in parser)
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

void handle_mouse_report(hid_report_t *report) {
    // Log, but otherwise send straight to the host.
    hid_mouse_report_t mouse = report->data.mouse;
    char l = mouse.buttons & MOUSE_BUTTON_LEFT ? 'L' : '-';
    char m = mouse.buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-';
    char r = mouse.buttons & MOUSE_BUTTON_RIGHT ? 'R' : '-';

    LOG_DEBUG("[%u] %c%c%c %4d %4d %4d\n", (*report).dev_addr, l, m, r, (*report).data.mouse.x, (*report).data.mouse.y, (*report).data.mouse.wheel);
    // TODO add_to_host_queue(report->instance, REPORT_ID_MOUSE, len, &report->data);

    return;
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

void queue_add_or_panic(queue_t *q, const void *data) {
    if (!queue_try_add(q, data)) {
        // TODO - this is most likely to happen if we are sending large definitions
        // more quickly than they can be sent over USB. Fix this by interleaving
        // queue puts with tud_task and using blocking adds.
        panic("Queue is full");
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

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
    (void) instance;

    LOG_INFO("tud_hid_set_report_cb[%x]  %x %x (size=%d)\n", instance, report_id, report_type, bufsize);

    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        // Set keyboard LED e.g Capslock, Numlock etc...
        if (report_id == REPORT_ID_KEYBOARD) {
            // bufsize should be (at least) 1
            if (bufsize < 1) return;

            uint8_t const kbd_leds = buffer[0];
            LOG_DEBUG("leds: %x\n", kbd_leds);
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
