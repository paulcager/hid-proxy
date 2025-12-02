# ESP32-S3 USB Host to UART Passthrough (PoC)

Simple proof-of-concept that demonstrates:
1. ESP32-S3 acting as USB host for HID keyboards/mice
2. Forwarding HID reports over UART using a simple packet protocol

This is Phase 1 of the ESP32 migration - a minimal working example before adding state machine logic, WiFi, etc.

## ⚡ Architectural Note

**This device will remain simple** - just USB host → UART forwarding.

**Current state**: USB→UART passthrough ✅ **COMPLETE**
**Future**: No changes planned - stays as simple forwarder

All application logic (state machine, macros, WiFi, storage) will run on **ESP32-S3 #2 (USB Device)**.

**See [ARCHITECTURE_DECISION.md](../ARCHITECTURE_DECISION.md) for rationale**

## Hardware Requirements

- **ESP32-S3-DevKitC-1** (or similar ESP32-S3 board with USB OTG support)
- **USB OTG cable** (USB Micro/Type-C female to USB-A male, depending on your ESP32-S3 board)
- **USB keyboard or mouse** for testing
- **UART connection** for monitoring output (optional: second ESP32 or USB-UART adapter)

## Pin Configuration

- **USB Host**: Native USB OTG on GPIO19/GPIO20 (hardware pins, no configuration needed)
- **UART TX**: GPIO3 (configurable in `main/main.c`)
- **UART RX**: GPIO4 (configurable in `main/main.c`)
- **UART Baud**: 921600

## Protocol

Simple packet-based protocol for transmitting HID reports:

```
[START][TYPE][LEN_L][LEN_H][PAYLOAD...][CHECKSUM]

START:    0xAA (sync byte)
TYPE:     Packet type (0x01=keyboard, 0x02=mouse, 0x03=LED, 0x04=status)
LEN_L/H:  Payload length (little-endian uint16)
PAYLOAD:  Variable-length data
CHECKSUM: XOR of all preceding bytes
```

### Packet Types

- `0x01` - **Keyboard Report** (8 bytes): `[modifier][reserved][key0-5]`
- `0x02` - **Mouse Report** (3-5 bytes): `[buttons][x][y][wheel][pan]`
- `0x03` - **LED Update** (1 byte): LED status from host
- `0x04` - **Status** (variable): Text status messages

## Building

### Prerequisites

1. **Install ESP-IDF** (v5.0 or later):
   ```bash
   # Follow official guide: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/
   # Quick install on Linux:
   mkdir -p ~/esp
   cd ~/esp
   git clone --recursive https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh esp32s3
   . ./export.sh
   ```

2. **Verify installation**:
   ```bash
   idf.py --version
   # Should show ESP-IDF v5.x or later
   ```

### Build Steps

```bash
cd esp32_usb_host

# Set target to ESP32-S3
idf.py set-target esp32s3

# Configure (optional - defaults should work)
idf.py menuconfig

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with your ESP32-S3's serial port.

## Testing

### Option 1: Monitor with ESP-IDF (Serial Console)

The simplest way to verify it's working:

```bash
# Flash and monitor in one command
idf.py -p /dev/ttyUSB0 flash monitor

# Expected output when USB keyboard is connected:
# I (1234) usb_host_hid: HID Device connected: VID=0x046D, PID=0xC534, Protocol=Keyboard
# I (1235) usb_host_hid: Sent keyboard report: mod=00 keys=[00 00 00 00 00 00]
```

### Option 2: Monitor UART Output

To see the actual UART packets (requires second device or USB-UART adapter):

**Hardware setup:**
```
ESP32-S3 #1                USB-UART Adapter
============                =================
GPIO3 (TX) -------------->  RX
GPIO4 (RX) <-------------  TX
GND ----------------------  GND
```

**Monitor packets:**
```bash
# Using provided Python script
./tools/uart_monitor.py /dev/ttyUSB1 921600

# Expected output when typing:
# STATUS: USB Host Ready
# KEYBOARD: mod=none, keys=[0x04]           # Pressed 'a'
# KEYBOARD: mod=none, keys=[none]           # Released 'a'
# KEYBOARD: mod=LSHIFT, keys=[0x04]         # Pressed Shift+a
# MOUSE: buttons=LEFT, x=  +5, y=  -3       # Mouse movement
```

### Option 3: Raw UART Dump

```bash
# View raw UART data (hex)
screen /dev/ttyUSB1 921600

# Or with minicom
minicom -D /dev/ttyUSB1 -b 921600
```

## Troubleshooting

### USB Device Not Detected

**Problem**: No "HID Device connected" message

**Solutions**:
1. Check USB OTG cable is correct type (not all USB cables support OTG)
2. Ensure ESP32-S3 board has USB OTG support (DevKitC-1 does)
3. Check USB device is HID keyboard/mouse (not composite device)
4. Try different USB device
5. Check serial console for errors

### UART Checksum Errors

**Problem**: `uart_monitor.py` shows "Checksum mismatch"

**Solutions**:
1. Verify baud rate is 921600 on both sides
2. Check wiring (TX→RX, RX→TX, GND→GND)
3. Try lower baud rate (edit `uart_protocol.c` and `uart_monitor.py`)
4. Ensure cables are not too long (< 30cm for 921600 baud)

### Build Errors

**Problem**: `#include "class/hid/hid_host.h"` not found

**Solution**: Your ESP-IDF version may be too old. Update to v5.0+:
```bash
cd ~/esp/esp-idf
git pull
git submodule update --init --recursive
./install.sh esp32s3
```

## Next Steps

Once this PoC is working:

1. **Create ESP32-S3 #2** (USB device side) to receive UART and act as keyboard to PC
2. **Add state machine** from original Pico project (double-shift commands, etc.)
3. **Add NVS storage** for key definitions
4. **Add WiFi/HTTP** for configuration
5. **Add MQTT** for home automation integration

## File Structure

```
esp32_usb_host/
├── CMakeLists.txt              # Top-level build config
├── main/
│   ├── CMakeLists.txt          # Main component config
│   ├── main.c                  # Entry point
│   ├── usb_host_hid.c          # USB host HID handler
│   └── usb_host_hid.h
├── components/
│   └── uart_protocol/          # Reusable UART protocol library
│       ├── CMakeLists.txt
│       ├── uart_protocol.c
│       └── include/
│           └── uart_protocol.h
└── tools/
    └── uart_monitor.py         # UART packet decoder

```

## References

- [ESP-IDF USB Host Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_host.html)
- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [USB HID Usage Tables](https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf)
- [Original Pico Project](../README.md)
