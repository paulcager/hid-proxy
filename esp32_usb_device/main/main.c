/**
 * ESP32-S3 UART to USB Device Passthrough (PoC)
 *
 * Simple proof-of-concept that:
 * 1. Receives HID reports from UART (from ESP32-S3 #1)
 * 2. Acts as USB keyboard/mouse to PC
 *
 * Hardware:
 * - ESP32-S3-DevKitC-1 or similar
 * - USB cable to PC (for USB device and power)
 * - UART connection on GPIO17 (TX) / GPIO18 (RX) to ESP32-S3 #1
 *
 * Build:
 *   idf.py set-target esp32s3
 *   idf.py build
 *   idf.py -p /dev/ttyUSB0 flash monitor
 */

#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "uart_protocol.h"
#include "usb_device_hid.h"

static const char *TAG = "main";

// GPIO pin configuration for UART (must match ESP32-S3 #1)
#define UART_NUM        UART_NUM_1
#define UART_TX_PIN     GPIO_NUM_3
#define UART_RX_PIN     GPIO_NUM_4

void app_main(void) {
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "ESP32-S3 UART to USB Device Passthrough PoC");
    ESP_LOGI(TAG, "=================================================");

    // Initialize UART protocol (receiver mode)
    ESP_LOGI(TAG, "Initializing UART on pins TX=%d, RX=%d", UART_TX_PIN, UART_RX_PIN);
    esp_err_t err = uart_protocol_init(UART_NUM, UART_TX_PIN, UART_RX_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UART: %s", esp_err_to_name(err));
        return;
    }

    // Initialize USB Device with HID support
    ESP_LOGI(TAG, "Initializing USB Device (Keyboard + Mouse)...");
    usb_device_hid_init();

    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "Setup complete!");
    ESP_LOGI(TAG, "1. Connect this ESP32-S3 to PC via USB");
    ESP_LOGI(TAG, "2. Connect UART to ESP32-S3 #1:");
    ESP_LOGI(TAG, "   - GPIO3 (TX) -> ESP32 #1 GPIO4 (RX)");
    ESP_LOGI(TAG, "   - GPIO4 (RX) -> ESP32 #1 GPIO3 (TX)");
    ESP_LOGI(TAG, "   - GND -> GND");
    ESP_LOGI(TAG, "3. Device should appear as HID keyboard/mouse");
    ESP_LOGI(TAG, "=================================================");

    // Main loop - just keep alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
