#ifndef HID_PROXY_LED_CONTROL_H
#define HID_PROXY_LED_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "pico/util/queue.h"

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
 * @brief Set the queue for sending LED updates to physical keyboard
 *
 * @param queue Pointer to the leds_queue
 */
void led_set_queue(queue_t *queue);

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

/**
 * @brief Update NumLock LED state and built-in LED
 *
 * Called from main loop to update LED status based on device state.
 * Handles boot-time LED behavior, NumLock LED pulsing, and mirroring
 * to the built-in LED.
 */
void update_status_led(void);

/**
 * @brief Mark boot as complete
 *
 * Called after the boot status message is printed. LEDs will transition
 * from "boot in progress" mode to normal status indication.
 */
void led_boot_complete(void);

/**
 * @brief Set LED timing intervals
 *
 * @param on_ms How long LED stays on (milliseconds), 0 = always off
 * @param off_ms How long LED stays off (milliseconds)
 */
void led_set_intervals(uint32_t on_ms, uint32_t off_ms);

/**
 * @brief Get current LED state
 *
 * @return Current LED state byte (bit 0 = NumLock)
 */
uint8_t led_get_state(void);

#endif // HID_PROXY_LED_CONTROL_H
