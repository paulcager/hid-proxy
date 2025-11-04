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

# Option to enable USB CDC stdio (for debugging)
# Disabled by default for production builds
option(ENABLE_USB_STDIO "Enable USB CDC stdio for debugging" OFF)

add_subdirectory("./Pico-PIO-USB" pico_pio_usb)

# Add cdc_stdio_lib only if USB stdio is enabled
if(ENABLE_USB_STDIO)
    add_subdirectory("./cdc_stdio_lib" cdc_stdio_lib)
    message(STATUS "USB CDC stdio enabled for debugging")
endif()

add_executable(hid_proxy
        src/hid_proxy.c
        src/flash.c
        src/key_defs.c
        src/encryption.c
        src/sane.c
        src/nfc_tag.c
        src/macros.c
        src/usb_host.c
        src/usb_descriptors.c
        tiny-AES-c/aes.c
        tinycrypt/lib/source/sha256.c
        tinycrypt/lib/source/utils.c
)

# Add WiFi/HTTP sources only if building for Pico W
if(PICO_CYW43_SUPPORTED)
    target_sources(hid_proxy PRIVATE
        src/wifi_config.c
        src/http_server.c
    )
endif()

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

# Add WiFi/HTTP libraries only if building for Pico W
if(PICO_CYW43_SUPPORTED)
    target_link_libraries(hid_proxy PUBLIC
        pico_cyw43_arch_lwip_threadsafe_background
        pico_lwip_mdns
    )
endif()

# Add cdc_stdio_lib if USB stdio is enabled
if(ENABLE_USB_STDIO)
    target_link_libraries(hid_proxy PUBLIC
        cdc_stdio_lib
    )
    target_compile_definitions(hid_proxy PRIVATE
        ENABLE_USB_STDIO=1
    )
endif()

target_link_options(hid_proxy PRIVATE "-T${CMAKE_CURRENT_SOURCE_DIR}/memmap_custom.ld")

pico_add_extra_outputs(hid_proxy)

# The pico_stdio_usb is disabled because of a conflict with the TinyUSB host stack (LIB_TINYUSB_HOST),
# which this project uses for PIO-USB. The Pico SDK automatically disables stdio_usb in this configuration.
# The official recommendation for enabling both is to implement a custom stdio_driver_t.
# See for more details: https://forums.raspberrypi.com/viewtopic.php?t=305225
# The original comment below is preserved for historical context.
# TODO - also need to modify pcio SDK src/rp2_common/pico_stdio_usb/stdio_usb.c to remove `#ifndef LIB_TINYUSB_HOST`
pico_enable_stdio_usb(hid_proxy 0)
pico_enable_stdio_uart(hid_proxy 1)

# Check for .env file and bake in WiFi credentials if present
if(PICO_CYW43_SUPPORTED)
    set(ENV_FILE "${CMAKE_SOURCE_DIR}/.env")
    if(EXISTS ${ENV_FILE})
        message(STATUS "Found .env file, attempting to read WiFi credentials.")
        file(READ ${ENV_FILE} ENV_CONTENT)

        # --- WIFI_SSID ---
        if("${ENV_CONTENT}" MATCHES "WIFI_SSID=([^
]*)")
            set(WIFI_SSID "${CMAKE_MATCH_1}")
            string(STRIP "${WIFI_SSID}" WIFI_SSID)

            # Unquote if quoted
            string(REGEX MATCH "^\"(.*)\"$" _ "${WIFI_SSID}")
            if(CMAKE_MATCH_COUNT GREATER 0)
                set(WIFI_SSID "${CMAKE_MATCH_1}")
            else()
                string(REGEX MATCH "^'(.*)'$" _ "${WIFI_SSID}")
                if(CMAKE_MATCH_COUNT GREATER 0)
                    set(WIFI_SSID "${CMAKE_MATCH_1}")
                endif()
            endif()

            message(STATUS "Found WIFI_SSID: ${WIFI_SSID}")
            target_compile_definitions(hid_proxy PRIVATE "WIFI_SSID=\"${WIFI_SSID}\"")
        endif()

        # --- WIFI_PASSWORD ---
        if("${ENV_CONTENT}" MATCHES "WIFI_PASSWORD=([^
]*)")
            set(WIFI_PASSWORD "${CMAKE_MATCH_1}")
            string(STRIP "${WIFI_PASSWORD}" WIFI_PASSWORD)

            # Unquote if quoted
            string(REGEX MATCH "^\"(.*)\"$" _ "${WIFI_PASSWORD}")
            if(CMAKE_MATCH_COUNT GREATER 0)
                set(WIFI_PASSWORD "${CMAKE_MATCH_1}")
            else()
                string(REGEX MATCH "^'(.*)'$" _ "${WIFI_PASSWORD}")
                if(CMAKE_MATCH_COUNT GREATER 0)
                    set(WIFI_PASSWORD "${CMAKE_MATCH_1}")
                endif()
            endif()

            message(STATUS "Found WIFI_PASSWORD.")
            target_compile_definitions(hid_proxy PRIVATE "WIFI_PASSWORD=\"${WIFI_PASSWORD}\"")
        endif()
    else()
        message(STATUS ".env file not found, skipping build-time WiFi config.")
    endif()
endif()

target_compile_definitions(hid_proxy PRIVATE
# Can move the UART0 ports like so:
#        PICO_DEFAULT_UART_RX_PIN=16
#        PICO_DEFAULT_UART_TX_PIN=17
        PN532DEBUG=1
        DEBUG_SERIAL=Serial
)

