/**
 * USB Device HID Handler
 *
 * Receives HID reports from UART and forwards to USB host (PC)
 * Integrated with state machine for command processing
 */

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "tinyusb.h"
#include "tusb.h"
#include "class/hid/hid_device.h"
#include "uart_protocol.h"
#include "state_machine.h"

static const char *TAG = "usb_device_hid";

// Queue for HID reports from UART (processed in TinyUSB task context)
static QueueHandle_t keyboard_report_queue;
static QueueHandle_t mouse_report_queue;

#define REPORT_QUEUE_SIZE 32

// HID interface indices (must match usb_descriptors.c)
#define ITF_NUM_KEYBOARD 0
#define ITF_NUM_MOUSE    1

//--------------------------------------------------------------------+
// HID Report Descriptors
//--------------------------------------------------------------------+

// Keyboard report descriptor (standard boot protocol)
static uint8_t const desc_hid_keyboard_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

// Mouse report descriptor (standard boot protocol)
static uint8_t const desc_hid_mouse_report[] = {
    TUD_HID_REPORT_DESC_MOUSE()
};

// Invoked when received GET HID REPORT DESCRIPTOR
uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance) {
    return (instance == 0) ? desc_hid_keyboard_report : desc_hid_mouse_report;
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    return 0;  // Not implemented
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    (void) report_id;

    // Handle LED updates from host (Num Lock, Caps Lock, etc.)
    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        if (instance == ITF_NUM_KEYBOARD && bufsize >= 1) {
            uint8_t leds = buffer[0];
            ESP_LOGI(TAG, "Keyboard LEDs: NumLock=%d CapsLock=%d ScrollLock=%d",
                     (leds & 0x01) ? 1 : 0,
                     (leds & 0x02) ? 1 : 0,
                     (leds & 0x04) ? 1 : 0);

            // TODO: Send LED update back to ESP32 #1 via UART
            // uart_send_packet(PKT_LED_UPDATE, &leds, 1);
        }
    }
}

//--------------------------------------------------------------------+
// UART to USB Forwarding
//--------------------------------------------------------------------+

// UART receiver task - reads packets and queues HID reports
static void uart_rx_task(void *arg) {
    uart_packet_t packet;
    ESP_LOGI(TAG, "UART RX task started");

    while (1) {
        // Blocking receive from UART
        esp_err_t err = uart_recv_packet(&packet, portMAX_DELAY);

        if (err == ESP_OK) {
            switch (packet.type) {
                case PKT_KEYBOARD_REPORT: {
                    if (packet.length == 8) {
                        // Process through state machine instead of direct queue
                        hid_keyboard_report_t *report = (hid_keyboard_report_t *)packet.payload;
                        handle_keyboard_report(report);
                    } else {
                        ESP_LOGW(TAG, "Invalid keyboard report length: %d", packet.length);
                    }
                    break;
                }

                case PKT_MOUSE_REPORT: {
                    if (packet.length >= 3 && packet.length <= 5) {
                        // Queue mouse report for sending
                        if (xQueueSend(mouse_report_queue, packet.payload, 0) != pdTRUE) {
                            ESP_LOGW(TAG, "Mouse queue full, dropping report");
                        }
                    } else {
                        ESP_LOGW(TAG, "Invalid mouse report length: %d", packet.length);
                    }
                    break;
                }

                case PKT_STATUS: {
                    // Log status messages
                    char msg[UART_MAX_PAYLOAD + 1] = {0};
                    memcpy(msg, packet.payload, packet.length);
                    ESP_LOGI(TAG, "Status from host: %s", msg);
                    break;
                }

                default:
                    ESP_LOGW(TAG, "Unknown packet type: 0x%02X", packet.type);
                    break;
            }
        } else if (err == ESP_ERR_TIMEOUT) {
            // Timeout is normal, just continue
            continue;
        } else if (err == ESP_ERR_INVALID_CRC) {
            ESP_LOGE(TAG, "UART packet checksum error");
        } else {
            ESP_LOGE(TAG, "UART receive error: %s", esp_err_to_name(err));
        }

        // Small delay to prevent task starvation
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// USB HID task - sends queued reports to USB host
static void usb_hid_task(void *arg) {
    uint8_t keyboard_report[8];
    uint8_t mouse_report[5];

    ESP_LOGI(TAG, "USB HID task started");

    while (1) {
        // Process TinyUSB stack
        tud_task();

        // Send keyboard reports if available and ready
        if (tud_hid_n_ready(ITF_NUM_KEYBOARD)) {
            if (xQueueReceive(keyboard_report_queue, keyboard_report, 0) == pdTRUE) {
                bool success = tud_hid_n_keyboard_report(ITF_NUM_KEYBOARD, 0,
                                                         keyboard_report[0],  // modifier
                                                         &keyboard_report[2]); // keycode[6]
                if (success) {
                    ESP_LOGD(TAG, "Sent keyboard report: mod=0x%02X keys=[%02X %02X %02X %02X %02X %02X]",
                             keyboard_report[0], keyboard_report[2], keyboard_report[3],
                             keyboard_report[4], keyboard_report[5], keyboard_report[6], keyboard_report[7]);
                } else {
                    ESP_LOGW(TAG, "Failed to send keyboard report");
                }
            }
        }

        // Send mouse reports if available and ready
        if (tud_hid_n_ready(ITF_NUM_MOUSE)) {
            if (xQueueReceive(mouse_report_queue, mouse_report, 0) == pdTRUE) {
                bool success = tud_hid_n_mouse_report(ITF_NUM_MOUSE, 0,
                                                      mouse_report[0],   // buttons
                                                      (int8_t)mouse_report[1],  // x
                                                      (int8_t)mouse_report[2],  // y
                                                      0,                 // vertical scroll
                                                      0);                // horizontal scroll
                if (success) {
                    ESP_LOGD(TAG, "Sent mouse report: buttons=0x%02X x=%d y=%d",
                             mouse_report[0], (int8_t)mouse_report[1], (int8_t)mouse_report[2]);
                } else {
                    ESP_LOGW(TAG, "Failed to send mouse report");
                }
            }
        }

        // Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

void usb_device_hid_init(void) {
    ESP_LOGI(TAG, "Initializing USB Device HID");

    // Create report queues
    keyboard_report_queue = xQueueCreate(REPORT_QUEUE_SIZE, 8);  // 8 bytes per keyboard report
    mouse_report_queue = xQueueCreate(REPORT_QUEUE_SIZE, 5);     // 5 bytes per mouse report

    if (!keyboard_report_queue || !mouse_report_queue) {
        ESP_LOGE(TAG, "Failed to create report queues");
        return;
    }

    // Initialize TinyUSB device stack
    ESP_LOGI(TAG, "Initializing TinyUSB device stack");
    const tinyusb_config_t tusb_cfg = {};  // Use default configuration

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    ESP_LOGI(TAG, "USB Device initialized - waiting for USB connection...");

    // Create tasks
    xTaskCreate(uart_rx_task, "uart_rx", 4096, NULL, 5, NULL);
    xTaskCreate(usb_hid_task, "usb_hid", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "USB Device HID ready");
}
