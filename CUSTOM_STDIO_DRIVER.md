# Custom stdio_driver_t Implementation Guide

## The Problem

Based on CMakeLists.txt:99-104, `pico_stdio_usb` is automatically disabled when `LIB_TINYUSB_HOST` is defined because the entire `stdio_usb.c` file in the Pico SDK is wrapped in `#ifndef LIB_TINYUSB_HOST`. This prevents USB serial debug output when using PIO-USB as a host.

The official recommendation is to implement a custom `stdio_driver_t` to enable USB stdio alongside TinyUSB host stack.

## What's Involved in the Fix

### 1. Define the Driver Structure and Functions

```c
// Custom stdio driver for USB CDC with TinyUSB host support
static void my_usb_out_chars(const char *buf, int length) {
    for (int i = 0; i < length; i++) {
        tud_cdc_write_char(buf[i]);
    }
    tud_cdc_write_flush();
}

static int my_usb_in_chars(char *buf, int length) {
    int rc = PICO_ERROR_NO_DATA;
    if (tud_cdc_available()) {
        int count = (int) tud_cdc_read(buf, (uint32_t) length);
        rc = count ? count : PICO_ERROR_NO_DATA;
    }
    return rc;
}

static void my_usb_out_flush(void) {
    tud_cdc_write_flush();
}

static stdio_driver_t my_usb_stdio = {
    .out_chars = my_usb_out_chars,
    .out_flush = my_usb_out_flush,
    .in_chars = my_usb_in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = true
#endif
};
```

### 2. Initialize and Register the Driver

```c
void my_usb_stdio_init(void) {
    // Ensure TinyUSB is initialized first
    // tusb_init() should already be called

    // Register the custom stdio driver
    stdio_set_driver_enabled(&my_usb_stdio, true);
}
```

### 3. Requirements Checklist

- ✅ **CDC interface in USB descriptors** (already present in usb_descriptors.c)
  ```c
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, ...)
  ```

- ✅ **Enable CDC in tusb_config.h** (already configured)
  ```c
  #define CFG_TUD_CDC 1
  ```

- ✅ **Call tud_task() regularly** (already done in hid_proxy.c:238)

## Projects That Already Do This

### 1. cdc_stdio_lib by rppicomidi ⭐ RECOMMENDED

**GitHub**: https://github.com/rppicomidi/cdc_stdio_lib

**Purpose**: Specifically designed to enable USB CDC stdio when using TinyUSB host stack

**Approach**: Extracts and adapts the pico-sdk's stdio_usb implementation to work with TinyUSB

**Usage**: Drop-in library that provides `cdc_stdio_lib_init()` function

**Perfect for this project**: Solves exactly the use case (Pico-PIO-USB with stdio)

**Implementation details**:
- Copies essential code from pico-sdk's source
- Modifies the code to work with TinyUSB-based applications
- Provides a way to "glue the TinyUSB CDC device driver to the pico-sdk's stdio library"

**Requirements**:
1. In `tusb_config.h`: Enable `CFG_TUD_ENABLED` and `CFG_TUD_CDC`
2. Add a CDC interface in the device descriptor
3. Call `tud_init()` or `tusb_init()` first
4. Then call `cdc_stdio_lib_init()`
5. Periodically call `tud_task()`

### 2. Pico-PIO-USB Examples

**GitHub**: https://github.com/sekigon-gonnoc/Pico-PIO-USB

**Example**: `examples/host_hid_to_device_cdc/host_hid_to_device_cdc.c`
- Shows HID host with CDC device output
- Uses native TinyUSB for both host (PIO) and device (native USB)
- Good reference for dual-stack configuration

### 3. TinyUSB BSP Examples

**GitHub**: https://github.com/hathach/tinyusb/blob/master/hw/bsp/rp2040/family.c

**Example**: RTT (Real-Time Transfer) stdio driver implementation
- Shows custom stdio_driver_t for debugging purposes
- Alternative to USB CDC for debug output

## Implementation Strategy for This Project

### Option A: Use cdc_stdio_lib (Easiest)

1. Add `cdc_stdio_lib` as a submodule:
   ```bash
   git submodule add https://github.com/rppicomidi/cdc_stdio_lib.git
   git submodule update --init --recursive
   ```

2. Update CMakeLists.txt:
   ```cmake
   add_subdirectory(cdc_stdio_lib)

   target_link_libraries(hid_proxy PUBLIC
       # ... existing libraries ...
       cdc_stdio_lib
   )
   ```

3. In your initialization code (hid_proxy.c):
   ```c
   #include "cdc_stdio_lib.h"

   // After tusb_init()
   cdc_stdio_lib_init();
   ```

4. Update CMakeLists.txt:
   ```cmake
   # Change this line:
   pico_enable_stdio_usb(hid_proxy 0)
   # To:
   # pico_enable_stdio_usb still disabled, using cdc_stdio_lib instead
   ```

### Option B: Custom Implementation (More Control)

1. Create `src/stdio_usb_custom.c` and `include/stdio_usb_custom.h`

2. Implement the driver based on the code pattern shown above

3. Copy relevant functions from pico-sdk's `stdio_usb.c` but remove the `#ifndef LIB_TINYUSB_HOST` guard

4. Reference implementations:
   - Pico SDK: `pico-sdk/src/rp2_common/pico_stdio_usb/stdio_usb.c`
   - Pico SDK: `pico-sdk/src/rp2_common/pico_stdio_uart/stdio_uart.c`

5. Register your custom driver in main initialization

## Key Technical Details

### stdio_driver_t Structure

```c
typedef struct stdio_driver {
    void (*out_chars)(const char *buf, int len);
    void (*out_flush)(void);
    int (*in_chars)(char *buf, int len);
    void (*set_chars_available_callback)(void (*fn)(void*), void *param);
    struct stdio_driver *next;
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    bool last_ended_with_cr;
    bool crlf_enabled;
#endif
} stdio_driver_t;
```

### Function Requirements

- **out_chars**: Output characters to the device
  - Takes buffer and length
  - Must handle the full length provided

- **out_flush**: Flush any buffered output
  - Can be NULL if no buffering

- **in_chars**: Read characters from device
  - **Important**: Must return `PICO_ERROR_NO_DATA` when no data available (not 0)
  - Returns number of characters read, or `PICO_ERROR_NO_DATA`

- **set_chars_available_callback**: Optional
  - Used for interrupt-driven input
  - Can be NULL

### TinyUSB CDC Functions Needed

- `tud_cdc_write()` - Write data to CDC
- `tud_cdc_write_char()` - Write single character
- `tud_cdc_write_flush()` - Flush write buffer
- `tud_cdc_read()` - Read data from CDC
- `tud_cdc_available()` - Check if data available
- `tud_task()` - Process USB events (must be called regularly)

### Reference Implementations from Pico SDK

#### stdio_uart.c Implementation

```c
static void stdio_uart_out_chars(const char *buf, int length) {
    for (int i = 0; i < length; i++) {
        uart_putc(uart_instance, buf[i]);
    }
}

int stdio_uart_in_chars(char *buf, int length) {
    int i = 0;
    while (i < length && uart_is_readable(uart_instance)) {
        buf[i++] = uart_getc(uart_instance);
    }
    return i ? i : PICO_ERROR_NO_DATA;
}

static void stdio_uart_out_flush(void) {
    uart_tx_wait_blocking(uart_instance);
}

stdio_driver_t stdio_uart = {
    .out_chars = stdio_uart_out_chars,
    .out_flush = stdio_uart_out_flush,
    .in_chars = stdio_uart_in_chars,
#if PICO_STDIO_UART_SUPPORT_CHARS_AVAILABLE_CALLBACK
    .set_chars_available_callback = stdio_uart_set_chars_available_callback,
#endif
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_UART_DEFAULT_CRLF
#endif
};
```

## Important Notes

1. **Driver Registration**: Use `stdio_set_driver_enabled(&driver, true)` to register
   - This function is not re-entrant
   - Always call on an initialized driver

2. **Multiple Drivers**: The stdio system supports multiple registered drivers
   - Iterates through all drivers for output
   - Checks each driver for input

3. **API Stability**: The API for adding additional input/output devices is not yet considered stable

4. **SDK Modification Not Recommended**: The comment mentions modifying pico-sdk's `stdio_usb.c` to remove `#ifndef LIB_TINYUSB_HOST`, but implementing a custom driver is the cleaner approach that doesn't require patching the SDK

## Recommended Next Steps

1. **Try cdc_stdio_lib first** - It's battle-tested and maintained
2. **Test thoroughly** - Ensure both printf output and input work correctly
3. **Verify with your dual-core setup** - Make sure USB CDC works correctly with Core 0/Core 1 split
4. **Consider thread safety** - If using FreeRTOS or accessing from multiple cores, add appropriate locking

## Additional Resources

- [Raspberry Pi Forums - TinyUSB host in PIO with stdio usb](https://forums.raspberrypi.com/viewtopic.php?t=355601)
- [Raspberry Pi Forums - USB serial with TinyUSB library](https://forums.raspberrypi.com/viewtopic.php?t=334842)
- [Pico SDK stdio_driver Reference](https://www.raspberrypi.com/documentation/pico-sdk/structstdio__driver.html)
- [Pico SDK pico_stdio Group](https://cec-code-lab.aps.edu/robotics/resources/pico-c-api/group__pico__stdio.html)
