cmake_minimum_required(VERSION 3.13)

# Option to build tests for host instead of Pico firmware
option(BUILD_TESTS "Build unit tests for host" OFF)

if(BUILD_TESTS)
    # Build tests for host machine (no Pico SDK needed)
    project(hid_proxy_tests C)
    add_subdirectory(test)
    return()
endif()

# Normal Pico firmware build below
# Allow PICO_SDK_PATH to be overridden by environment variable or command line
# Priority: 1) CMake cache/command line, 2) Environment variable, 3) Default local path
if(NOT PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
        message(STATUS "Using PICO_SDK_PATH from environment: ${PICO_SDK_PATH}")
    else()
        set(PICO_SDK_PATH /home/paul/pico/pico-sdk)
        message(STATUS "Using default PICO_SDK_PATH: ${PICO_SDK_PATH}")
    endif()
else()
    message(STATUS "Using PICO_SDK_PATH from cache: ${PICO_SDK_PATH}")
endif()

include (pico_sdk_import.cmake)

project(hid_proxy)

pico_sdk_init()

add_subdirectory("./Pico-PIO-USB" pico_pio_usb)

add_executable(hid_proxy
        src/hid_proxy.c
        src/flash.c
        src/key_defs.c
        src/encryption.c
        src/sane.c
        src/nfc_tag.c
        src/macros.c
        src/msc_disk.c
        src/usb_host.c
        src/usb_descriptors.c
        tiny-AES-c/aes.c
        tinycrypt/lib/source/sha256.c
        tinycrypt/lib/source/utils.c
)

target_sources(hid_proxy PUBLIC
        # I have copied the following from the samples, without understanding why.
        # can use 'tinyusb_pico_pio_usb' library later when pico-sdk is updated
        ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/dcd_pio_usb.c
        ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
)

# Make sure TinyUSB can find tusb_config.h and project headers
target_include_directories(hid_proxy PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/tinycrypt/lib/include
)

target_link_libraries(hid_proxy PUBLIC
        pico_stdlib
        pico_flash
        pico_rand
        pico_time
        hardware_flash
        hardware_i2c
        hardware_exception
        pico_pio_usb
        tinyusb_device
        tinyusb_board
        tinyusb_host
)

target_link_options(hid_proxy PRIVATE "-T${CMAKE_CURRENT_SOURCE_DIR}/memmap_custom.ld")

pico_add_extra_outputs(hid_proxy)

# TODO - disable USB stdio for prod build.
# TODO - also need to modify pcio SDK src/rp2_common/pico_stdio_usb/stdio_usb.c to remove `#ifndef LIB_TINYUSB_HOST`
pico_enable_stdio_usb(hid_proxy 0)
pico_enable_stdio_uart(hid_proxy 1)

target_compile_definitions(hid_proxy PRIVATE
# Can move the UART0 ports like so:
#        PICO_DEFAULT_UART_RX_PIN=16
#        PICO_DEFAULT_UART_TX_PIN=17
        PN532DEBUG=1
        DEBUG_SERIAL=Serial
)

