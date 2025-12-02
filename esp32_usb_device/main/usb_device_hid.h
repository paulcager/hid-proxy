/**
 * USB Device HID Handler Header
 */

#ifndef USB_DEVICE_HID_H
#define USB_DEVICE_HID_H

/**
 * Initialize USB Device HID support
 *
 * Sets up TinyUSB device stack and creates tasks to:
 * - Receive HID reports from UART
 * - Forward reports to USB host (PC)
 */
void usb_device_hid_init(void);

#endif // USB_DEVICE_HID_H
