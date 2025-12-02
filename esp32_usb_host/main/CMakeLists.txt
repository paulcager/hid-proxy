idf_component_register(SRCS "main.c" "usb_host_hid.c"
                       INCLUDE_DIRS "."
                       REQUIRES uart_protocol usb)
