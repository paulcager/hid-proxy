cmake_minimum_required(VERSION 3.18)

# Project name
project(hid_proxy C CXX ASM)

# --- Pico SDK ---
if(NOT DEFINED PICO_SDK_PATH)
    message(FATAL_ERROR "PICO_SDK_PATH is not set. Set it to the path of pico-sdk")
endif()
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Initialize the SDK
pico_sdk_init()

# --- Define firmware executable ---
add_executable(hid_proxy
        src/main.c
        src/hid_proxy.c
        # Add other RP2040-dependent source files here
        src/usb_iface_pico.c  # RP2040-specific wrapper
)

# --- Include directories ---
target_include_directories(hid_proxy PRIVATE
        include
        $ENV{PICO_LIBS_PATH}/tiny-AES-c
        $ENV{PICO_LIBS_PATH}/pn532-lib
        $ENV{PICO_LIBS_PATH}/Adafruit-PN532
        $ENV{PICO_LIBS_PATH}/tinycrypt
)

# --- Link submodules ---
add_subdirectory($ENV{PICO_LIBS_PATH}/tiny-AES-c tiny-AES-c)
add_subdirectory($ENV{PICO_LIBS_PATH}/pn532-lib pn532-lib)
add_subdirectory($ENV{PICO_LIBS_PATH}/Adafruit-PN532 Adafruit-PN532)
add_subdirectory($ENV{PICO_LIBS_PATH}/tinycrypt tinycrypt)
add_subdirectory($ENV{PICO_PIO_USB_PATH} pico_pio_usb)

target_link_libraries(hid_proxy
        pico_stdlib
        tiny-AES-c
        pn532-lib
        Adafruit-PN532
        tinycrypt
        pico_pio_usb
        tinyusb_board   # Built-in from Pico SDK
)

# --- Enable USB output and RP2040 features ---
pico_enable_stdio_usb(hid_proxy 1)
pico_enable_stdio_uart(hid_proxy 0)
pico_add_extra_outputs(hid_proxy)

# --- Host-side unit tests ---
enable_testing()
add_executable(test_core
        tests/test_core_logic.c
        src/hid_proxy.c        # Pure C logic
        src/usb_iface_fake.c   # RP2040 stubs for host
)

target_include_directories(test_core PRIVATE include)
target_link_libraries(test_core tiny-AES-c pn532-lib Adafruit-PN532 tinycrypt)

add_test(NAME unit_tests COMMAND test_core)
