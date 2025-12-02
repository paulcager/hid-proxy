/**
 * ESP32-S3 USB Host to UART Passthrough (PoC)
 *
 * Simple proof-of-concept that:
 * 1. Acts as USB host for HID keyboards/mice
 * 2. Forwards HID reports over UART using simple packet protocol
 *
 * Hardware:
 * - ESP32-S3-DevKitC-1 or similar
 * - USB OTG cable for keyboard connection
 * - UART connection on GPIO3 (TX) / GPIO4 (RX)
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
#include "uart_protocol.h"
#include "usb_host_hid.h"

static const char *TAG = "main";

// GPIO pin configuration for UART
#define UART_NUM        UART_NUM_1
#define UART_TX_PIN     GPIO_NUM_3
#define UART_RX_PIN     GPIO_NUM_4

void app_main(void) {
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "ESP32-S3 USB Host to UART Passthrough PoC");
    ESP_LOGI(TAG, "=================================================");

    // Initialize UART protocol
    ESP_LOGI(TAG, "Initializing UART on pins TX=%d, RX=%d", UART_TX_PIN, UART_RX_PIN);
    esp_err_t err = uart_protocol_init(UART_NUM, UART_TX_PIN, UART_RX_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UART: %s", esp_err_to_name(err));
        return;
    }

    // Send a test packet to verify UART is working
    const char *test_msg = "USB Host Ready";
    uart_send_packet(PKT_STATUS, test_msg, strlen(test_msg));
    ESP_LOGI(TAG, "Sent test packet over UART");

    // Initialize USB Host with HID support
    ESP_LOGI(TAG, "Initializing USB Host for HID devices...");
    usb_host_hid_init();

    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "Setup complete! Connect a USB keyboard or mouse.");
    ESP_LOGI(TAG, "HID reports will be forwarded to UART at 921600 baud");
    ESP_LOGI(TAG, "=================================================");

    // Main loop - just keep alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
