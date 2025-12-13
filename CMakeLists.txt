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

# Debug: Show board selection
message(STATUS "PICO_BOARD = ${PICO_BOARD}")
message(STATUS "PICO_PLATFORM = ${PICO_PLATFORM}")
message(STATUS "PICO_CYW43_SUPPORTED = ${PICO_CYW43_SUPPORTED}")

# Option to enable USB CDC stdio (for debugging)
# Disabled by default for production builds
option(ENABLE_USB_STDIO "Enable USB CDC stdio for debugging" OFF)

# Option to enable NFC tag authentication
# Disabled by default as NFC hardware is not commonly used
option(ENABLE_NFC "Enable NFC tag authentication support" OFF)

# Option to enable diagnostic system (cyclic buffers, Double-shift+D)
# Disabled by default to save 16KB RAM
option(ENABLE_DIAGNOSTICS "Enable diagnostic keystroke buffers and dump command" OFF)

# Option to enable Waveshare RP2350-USB-A board (changes PIO-USB pins)
option(BOARD_WS_2350 "Build for Waveshare RP2350-USB-A board" OFF)

# USB host GPIO pin configuration (D+ pin, D- is automatically D++1)
# Default is GPIO6 for new boards, but can be set to GPIO2 for backwards compatibility
set(USB_HOST_DP_PIN "6" CACHE STRING "GPIO pin for USB host D+ (D- is D++1)")
message(STATUS "USB host D+ pin: GPIO${USB_HOST_DP_PIN}")

add_subdirectory("./Pico-PIO-USB" pico_pio_usb)

# Add pico-kvstore for key-value storage with encryption
add_subdirectory("./pico-kvstore" pico_kvstore)

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
        src/pbkdf-lite.c
        src/macros.c
        src/usb_host.c
        src/usb_descriptors.c
        src/kvstore_init.c
        src/keydef_store.c
        src/led_control.c
        src/diagnostics.c
        tiny-AES-c/aes.c
        tinycrypt/lib/source/sha256.c
        tinycrypt/lib/source/hmac.c
        tinycrypt/lib/source/utils.c
)

# Add NFC sources only if enabled
if(ENABLE_NFC)
    target_sources(hid_proxy PRIVATE
        src/nfc_tag.c
    )
endif()

# Add WS2812 LED sources only if building for Waveshare RP2350-USB-A
if(BOARD_WS_2350)
    # Generate PIO header for WS2812
    pico_generate_pio_header(hid_proxy ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

    target_sources(hid_proxy PRIVATE
        src/ws2812_led.c
    )
    message(STATUS "WS2812 RGB LED support enabled for Waveshare board")
endif()

# Add WiFi/HTTP/MQTT/TLS sources only if building for Pico W
if(PICO_CYW43_SUPPORTED)
    target_sources(hid_proxy PRIVATE
        src/wifi_config.c
        src/wifi_console.c
        src/http_server.c
        src/https_cert.c
        src/mqtt_client.c
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
        ${CMAKE_CURRENT_LIST_DIR}/pico-kvstore/include
        ${CMAKE_CURRENT_LIST_DIR}/pico-kvstore/lib/pico-vfs/include
)

target_link_libraries(hid_proxy PUBLIC
        pico_stdlib
        pico_flash
        pico_rand
        pico_time
        hardware_flash
        hardware_exception
        pico_pio_usb
        tinyusb_device
        tinyusb_board
        tinyusb_host
        pico_bootrom
        kvstore
        kvstore_logkvs
        kvstore_securekvs
        blockdevice_flash
        pico_mbedtls
)

# Add I2C library only if NFC is enabled
if(ENABLE_NFC)
    target_link_libraries(hid_proxy PUBLIC
        hardware_i2c
    )
endif()

# Add WiFi/HTTP/MQTT/TLS libraries only if building for Pico W
if(PICO_CYW43_SUPPORTED)
    target_link_libraries(hid_proxy PUBLIC
        pico_cyw43_arch_lwip_threadsafe_background
        pico_lwip_mdns
        pico_lwip_http
        pico_lwip_mqtt
        pico_lwip_mbedtls
        pico_mbedtls
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

        # --- MQTT_BROKER ---
        if("${ENV_CONTENT}" MATCHES "MQTT_BROKER=([^
]*)")
            set(MQTT_BROKER "${CMAKE_MATCH_1}")
            string(STRIP "${MQTT_BROKER}" MQTT_BROKER)

            # Strip comments (everything after #)
            string(REGEX REPLACE "#.*$" "" MQTT_BROKER "${MQTT_BROKER}")
            string(STRIP "${MQTT_BROKER}" MQTT_BROKER)

            # Unquote if quoted
            string(REGEX MATCH "^\"(.*)\"$" _ "${MQTT_BROKER}")
            if(CMAKE_MATCH_COUNT GREATER 0)
                set(MQTT_BROKER "${CMAKE_MATCH_1}")
            else()
                string(REGEX MATCH "^'(.*)'$" _ "${MQTT_BROKER}")
                if(CMAKE_MATCH_COUNT GREATER 0)
                    set(MQTT_BROKER "${CMAKE_MATCH_1}")
                endif()
            endif()

            message(STATUS "Found MQTT_BROKER: ${MQTT_BROKER}")
            target_compile_definitions(hid_proxy PRIVATE "MQTT_BROKER=\"${MQTT_BROKER}\"")
        endif()

        # --- MQTT_PORT ---
        if("${ENV_CONTENT}" MATCHES "MQTT_PORT=([^
]*)")
            set(MQTT_PORT "${CMAKE_MATCH_1}")
            string(STRIP "${MQTT_PORT}" MQTT_PORT)
            # Strip comments
            string(REGEX REPLACE "#.*$" "" MQTT_PORT "${MQTT_PORT}")
            string(STRIP "${MQTT_PORT}" MQTT_PORT)
            message(STATUS "Found MQTT_PORT: ${MQTT_PORT}")
            target_compile_definitions(hid_proxy PRIVATE "MQTT_PORT=${MQTT_PORT}")
        endif()

        # --- MQTT_USE_TLS ---
        if("${ENV_CONTENT}" MATCHES "MQTT_USE_TLS=([^
]*)")
            set(MQTT_USE_TLS "${CMAKE_MATCH_1}")
            string(STRIP "${MQTT_USE_TLS}" MQTT_USE_TLS)
            # Strip comments
            string(REGEX REPLACE "#.*$" "" MQTT_USE_TLS "${MQTT_USE_TLS}")
            string(STRIP "${MQTT_USE_TLS}" MQTT_USE_TLS)
            string(TOUPPER "${MQTT_USE_TLS}" MQTT_USE_TLS_UPPER)
            if("${MQTT_USE_TLS_UPPER}" STREQUAL "TRUE" OR "${MQTT_USE_TLS_UPPER}" STREQUAL "1")
                message(STATUS "MQTT TLS enabled")
                target_compile_definitions(hid_proxy PRIVATE "MQTT_USE_TLS=1")
            endif()
        endif()

        # --- MQTT_USERNAME ---
        if("${ENV_CONTENT}" MATCHES "MQTT_USERNAME=([^
]*)")
            set(MQTT_USERNAME "${CMAKE_MATCH_1}")
            string(STRIP "${MQTT_USERNAME}" MQTT_USERNAME)
            # Strip comments
            string(REGEX REPLACE "#.*$" "" MQTT_USERNAME "${MQTT_USERNAME}")
            string(STRIP "${MQTT_USERNAME}" MQTT_USERNAME)

            # Unquote if quoted
            string(REGEX MATCH "^\"(.*)\"$" _ "${MQTT_USERNAME}")
            if(CMAKE_MATCH_COUNT GREATER 0)
                set(MQTT_USERNAME "${CMAKE_MATCH_1}")
            else()
                string(REGEX MATCH "^'(.*)'$" _ "${MQTT_USERNAME}")
                if(CMAKE_MATCH_COUNT GREATER 0)
                    set(MQTT_USERNAME "${CMAKE_MATCH_1}")
                endif()
            endif()

            message(STATUS "Found MQTT_USERNAME: ${MQTT_USERNAME}")
            target_compile_definitions(hid_proxy PRIVATE "MQTT_USERNAME=\"${MQTT_USERNAME}\"")
        endif()

        # --- MQTT_PASSWORD ---
        if("${ENV_CONTENT}" MATCHES "MQTT_PASSWORD=([^
]*)")
            set(MQTT_PASSWORD "${CMAKE_MATCH_1}")
            string(STRIP "${MQTT_PASSWORD}" MQTT_PASSWORD)
            # Strip comments
            string(REGEX REPLACE "#.*$" "" MQTT_PASSWORD "${MQTT_PASSWORD}")
            string(STRIP "${MQTT_PASSWORD}" MQTT_PASSWORD)

            # Unquote if quoted
            string(REGEX MATCH "^\"(.*)\"$" _ "${MQTT_PASSWORD}")
            if(CMAKE_MATCH_COUNT GREATER 0)
                set(MQTT_PASSWORD "${CMAKE_MATCH_1}")
            else()
                string(REGEX MATCH "^'(.*)'$" _ "${MQTT_PASSWORD}")
                if(CMAKE_MATCH_COUNT GREATER 0)
                    set(MQTT_PASSWORD "${CMAKE_MATCH_1}")
                endif()
            endif()

            message(STATUS "Found MQTT_PASSWORD")
            target_compile_definitions(hid_proxy PRIVATE "MQTT_PASSWORD=\"${MQTT_PASSWORD}\"")
        endif()
    else()
        message(STATUS ".env file not found, skipping build-time WiFi/MQTT config.")
    endif()
endif()

target_compile_definitions(hid_proxy PRIVATE
# Can move the UART0 ports like so:
#        PICO_DEFAULT_UART_RX_PIN=16
#        PICO_DEFAULT_UART_TX_PIN=17
        PN532DEBUG=1
        DEBUG_SERIAL=Serial
        AES256=1
)

# Pass PICO_CYW43_SUPPORTED to source code as a preprocessor macro
if(PICO_CYW43_SUPPORTED)
    target_compile_definitions(hid_proxy PRIVATE PICO_CYW43_SUPPORTED=1)
    message(STATUS "PICO_CYW43_SUPPORTED defined for source code")
endif()

# Pass ENABLE_NFC to source code as a preprocessor macro
if(ENABLE_NFC)
    target_compile_definitions(hid_proxy PRIVATE ENABLE_NFC=1)
    message(STATUS "NFC support enabled")
endif()

# Pass ENABLE_DIAGNOSTICS to source code as a preprocessor macro
if(ENABLE_DIAGNOSTICS)
    target_compile_definitions(hid_proxy PRIVATE ENABLE_DIAGNOSTICS=1)
    message(STATUS "Diagnostic system enabled (16KB RAM allocated)")
endif()

# Pass BOARD_WS_2350 to source code as a preprocessor macro
if(BOARD_WS_2350)
    target_compile_definitions(hid_proxy PRIVATE BOARD_WS_2350=1)
    message(STATUS "Waveshare RP2350-USB-A board selected (USB-A on GPIO13)")
endif()

# Pass USB_HOST_DP_PIN to source code as a preprocessor macro
target_compile_definitions(hid_proxy PRIVATE USB_HOST_DP_PIN=${USB_HOST_DP_PIN})

