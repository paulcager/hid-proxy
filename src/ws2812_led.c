/**
 * WS2812 RGB LED driver for Waveshare RP2350-USB-A board
 *
 * Provides visual status feedback using the onboard WS2812 RGB LED
 * connected to GPIO16.
 */

#include "ws2812_led.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"
#include "pico/time.h"
#include <stdio.h>

#define WS2812_PIN 16          // GPIO pin for WS2812 on Waveshare RP2350-USB-A
#define IS_RGBW false          // Standard RGB WS2812, not RGBW
#define WS2812_FREQ 800000     // 800kHz data rate for WS2812

// PIO resources (auto-selected during init)
static PIO led_pio = NULL;
static uint led_sm = 0;
static uint led_offset = 0;
static bool led_initialized = false;

// Animation state
static led_color_t current_color = LED_OFF;
static uint32_t rainbow_counter = 0;

/**
 * Convert RGB to GRB format (WS2812 expects GRB order)
 */
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)(r) << 8) |
           ((uint32_t)(g) << 16) |
           (uint32_t)(b);
}

/**
 * Send pixel data to WS2812
 */
static inline void put_pixel(uint32_t pixel_grb) {
    if (!led_initialized) return;
    pio_sm_put_blocking(led_pio, led_sm, pixel_grb << 8u);
}

/**
 * Initialize WS2812 LED
 */
bool ws2812_led_init(void) {
    // Automatically find a free PIO and state machine
    bool success = pio_claim_free_sm_and_add_program_for_gpio_range(
        &ws2812_program, &led_pio, &led_sm, &led_offset, WS2812_PIN, 1, true);

    if (!success) {
        printf("Failed to claim PIO/SM for WS2812 LED\n");
        return false;
    }

    ws2812_program_init(led_pio, led_sm, led_offset, WS2812_PIN, WS2812_FREQ, IS_RGBW);

    led_initialized = true;
    printf("WS2812 LED initialized on GPIO%d (PIO%d, SM%d)\n",
           WS2812_PIN, pio_get_index(led_pio), led_sm);

    // Start with LED off
    ws2812_led_set_color(LED_OFF);

    return true;
}

/**
 * Set LED to RGB value directly
 */
void ws2812_led_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    if (!led_initialized) return;
    put_pixel(urgb_u32(r, g, b));
}

/**
 * Set LED to predefined color
 */
void ws2812_led_set_color(led_color_t color) {
    current_color = color;

    switch (color) {
        case LED_OFF:
            ws2812_led_set_rgb(0, 0, 0);
            break;
        case LED_RED:
            ws2812_led_set_rgb(32, 0, 0);  // Dimmed for comfort
            break;
        case LED_GREEN:
            ws2812_led_set_rgb(0, 32, 0);
            break;
        case LED_BLUE:
            ws2812_led_set_rgb(0, 0, 32);
            break;
        case LED_YELLOW:
            ws2812_led_set_rgb(24, 24, 0);
            break;
        case LED_PURPLE:
            ws2812_led_set_rgb(24, 0, 24);
            break;
        case LED_WHITE_DIM:
            ws2812_led_set_rgb(8, 8, 8);
            break;
        case LED_ORANGE:
            ws2812_led_set_rgb(32, 16, 0);
            break;
        case LED_RAINBOW:
            // Animated in task function
            break;
    }
}

/**
 * Update LED based on device status
 */
void ws2812_led_update_status(status_t status, bool web_access_enabled) {
    if (!led_initialized) return;

    // Web access overrides other status (rainbow animation)
    if (web_access_enabled) {
        ws2812_led_set_color(LED_RAINBOW);
        return;
    }

    // Map status to LED color
    switch (status) {
        case blank:
        case blank_seen_magic:
            ws2812_led_set_color(LED_WHITE_DIM);
            break;

        case locked:
            ws2812_led_set_color(LED_RED);
            break;

        case locked_seen_magic:
        case locked_expecting_command:
            ws2812_led_set_color(LED_YELLOW);
            break;

        case entering_password:
        case entering_new_password:
            ws2812_led_set_color(LED_BLUE);
            break;

        case normal:
            ws2812_led_set_color(LED_GREEN);
            break;

        case seen_magic:
        case expecting_command:
        case seen_assign:
            ws2812_led_set_color(LED_YELLOW);
            break;

        case defining:
            ws2812_led_set_color(LED_BLUE);
            break;
    }
}

/**
 * Task function for animated LED effects
 */
void ws2812_led_task(void) {
    if (!led_initialized) return;

    // Only animate if in RAINBOW mode
    if (current_color != LED_RAINBOW) return;

    // Simple rainbow pulse effect
    rainbow_counter++;
    uint32_t t = rainbow_counter / 4;  // Slow down the animation

    // HSV to RGB conversion for smooth rainbow
    uint8_t hue = t & 0xFF;
    uint8_t region = hue / 43;
    uint8_t remainder = (hue - (region * 43)) * 6;

    uint8_t p = 0;
    uint8_t q = (16 * (255 - remainder)) / 255;
    uint8_t t_val = (16 * remainder) / 255;

    uint8_t r, g, b;
    switch (region) {
        case 0: r = 16; g = t_val; b = p; break;
        case 1: r = q; g = 16; b = p; break;
        case 2: r = p; g = 16; b = t_val; break;
        case 3: r = p; g = q; b = 16; break;
        case 4: r = t_val; g = p; b = 16; break;
        default: r = 16; g = p; b = q; break;
    }

    ws2812_led_set_rgb(r, g, b);
}

/**
 * Show error indication
 */
void ws2812_led_show_error(void) {
    if (!led_initialized) return;

    // Brief orange flash
    ws2812_led_set_color(LED_ORANGE);
    sleep_ms(200);

    // Restore previous color
    ws2812_led_set_color(current_color);
}
