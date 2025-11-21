#ifndef WS2812_LED_H
#define WS2812_LED_H

#include <stdint.h>
#include <stdbool.h>
#include "hid_proxy.h"

// WS2812 RGB LED support for Waveshare RP2350-USB-A board
// LED is connected to GPIO16

// LED color definitions for status indication
typedef enum {
    LED_OFF = 0,
    LED_RED,           // Device locked
    LED_GREEN,         // Device unlocked (normal)
    LED_BLUE,          // Entering password / defining key
    LED_YELLOW,        // Command mode (both shifts)
    LED_PURPLE,        // NFC operation
    LED_WHITE_DIM,     // Idle/suspended
    LED_ORANGE,        // Error state
    LED_RAINBOW        // Web access enabled (animated)
} led_color_t;

/**
 * Initialize the WS2812 RGB LED
 * Must be called before any other LED functions
 * @return true if initialization succeeded, false on error
 */
bool ws2812_led_init(void);

/**
 * Set the LED to a specific color
 * @param color The color to display
 */
void ws2812_led_set_color(led_color_t color);

/**
 * Update LED based on device status
 * Call this from main loop when status changes
 * @param status Current device status
 * @param web_access_enabled Whether web access is currently enabled
 */
void ws2812_led_update_status(status_t status, bool web_access_enabled);

/**
 * Task function to handle animated LED effects (e.g. rainbow pulse)
 * Call this periodically from main loop
 */
void ws2812_led_task(void);

/**
 * Set LED to RGB value directly
 * @param r Red (0-255)
 * @param g Green (0-255)
 * @param b Blue (0-255)
 */
void ws2812_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * Show error indication (brief orange flash)
 */
void ws2812_led_show_error(void);

#endif // WS2812_LED_H
