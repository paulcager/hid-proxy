/**
 * USB Host HID Handler
 *
 * Handles USB HID keyboard and mouse devices, forwarding reports to UART
 */

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "usb/usb_host.h"
#include "class/hid/hid_host.h"
#include "uart_protocol.h"

static const char *TAG = "usb_host_hid";

// Event flags for USB host events
#define USB_HOST_READY_BIT BIT0
static EventGroupHandle_t usb_host_event_group;

// USB Host Library event handler
static void usb_host_lib_event_handler(const usb_host_lib_info_t *info, void *user_ctx) {
    if (info->new_dev_address != 0) {
        ESP_LOGI(TAG, "USB device connected at address %d", info->new_dev_address);
    }
}

// HID Host interface callback
static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                        const hid_host_interface_event_t event,
                                        void *arg) {
    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
            ESP_LOGI(TAG, "HID input report received");
            break;
        }
        case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HID device disconnected");
            break;
        case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
            ESP_LOGW(TAG, "HID transfer error");
            break;
        default:
            break;
    }
}

// HID Host device event callback
static void hid_host_device_event(hid_host_device_handle_t hid_device_handle,
                                  const hid_host_driver_event_t event,
                                  void *arg) {
    switch (event) {
        case HID_HOST_DRIVER_EVENT_CONNECTED: {
            hid_host_dev_params_t dev_params;
            ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

            const char *protocol_name = (dev_params.proto == HID_PROTOCOL_KEYBOARD) ? "Keyboard" :
                                        (dev_params.proto == HID_PROTOCOL_MOUSE) ? "Mouse" : "None";

            ESP_LOGI(TAG, "HID Device connected: VID=0x%04X, PID=0x%04X, Protocol=%s",
                     dev_params.vid, dev_params.pid, protocol_name);

            // Claim the interface and start receiving reports
            ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle));
            ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));

            break;
        }
        default:
            break;
    }
}

// HID keyboard report callback - forward to UART
static void hid_keyboard_report_callback(const uint8_t *const data, const int length) {
    if (length == 8) {  // Standard HID keyboard report is 8 bytes
        esp_err_t err = uart_send_packet(PKT_KEYBOARD_REPORT, data, length);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Sent keyboard report: mod=%02X keys=[%02X %02X %02X %02X %02X %02X]",
                     data[0], data[2], data[3], data[4], data[5], data[6], data[7]);
        } else {
            ESP_LOGE(TAG, "Failed to send keyboard report over UART");
        }
    } else {
        ESP_LOGW(TAG, "Unexpected keyboard report length: %d", length);
    }
}

// HID mouse report callback - forward to UART
static void hid_mouse_report_callback(const uint8_t *const data, const int length) {
    if (length >= 3) {  // Minimum mouse report is 3 bytes (buttons, x, y)
        esp_err_t err = uart_send_packet(PKT_MOUSE_REPORT, data, length);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Sent mouse report: buttons=%02X x=%d y=%d",
                     data[0], (int8_t)data[1], (int8_t)data[2]);
        } else {
            ESP_LOGE(TAG, "Failed to send mouse report over UART");
        }
    } else {
        ESP_LOGW(TAG, "Unexpected mouse report length: %d", length);
    }
}

// USB Host task
static void usb_host_task(void *arg) {
    ESP_LOGI(TAG, "USB Host task starting");

    // Install USB Host Library
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    // Create task for USB Host Library event handling
    xTaskCreate(
        [](void *arg) {
            while (1) {
                usb_host_lib_handle_events(portMAX_DELAY, NULL);
            }
        },
        "usb_events", 4096, NULL, 2, NULL);

    // Install HID Host driver
    const hid_host_driver_config_t hid_host_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_host_device_event,
        .callback_arg = NULL
    };
    ESP_ERROR_CHECK(hid_host_install(&hid_host_config));

    // Register HID interface callbacks for keyboard and mouse
    const hid_host_device_config_t dev_config = {
        .callback = hid_host_interface_callback,
        .callback_arg = NULL
    };

    // Register protocol callbacks
    ESP_ERROR_CHECK(hid_host_claim(HID_PROTOCOL_KEYBOARD, &dev_config));
    ESP_ERROR_CHECK(hid_host_claim(HID_PROTOCOL_MOUSE, &dev_config));

    // Register report callbacks
    hid_host_interface_set_report_callback(HID_PROTOCOL_KEYBOARD,
                                          HID_REPORT_TYPE_INPUT,
                                          hid_keyboard_report_callback);
    hid_host_interface_set_report_callback(HID_PROTOCOL_MOUSE,
                                          HID_REPORT_TYPE_INPUT,
                                          hid_mouse_report_callback);

    ESP_LOGI(TAG, "USB Host initialized, waiting for HID devices...");
    xEventGroupSetBits(usb_host_event_group, USB_HOST_READY_BIT);

    // Keep task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void usb_host_hid_init(void) {
    usb_host_event_group = xEventGroupCreate();
    xTaskCreate(usb_host_task, "usb_host", 8192, NULL, 5, NULL);

    // Wait for USB host to be ready
    xEventGroupWaitBits(usb_host_event_group, USB_HOST_READY_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "USB Host ready");
}
