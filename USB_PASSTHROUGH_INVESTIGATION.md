# USB Pass-Through Investigation

## Goal
Make the HID proxy appear as a true "pass-through" device by forwarding the physical keyboard's USB descriptors (VID/PID, manufacturer, product, serial) to the host computer, rather than using hard-coded values.

## Current State

### Hard-Coded Descriptors
**File**: `src/usb_descriptors.c`
- VID: `0xcafd` (custom)
- PID: `0xc31c` (custom)
- Manufacturer: "CagerSB"
- Product: "USB Keyboard"
- Serial: "892156789012"

### Architecture
- **Core 0**: Device stack (`tud_init`) - acts as keyboard to host computer
- **Core 1**: Host stack (`tuh_init`) - receives input from physical keyboard
- Device stack initialized immediately at startup (`hid_proxy.c:138`)
- Host stack initialized later on Core 1
- Physical keyboard info obtained in `tuh_hid_mount_cb()` via `tuh_vid_pid_get()`

## The Problem

### USB Enumeration Timing
The device stack must be initialized with descriptors **before** the host PC enumerates it, but we don't know the keyboard's identity until after it's plugged in. This creates a chicken-and-egg problem.

### TinyUSB Limitations
TinyUSB doesn't support changing device descriptors dynamically after `tud_init()` is called. Once initialized, the descriptors are fixed.

## Solutions Considered

### Option A: USB Re-enumeration (Complex)
1. Start with temporary/generic descriptors
2. When keyboard detected, clone its descriptors
3. Force USB re-enumeration (software reset or disconnect/reconnect)

**Problems**:
- RP2040's native USB lacks software-controlled pullup
- Requires watchdog reboot or external hardware
- Complex state management

### Option B: Delayed Device Initialization (RECOMMENDED)

**Constraint**: Keyboard must be plugged into Pico BEFORE Pico is plugged into host

#### Advantages
- ✅ No USB re-enumeration needed
- ✅ No software reset/reboot needed
- ✅ No USB pullup control needed
- ✅ No hot-plug complexity
- ✅ Clean and simple implementation

#### How It Works
1. Pico powers up (not connected to host yet)
2. Core 0: Initialize hardware, launch Core 1, **wait** for keyboard
3. Core 1: Initialize host stack, wait for keyboard
4. **User plugs keyboard into Pico**
5. Core 1: `tuh_hid_mount_cb()` fires, queries descriptors
6. Core 0: Calls `tud_init(0)` with cloned descriptors
7. **User plugs Pico into host PC**
8. Host PC enumerates and sees keyboard's original identity

## Implementation Plan

### 1. Add Descriptor Storage
**File**: `src/usb_descriptors.c` (~100 lines)

```c
// Storage for cloned descriptors
static tusb_desc_device_t cloned_device_desc;
static char cloned_manufacturer[128];
static char cloned_product[128];
static char cloned_serial[128];
static bool use_cloned_descriptors = false;

void set_cloned_descriptors(tusb_desc_device_t* dev_desc,
                           const char* mfr,
                           const char* prod,
                           const char* serial) {
    memcpy(&cloned_device_desc, dev_desc, sizeof(tusb_desc_device_t));
    strncpy(cloned_manufacturer, mfr, sizeof(cloned_manufacturer) - 1);
    strncpy(cloned_product, prod, sizeof(cloned_product) - 1);
    strncpy(cloned_serial, serial, sizeof(cloned_serial) - 1);
    use_cloned_descriptors = true;
}
```

### 2. Make Device Descriptor Callbacks Dynamic
**File**: `src/usb_descriptors.c`

```c
uint8_t const * tud_descriptor_device_cb(void) {
    if (use_cloned_descriptors) {
        return (uint8_t const *) &cloned_device_desc;
    }
    // Fallback to default (if keyboard not detected before timeout)
    return (uint8_t const *) &desc_device;
}

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    if (use_cloned_descriptors && index >= 1 && index <= 3) {
        const char* str = NULL;
        switch(index) {
            case 1: str = cloned_manufacturer; break;
            case 2: str = cloned_product; break;
            case 3: str = cloned_serial; break;
        }

        if (str) {
            // Convert ASCII to UTF-16 (existing code pattern)
            uint8_t chr_count = strlen(str);
            if (chr_count > 31) chr_count = 31;
            for(uint8_t i = 0; i < chr_count; i++) {
                desc_str[1 + i] = str[i];
            }
            desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
            return desc_str;
        }
    }

    // Fallback to original implementation
    // ... existing code ...
}
```

### 3. Capture Keyboard Descriptors on Mount
**File**: `src/usb_host.c` (~40 lines)

```c
// At top of file
extern void set_cloned_descriptors(tusb_desc_device_t* dev_desc,
                                   const char* mfr,
                                   const char* prod,
                                   const char* serial);
extern volatile bool keyboard_descriptors_ready;

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const *desc_report, uint16_t desc_len) {
    // ... existing code ...

    // Get VID/PID (already done at line 100)
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    // Get full device descriptor
    tusb_desc_device_t dev_desc;
    if (tuh_descriptor_get_device_local(dev_addr, &dev_desc)) {
        LOG_INFO("Got device descriptor: VID=%04x PID=%04x\n",
                 dev_desc.idVendor, dev_desc.idProduct);

        // Get string descriptors (use sync API if available)
        // Note: May need to use async callbacks depending on TinyUSB version
        char mfr[128] = "Unknown";
        char prod[128] = "Unknown";
        char serial[128] = "Unknown";

        // Try to get strings (these may fail, use defaults if so)
        uint16_t temp_buf[128];
        if (tuh_descriptor_get_manufacturer_string_sync(dev_addr, 0x0409,
                                                         temp_buf, sizeof(temp_buf)) == 0) {
            // Convert UTF-16 to ASCII (simplified)
            for (int i = 0; i < 127 && temp_buf[1 + i]; i++) {
                mfr[i] = (char)temp_buf[1 + i];
            }
        }

        if (tuh_descriptor_get_product_string_sync(dev_addr, 0x0409,
                                                    temp_buf, sizeof(temp_buf)) == 0) {
            for (int i = 0; i < 127 && temp_buf[1 + i]; i++) {
                prod[i] = (char)temp_buf[1 + i];
            }
        }

        if (tuh_descriptor_get_serial_string_sync(dev_addr, 0x0409,
                                                   temp_buf, sizeof(temp_buf)) == 0) {
            for (int i = 0; i < 127 && temp_buf[1 + i]; i++) {
                serial[i] = (char)temp_buf[1 + i];
            }
        }

        // Store cloned descriptors
        set_cloned_descriptors(&dev_desc, mfr, prod, serial);
        keyboard_descriptors_ready = true;

        LOG_INFO("Cloned keyboard identity: %s %s (%s)\n", mfr, prod, serial);
    }

    // ... rest of existing code ...
}
```

### 4. Delay Device Stack Initialization
**File**: `src/hid_proxy.c` (~20 lines)

```c
// Add global flag
volatile bool keyboard_descriptors_ready = false;

int main(void) {
    // ... existing initialization ...

    // DON'T call tud_init(0) here anymore!
    // tud_init(0);  // <-- REMOVE OR COMMENT OUT

    // ... other initialization ...

    // Launch Core 1 FIRST to detect keyboard
    multicore_launch_core1(core1_main);
    LOG_INFO("Core 1 launched\n");

    // Wait for keyboard to be detected (with timeout)
    LOG_INFO("Waiting for keyboard to be detected...\n");
    absolute_time_t timeout = make_timeout_time_ms(30000); // 30 second timeout
    while (!keyboard_descriptors_ready) {
        if (time_reached(timeout)) {
            LOG_INFO("Timeout waiting for keyboard, using default descriptors\n");
            break;
        }
        // Could add LED blink pattern here to indicate waiting
        tight_loop_contents();
    }

    // NOW initialize device stack with cloned (or default) descriptors
    LOG_INFO("Initializing USB device stack\n");
    tud_init(0);

    // ... rest of existing code ...
}
```

### 5. Add Function Prototype
**File**: `src/usb_descriptors.h` or new header (~15 lines)

```c
#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H

#include "tusb.h"

// Set cloned descriptors from physical keyboard
void set_cloned_descriptors(tusb_desc_device_t* dev_desc,
                           const char* manufacturer,
                           const char* product,
                           const char* serial);

#endif
```

## Files to Modify

1. **src/usb_descriptors.c** (~100 lines added/modified)
   - Add descriptor storage variables
   - Add `set_cloned_descriptors()` function
   - Modify `tud_descriptor_device_cb()` to return cloned descriptor
   - Modify `tud_descriptor_string_cb()` to return cloned strings

2. **src/usb_descriptors.h** (~15 lines added)
   - Add function prototype for `set_cloned_descriptors()`
   - Create header if it doesn't exist

3. **src/usb_host.c** (~40 lines added)
   - Add descriptor fetching in `tuh_hid_mount_cb()`
   - Call `set_cloned_descriptors()` when keyboard mounted
   - Set `keyboard_descriptors_ready` flag

4. **src/hid_proxy.c** (~20 lines modified)
   - Add `keyboard_descriptors_ready` flag (volatile, shared between cores)
   - Move `tud_init(0)` to after keyboard detection
   - Add wait loop with timeout (30 seconds)
   - Add logging for user feedback

5. **include/hid_proxy.h** (~2 lines)
   - Declare `extern volatile bool keyboard_descriptors_ready;`

## User Experience

### Requirements
- ⚠️ **Keyboard MUST be plugged into Pico BEFORE Pico is plugged into host**
- Order: Power Pico → Plug keyboard → Wait for ready → Plug into host

### Feedback
- LED blink pattern to indicate "waiting for keyboard" (NumLock LED)
- Serial console messages for debugging
- 30-second timeout → falls back to default descriptors if no keyboard

### Optional Enhancements
1. Store last-seen keyboard descriptors in kvstore
2. Use stored descriptors if keyboard not detected within timeout
3. Add configuration option to enable/disable pass-through mode
4. Support multiple keyboards (use first detected)

## Testing Plan

1. **Test normal flow**:
   - Power Pico without host connection
   - Plug in keyboard
   - Verify serial output shows cloned descriptors
   - Plug into host PC
   - Run `lsusb -v` to verify VID/PID/strings match keyboard

2. **Test timeout**:
   - Power Pico without keyboard
   - Wait 30 seconds
   - Verify fallback to default descriptors
   - Plug into host PC

3. **Test different keyboards**:
   - Try multiple keyboard brands/models
   - Verify each one's identity is correctly forwarded

## Notes

- HID report descriptors are NOT cloned (intentional)
  - We keep our custom HID reports for macro functionality
  - Only the device identity (VID/PID/strings) is forwarded

- Configuration descriptors are NOT cloned
  - We maintain our own interface configuration (keyboard + mouse + optional CDC)
  - Only matters if host driver does strict validation

- The `_sync` versions of string descriptor functions may not exist in all TinyUSB versions
  - May need to use async callbacks instead
  - Check TinyUSB version in use

## Estimated Complexity
**Low-Medium**: With the keyboard-first constraint, this becomes straightforward. Main work is:
1. String descriptor fetching and UTF-16 handling (~30 mins)
2. Core synchronization and timing (~20 mins)
3. Testing with different keyboards (~1 hour)

Total: ~2-3 hours of development + testing

## Status
**On Hold** - Investigation complete, ready for implementation when needed.
