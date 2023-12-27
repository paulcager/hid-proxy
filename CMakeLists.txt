cmake_minimum_required(VERSION 3.13)
set(PICO_SDK_PATH /home/paul/pico/pico-sdk)
include (pico_sdk_import.cmake)
project(hid_proxy)

pico_sdk_init()

add_subdirectory("./Pico-PIO-USB" pico_pio_usb)
add_subdirectory("./pico-arduino-compat/libs/adafruit-pn532")

add_executable(hid_proxy
        hid_proxy.c
        flash.c
        hid_proxy.h
        key_defs.c
        logging.h
        encryption.c
        encryption.h
        tiny-AES-c/aes.c
        # Adafruit-PN532/Adafruit_PN532.cpp
        sane.c
        nfc_tag.cpp
        nfc_tag.h
        pn532-lib/pn532.c
        pn532-lib/pn532_rp2040.c)

target_sources(hid_proxy PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/hid_proxy
        ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
        # I have copied the following from the samples, without understanding why.
        # can use 'tinyusb_pico_pio_usb' library later when pico-sdk is updated
        ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/dcd_pio_usb.c
        ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
        )

# Make sure TinyUSB can find tusb_config.h
target_include_directories(hid_proxy PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(hid_proxy PUBLIC
        pico_stdlib
        pico_flash
        pico_rand
        hardware_flash
        hardware_exception
        pico_pio_usb
        tinyusb_device
        tinyusb_board
        tinyusb_host
        pac-adafruit-pn532
)

pico_add_extra_outputs(hid_proxy)

pico_enable_stdio_usb(hid_proxy 0)
pico_enable_stdio_uart(hid_proxy 1)
# Can move the UART0 ports like so:
#target_compile_definitions(hid_proxy PRIVATE
#        PICO_DEFAULT_UART_RX_PIN=16
#        PICO_DEFAULT_UART_TX_PIN=17
#)

