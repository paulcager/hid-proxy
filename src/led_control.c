#include "led_control.h"
#include "logging.h"
#include "pico/stdlib.h"

#ifdef PICO_CYW43_SUPPORTED
#include "pico/cyw43_arch.h"
#include "wifi_config.h"
#endif

// Built-in LED pin on non-WiFi boards
#define BUILTIN_LED_PIN 25

// Runtime detection: is CYW43 WiFi chip present?
static bool cyw43_available = false;
static bool led_initialized = false;

void led_init(void) {
    if (led_initialized) {
        return;
    }

#ifdef PICO_CYW43_SUPPORTED
    // Check if CYW43 was successfully initialized by wifi_init()
    // wifi_is_initialized() returns true if cyw43_arch_init() succeeded
    if (wifi_is_initialized()) {
        // CYW43 chip is present and initialized
        cyw43_available = true;
        LOG_INFO("LED control: Using CYW43 LED (Pico W hardware detected)\n");
    } else {
        // CYW43 not present, use GPIO25
        cyw43_available = false;
        LOG_INFO("LED control: Using GPIO25 (plain Pico hardware detected)\n");
        gpio_init(BUILTIN_LED_PIN);
        gpio_set_dir(BUILTIN_LED_PIN, GPIO_OUT);
    }
#else
    // Build without CYW43 support - always use GPIO25
    cyw43_available = false;
    LOG_INFO("LED control: Using GPIO25 (no CYW43 support in build)\n");
    gpio_init(BUILTIN_LED_PIN);
    gpio_set_dir(BUILTIN_LED_PIN, GPIO_OUT);
#endif

    led_initialized = true;
}

void led_set(bool on) {
    if (!led_initialized) {
        LOG_ERROR("LED control: led_set() called before led_init()\n");
        return;
    }

#ifdef PICO_CYW43_SUPPORTED
    if (cyw43_available) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
        return;
    }
#endif

    // Use GPIO25 (either because CYW43 not available, or build without CYW43)
    gpio_put(BUILTIN_LED_PIN, on ? 1 : 0);
}

bool led_is_cyw43_available(void) {
    return cyw43_available;
}
