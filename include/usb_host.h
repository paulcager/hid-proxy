//
// Created by paul on 14/02/25.
//

#ifndef HID_PROXY_USB_HOST_H
#define HID_PROXY_USB_HOST_H

#include <stdint.h>
#include <stdbool.h>

extern void core1_main();

// Debug: track if any USB device has ever been detected
extern volatile bool usb_device_ever_mounted;
extern volatile uint32_t usb_mount_count;

#endif //HID_PROXY_USB_HOST_H
