# Quick Start Guide - ESP32-S3 #2 (USB Device)

## Prerequisites

- ✅ ESP-IDF installed (same as ESP32-S3 #1)
- ✅ ESP32-S3 #1 built and tested
- ✅ UART protocol verified working

## Build and Flash

```bash
# Set up environment
cd ~/esp/esp-idf
. ./export.sh

# Navigate to project
cd /path/to/hid-proxy2/esp32_usb_device

# First time only
idf.py set-target esp32s3

# Build and flash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Test Sequence

### Test 1: USB Device Enumeration

1. Flash ESP32-S3 #2
2. Connect to PC via USB
3. Check device appears:

```bash
# Linux
lsusb | grep CAFE
# Should show: ID cafe:4001 ESP32-S3 HID Proxy Device

# Windows: Device Manager → Human Interface Devices
# macOS: System Information → USB
```

✅ **Pass**: Device shows up as keyboard + mouse
❌ **Fail**: Check USB cable, try different port

### Test 2: UART Reception

1. Keep ESP32-S3 #2 connected to PC
2. Connect UART adapter/ESP32 #1 to GPIO3/4
3. Run test script:

```bash
cd ../esp32_usb_host
./tools/test_uart_protocol.py /dev/ttyUSB1 921600
```

4. Watch ESP32 #2 serial console for:
```
I (xxxx) usb_device_hid: Sent keyboard report: mod=0x00 keys=[04 ...]
```

5. **Open text editor on PC** - should type 'a' when test runs

✅ **Pass**: Keystrokes appear in text editor
❌ **Fail**: Check UART wiring, verify RX/TX crossed

### Test 3: Full Passthrough

**Wiring:**
```
ESP32 #1         ESP32 #2
========         ========
GPIO3 ---------> GPIO4
GPIO4 <--------- GPIO3
GND ------------- GND

USB Keyboard → ESP32 #1
ESP32 #2 → USB to PC
```

**Test:**
1. Start ESP32 #1: `cd ../esp32_usb_host && idf.py monitor`
2. Start ESP32 #2: `cd ../esp32_usb_device && idf.py monitor`
3. Plug USB keyboard into ESP32 #1
4. Type on keyboard
5. **Keystrokes should appear on PC!**

✅ **Pass**: Full passthrough working!
❌ **Fail**: Check all connections, review both serial consoles

## Expected Output

**ESP32-S3 #2 Serial Console:**
```
I (1234) main: ESP32-S3 UART to USB Device Passthrough PoC
I (1235) uart_protocol: UART protocol initialized on UART1 (TX:17, RX:18, baud:921600)
I (1236) usb_device_hid: Initializing USB Device HID
I (1237) usb_device_hid: USB Device HID ready
I (1238) usb_device_hid: UART RX task started
I (1239) usb_device_hid: USB HID task started

# When typing:
I (5678) usb_device_hid: Sent keyboard report: mod=0x00 keys=[04 00 00 00 00 00]
I (5690) usb_device_hid: Sent keyboard report: mod=0x00 keys=[00 00 00 00 00 00]
```

## Troubleshooting Quick Reference

| Problem | Quick Fix |
|---------|-----------|
| "Permission denied /dev/ttyUSB0" | `sudo usermod -a -G dialout $USER`, logout/login |
| Device not showing in `lsusb` | Try different USB cable/port |
| No keystrokes on PC | Check UART TX↔RX are crossed |
| Build error: esp_tinyusb.h | Update ESP-IDF to v5.0+ |
| Queue full warnings | Normal if typing very fast |

## Pin Reference

| Function | GPIO | Connects To |
|----------|------|-------------|
| USB Device D+ | 19 | PC (auto) |
| USB Device D- | 20 | PC (auto) |
| UART TX | 3 | ESP32 #1 GPIO4 |
| UART RX | 4 | ESP32 #1 GPIO3 |
| GND | GND | ESP32 #1 GND |

## Success Criteria

✅ Device enumerated on PC as keyboard + mouse
✅ Test script sends keystrokes to PC
✅ Full passthrough: Physical keyboard → PC works
✅ Both ESP32 serial consoles show activity

## Next: Add Intelligence

Once passthrough works, you can:
1. Port state machine (double-shift commands)
2. Add macro expansion
3. Add WiFi configuration
4. Add MQTT integration
