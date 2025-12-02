/**
 * USB Host HID Handler Header
 */

#ifndef USB_HOST_HID_H
#define USB_HOST_HID_H

/**
 * Initialize USB Host HID support
 *
 * Sets up USB host library and registers HID keyboard/mouse handlers
 * that forward reports to UART
 */
void usb_host_hid_init(void);

#endif // USB_HOST_HID_H
