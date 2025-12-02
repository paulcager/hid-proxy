idf_component_register(SRCS "main.c" "usb_device_hid.c" "usb_descriptors.c"
                       INCLUDE_DIRS "."
                       REQUIRES uart_protocol esp_tinyusb freertos)
