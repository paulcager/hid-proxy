#include "led_control.h"
#include "logging.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"

#ifdef PICO_CYW43_SUPPORTED
#include "pico/cyw43_arch.h"
#include "wifi_config.h"
#endif

// Built-in LED pin on non-WiFi boards
#define BUILTIN_LED_PIN 25

// Runtime detection: is CYW43 WiFi chip present?
static bool cyw43_available = false;
static bool led_initialized = false;

// NumLock LED state management
static uint8_t current_led_state = 0;       // Current LED state to send to keyboard
static uint8_t host_led_state = 0;          // LED state from host PC (CAPS_LOCK, SCROLL_LOCK, etc.)
static absolute_time_t next_led_toggle;     // When to toggle LED next
static uint32_t led_on_interval_ms = 0;     // How long LED stays on (ms)
static uint32_t led_off_interval_ms = 0;    // How long LED stays off (ms)
static bool boot_complete = false;          // Set to true after boot message is printed
static queue_t *leds_queue_ptr = NULL;      // Queue to send LED updates to physical keyboard

void led_set_queue(queue_t *queue) {
    leds_queue_ptr = queue;
}

void led_set_host_state(uint8_t leds) {
    host_led_state = leds;
}

void led_boot_complete(void) {
    boot_complete = true;
}

void led_set_intervals(uint32_t on_ms, uint32_t off_ms) {
    led_on_interval_ms = on_ms;
    led_off_interval_ms = off_ms;
}

uint8_t led_get_state(void) {
    return current_led_state;
}

void update_status_led(void) {
    // Track what we last sent to avoid queue spam (CRITICAL for USB stability!)
    static uint8_t last_sent_state = 0xFF;  // Invalid initial state forces first send

    // Keep LEDs ON until boot message is displayed (power-on indicator)
    if (!boot_complete) {
        led_set(true);  // On-board LED ON
        current_led_state = 0x01;  // NumLock LED ON
        if (current_led_state != last_sent_state && leds_queue_ptr) {
            queue_try_add(leds_queue_ptr, &current_led_state);
            last_sent_state = current_led_state;
        }
        return;
    }

    // Keep LED ON until a keyboard is connected
    extern volatile bool usb_device_ever_mounted;
    if (!usb_device_ever_mounted) {
        // No keyboard connected yet - keep LED ON
        led_set(true);
        return;
    }

    // Compute our desired NUM_LOCK state (bit 0)
    bool numlock_on;
    if (led_on_interval_ms == 0 && led_off_interval_ms == 0) {
        // LED off (locked/blank state)
        numlock_on = false;
    } else if (time_reached(next_led_toggle)) {
        // Toggle Num Lock LED (bit 0)
        numlock_on = !(current_led_state & 0x01);  // Toggle the bit

        // Use different interval based on new state
        uint32_t next_interval = numlock_on ? led_on_interval_ms : led_off_interval_ms;
        next_led_toggle = make_timeout_time_ms(next_interval);
    } else {
        // Keep current NUM_LOCK state
        numlock_on = (current_led_state & 0x01) != 0;
    }

    // Merge: Take host's CAPS_LOCK/SCROLL_LOCK (bits 1,2) + our NUM_LOCK (bit 0)
    // LED bits: 0=NUM_LOCK, 1=CAPS_LOCK, 2=SCROLL_LOCK, 3=COMPOSE, 4=KANA
    current_led_state = (host_led_state & 0xFE) | (numlock_on ? 0x01 : 0x00);

    // Update built-in LED to mirror NumLock state (bit 0)
    bool led_on = (current_led_state & 0x01) != 0;
    led_set(led_on);

    // CRITICAL FIX: Only send to physical keyboard when state actually changes!
    // Previously this ran unconditionally (~1000 times/sec), causing USB transaction
    // interference on Core 1 which led to keystroke corruption. Now only sends
    // when LED state changes (~0.4 times/sec), reducing queue traffic by 99.96%.
    if (current_led_state != last_sent_state && leds_queue_ptr) {
        queue_try_add(leds_queue_ptr, &current_led_state);
        last_sent_state = current_led_state;
    }
}

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
