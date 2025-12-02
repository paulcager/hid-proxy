# Files Overview

Quick reference for what each file does.

## Build Configuration

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Top-level ESP-IDF project configuration |
| `main/CMakeLists.txt` | Main component dependencies and sources |
| `components/uart_protocol/CMakeLists.txt` | UART component configuration |
| `.gitignore` | Ignore build artifacts and temp files |

## Source Code

### Main Application

| File | Lines | Purpose |
|------|-------|---------|
| `main/main.c` | ~60 | Entry point, UART init, main loop |
| `main/usb_host_hid.c` | ~180 | USB host setup, HID callbacks, UART forwarding |
| `main/usb_host_hid.h` | ~15 | USB host public API |

### UART Protocol Component (Reusable)

| File | Lines | Purpose |
|------|-------|---------|
| `components/uart_protocol/uart_protocol.c` | ~120 | Packet framing, send/receive, checksum |
| `components/uart_protocol/include/uart_protocol.h` | ~60 | Protocol definitions, API |

**Total production code**: ~435 lines

## Tools

| File | Lines | Purpose |
|------|-------|---------|
| `tools/uart_monitor.py` | ~140 | Decode and display UART packets (Python) |
| `tools/test_uart_protocol.py` | ~120 | Send test packets to UART (Python) |

## Documentation

| File | Purpose | Audience |
|------|---------|----------|
| `README.md` | Complete documentation | All users |
| `QUICKSTART.md` | Build/flash/test instructions | First-time users |
| `PROJECT_SUMMARY.md` | Architecture and design decisions | Developers |
| `FILES_OVERVIEW.md` | This file | Reference |

## Key Sections to Read

**If you want to...**

- **Build and flash**: Read `QUICKSTART.md`
- **Understand the code**: Read `PROJECT_SUMMARY.md`
- **Modify UART protocol**: Edit `components/uart_protocol/*`
- **Change USB behavior**: Edit `main/usb_host_hid.c`
- **Test UART output**: Use `tools/uart_monitor.py`
- **Debug UART receiver**: Use `tools/test_uart_protocol.py`

## Important Constants

Defined in code:

```c
// main/main.c
#define UART_NUM        UART_NUM_1      // Use UART1 (not UART0 = console)
#define UART_TX_PIN     GPIO_NUM_3
#define UART_RX_PIN     GPIO_NUM_4

// components/uart_protocol/uart_protocol.c
#define UART_BUF_SIZE   1024            // RX/TX buffer size
.baud_rate = 921600                     // UART speed

// components/uart_protocol/include/uart_protocol.h
#define UART_PACKET_START 0xAA          // Sync byte
#define UART_MAX_PAYLOAD  256           // Max packet payload
```

## Modification Guide

### To change UART pins:

Edit `main/main.c`:
```c
#define UART_TX_PIN     GPIO_NUM_XX  // Your TX pin
#define UART_RX_PIN     GPIO_NUM_YY  // Your RX pin
```

### To change baud rate:

Edit `components/uart_protocol/uart_protocol.c`:
```c
uart_config_t uart_config = {
    .baud_rate = 115200,  // Change from 921600
    // ...
};
```

Also update `tools/uart_monitor.py` and `tools/test_uart_protocol.py`.

### To add new packet type:

1. Add to enum in `components/uart_protocol/include/uart_protocol.h`:
   ```c
   typedef enum {
       PKT_KEYBOARD_REPORT = 0x01,
       PKT_MOUSE_REPORT = 0x02,
       PKT_LED_UPDATE = 0x03,
       PKT_STATUS = 0x04,
       PKT_ACK = 0x05,
       PKT_YOUR_TYPE = 0x06,  // Add here
   } packet_type_t;
   ```

2. Handle in `main/usb_host_hid.c` (sender) or ESP32-S3 #2 (receiver)

3. Update `tools/uart_monitor.py` to decode it

### To enable debug logging:

```bash
idf.py menuconfig
# Navigate to: Component config → Log output → Default log verbosity
# Set to "Debug" or "Verbose"
idf.py build flash monitor
```

## Dependencies

### ESP-IDF Components Used

- `driver` - UART driver
- `usb` - USB host library
- `freertos` - Task scheduler, event groups
- `esp_log` - Logging macros

### External Dependencies

None! All code is self-contained.

## Build Artifacts (Ignored by Git)

After building, you'll see:

```
build/
├── bootloader/
├── esp-idf/
├── usb_host_uart_passthrough.bin    # Flash this at 0x10000
├── usb_host_uart_passthrough.elf    # Debug symbols
├── usb_host_uart_passthrough.map    # Memory map
└── ...

sdkconfig                             # Build configuration (auto-generated)
```

Don't commit these - they're in `.gitignore`.

## Code Statistics

```
Language      Files    Blank    Comment    Code
============================================
C                3       45        90       300
C Header         3       15        25        60
Python           2       30        40       220
Markdown         4      100         0       400
CMake            3        5         0        15
============================================
TOTAL           15      195       155       995
```

Small, focused codebase. Easy to understand and modify.
