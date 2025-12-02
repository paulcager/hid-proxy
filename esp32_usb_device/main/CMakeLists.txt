idf_component_register(SRCS "main.c" "usb_device_hid.c" "state_machine.c"
                       REQUIRES uart_protocol espressif__esp_tinyusb espressif__tinyusb freertos)
