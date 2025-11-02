cmake_minimum_required(VERSION 3.18)

project(hid_proxy C CXX ASM)

# --- Pico SDK ---
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "/opt/pico-sdk")
endif()
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
pico_sdk_init()

# --- Firmware executable ---
add_executable(hid_proxy
        src/encryption.c
        src/flash.c
        src/hid_proxy.c
        src/key_defs.c
        src/macros.c
        src/msc_disk.c
        src/nfc_tag.c
        src/sane.c
        src/usb_descriptors.c
        src/usb_host.c
)

# --- Include directories ---
target_include_directories(hid_proxy PRIVATE
        include
        $ENV{PICO_LIBS_PATH}/tiny-AES-c
        $ENV{PICO_LIBS_PATH}/tinycrypt/lib/include/
)


# tinycrypt
file(GLOB_RECURSE TINYCRYPT_SOURCES
        $ENV{PICO_LIBS_PATH}/tinycrypt/lib/*.c
)
add_library(tinycrypt STATIC ${TINYCRYPT_SOURCES})
target_include_directories(tinycrypt PUBLIC $ENV{PICO_LIBS_PATH}/tinycrypt/lib)

# --- Libraries with CMakeLists.txt ---
add_subdirectory($ENV{PICO_LIBS_PATH}/tiny-AES-c tiny-AES-c)
add_subdirectory($ENV{PICO_PIO_USB_PATH} pico_pio_usb)

# --- Link libraries to firmware ---
target_link_libraries(hid_proxy
        pico_stdlib
        tiny-AES-c
        tinycrypt
        pico_pio_usb
        tinyusb_board
)

# --- Enable USB stdio on Pico ---
pico_enable_stdio_usb(hid_proxy 1)
pico_enable_stdio_uart(hid_proxy 0)
pico_add_extra_outputs(hid_proxy)
