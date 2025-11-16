/*
 * wifi_console.h
 *
 * Serial console for WiFi configuration
 */

#ifndef HID_PROXY_WIFI_CONSOLE_H
#define HID_PROXY_WIFI_CONSOLE_H

/*! \brief Interactive WiFi configuration via serial console
 *
 * Prompts user for WiFi SSID, password, and country code via UART.
 * Saves configuration to kvstore for use on next boot.
 *
 * This function blocks while waiting for user input (with timeouts).
 * Should only be called when user explicitly requests WiFi setup.
 */
void wifi_console_setup(void);

#endif // HID_PROXY_WIFI_CONSOLE_H
