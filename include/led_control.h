#ifndef HID_PROXY_LED_CONTROL_H
#define HID_PROXY_LED_CONTROL_H

#include <stdbool.h>

/**
 * @brief Initialize the built-in LED
 *
 * Detects hardware at runtime and initializes either:
 * - GPIO25 LED on plain Pico/Pico2
 * - CYW43 LED on Pico W/Pico2 W
 *
 * This function must be called after wifi_init() has attempted to
 * initialize the CYW43 chip (if building for pico_w).
 */
void led_init(void);

/**
 * @brief Set built-in LED state
 *
 * @param on true to turn LED on, false to turn off
 */
void led_set(bool on);

/**
 * @brief Check if CYW43 WiFi chip is present
 *
 * @return true if CYW43 chip detected, false if using GPIO25
 */
bool led_is_cyw43_available(void);

#endif // HID_PROXY_LED_CONTROL_H
