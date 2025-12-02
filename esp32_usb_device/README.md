# ESP32-S3 UART to USB Device Passthrough (PoC)

Companion to ESP32-S3 USB Host project. Receives HID reports over UART and acts as USB keyboard/mouse to PC.

## ⚡ Architectural Note

**This device will receive ALL application logic** (state machine, macros, WiFi, storage) in future phases.

**Current state**: Simple UART→USB passthrough (PoC)
**Future**: State machine, macro expansion, NVS storage, WiFi/HTTP/MQTT

**See [ARCHITECTURE_DECISION.md](../ARCHITECTURE_DECISION.md) for rationale**

## What It Does (Current PoC)

1. **UART Receiver**: Listens for HID report packets from ESP32-S3 #1
2. **USB Device**: Presents as USB keyboard + mouse to host PC
3. **Report Forwarding**: Forwards keyboard/mouse reports from UART to USB

Combined with ESP32-S3 #1, this creates a complete keyboard passthrough system.

**Next**: Port state machine from Pico project to this device.

## Hardware Requirements

- **ESP32-S3-DevKitC-1** (or similar ESP32-S3 board)
- **USB cable** to PC (for power and USB device connection)
- **UART connection** to ESP32-S3 #1 (3 wires: TX, RX, GND)

## Pin Configuration

- **USB Device**: Native USB on GPIO19/GPIO20 (hardware pins, automatic)
- **UART TX**: GPIO3 (connects to ESP32 #1 GPIO4)
- **UART RX**: GPIO4 (connects to ESP32 #1 GPIO3)
- **UART Baud**: 921600

## Wiring Diagram

```
ESP32-S3 #1 (Host)         ESP32-S3 #2 (Device)
=====================      =======================
GPIO3 (TX) -------------> GPIO4 (RX)
GPIO4 (RX) <------------- GPIO3 (TX)
GND ---------------------- GND

[USB Keyboard]             [USB to PC]
      ↓                          ↓
  ESP32-S3 #1 ===UART==> ESP32-S3 #2
  (USB Host)              (USB Device)
```

## Building

### Prerequisites

1. **Install ESP-IDF** (v5.0 or later) - same as ESP32-S3 #1
2. **Build ESP32-S3 #1 first** to verify UART protocol works

### Build Steps

```bash
cd esp32_usb_device

# Set target to ESP32-S3
idf.py set-target esp32s3

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

## Testing

### Phase 1: Verify USB Device Enumeration

1. Flash ESP32-S3 #2
2. Connect to PC via USB
3. Check PC recognizes device:

**Linux:**
```bash
lsusb | grep "CAFE:4001"
# Should show: Bus XXX Device XXX: ID cafe:4001 ESP32-S3 HID Proxy Device

# View detailed info
lsusb -v -d cafe:4001
```

**Windows:**
```
Device Manager → Human Interface Devices
Should show "HID Proxy Device" (keyboard and mouse)
```

**macOS:**
```bash
system_profiler SPUSBDataType | grep -A 10 "HID Proxy"
```

### Phase 2: Test UART Reception

1. **Keep ESP32-S3 #2 running** (connected to PC)
2. **Wire UART to ESP32-S3 #1** (or USB-UART adapter)
3. **Send test packets**:

```bash
# From another terminal, using ESP32 #1's test tool
cd ../esp32_usb_host
./tools/test_uart_protocol.py /dev/ttyUSB1 921600
```

4. **Monitor ESP32 #2 serial output**:
```
I (5678) usb_device_hid: Status from host: UART Protocol Test Started
I (5700) usb_device_hid: Sent keyboard report: mod=0x00 keys=[04 00 00 00 00 00]
I (5750) usb_device_hid: Sent keyboard report: mod=0x00 keys=[00 00 00 00 00 00]
```

5. **Verify keystrokes appear on PC** (open text editor, should type 'a')

### Phase 3: End-to-End Test (Full Passthrough)

1. **Flash both ESP32-S3 boards**
2. **Wire UART between them** (see wiring diagram above)
3. **Connect USB keyboard to ESP32 #1**
4. **Connect ESP32 #2 to PC via USB**
5. **Type on physical keyboard** → should appear on PC!

**Expected flow:**
```
Physical Keyboard → ESP32 #1 (USB Host)
                         ↓
                    UART @ 921600
                         ↓
                    ESP32 #2 (USB Device)
                         ↓
                    Host PC
```

## Troubleshooting

### USB Device Not Recognized

**Problem**: PC doesn't see USB device

**Solutions**:
1. Check USB cable supports data (not just power)
2. Try different USB port on PC
3. Check serial console for errors: `idf.py monitor`
4. Verify TinyUSB initialized: Look for "USB Device initialized"

### No Keystrokes on PC

**Problem**: Device detected but no keystrokes

**Solutions**:
1. Check UART wiring (TX↔RX crossed, GND connected)
2. Verify UART packets arriving: Check serial console for "Sent keyboard report"
3. Test with `test_uart_protocol.py` to isolate issue
4. Try different text editor/application on PC

### Keyboard LEDs Not Working

**Problem**: Num Lock/Caps Lock don't work

**Solutions**:
This is expected - LED feedback not implemented in PoC. Will be added in full version.

### Build Errors

**Problem**: `esp_tinyusb.h not found`

**Solution**: Your ESP-IDF may be too old. Update to v5.0+:
```bash
cd ~/esp/esp-idf
git pull
git submodule update --init --recursive
./install.sh esp32s3
```

## Protocol Details

### Received Packet Types

- `0x01` - **Keyboard Report** (8 bytes): `[modifier][reserved][key0-5]`
  - Forwarded to USB as HID keyboard report

- `0x02` - **Mouse Report** (3-5 bytes): `[buttons][x][y][wheel][pan]`
  - Forwarded to USB as HID mouse report

- `0x04` - **Status** (variable): Text messages
  - Logged to serial console

### USB Device Details

**Vendor ID**: 0xCAFE
**Product ID**: 0x4001
**Manufacturer**: ESP32-S3
**Product**: HID Proxy Device

**Interfaces**:
- Interface 0: HID Keyboard (boot protocol)
- Interface 1: HID Mouse (boot protocol)

## Known Limitations (PoC Only)

1. ⚠️ **No LED feedback** - Num/Caps/Scroll Lock LEDs not sent back to keyboard
2. ⚠️ **Boot protocol only** - No support for advanced HID features
3. ⚠️ **No report queuing on PC side** - If PC is slow, reports may be dropped
4. ⚠️ **No UART error recovery** - Checksum failures drop packet

These are acceptable for PoC. Will be addressed in full implementation.

## File Structure

```
esp32_usb_device/
├── CMakeLists.txt              # Project config
├── main/
│   ├── CMakeLists.txt          # Main component
│   ├── main.c                  # Entry point
│   ├── usb_device_hid.c        # USB device + UART RX
│   ├── usb_device_hid.h
│   ├── usb_descriptors.c       # USB descriptors (VID/PID/interfaces)
│   └── tusb_config.h           # TinyUSB configuration
└── components/
    └── uart_protocol/          # Symlink to ../esp32_usb_host/components/uart_protocol
```

## Performance

**Latency**:
- UART reception: ~86μs per byte @ 921600 baud
- Queue processing: <100μs
- USB HID polling: 1ms (1000Hz for keyboard, 125Hz for mouse)
- **Total latency**: <2ms keyboard, <9ms mouse

**Throughput**:
- Keyboard: 8 bytes @ 1000 reports/sec max = 8KB/sec
- Mouse: 5 bytes @ 125 reports/sec max = 625 bytes/sec
- UART capacity: 92KB/sec
- **Bottleneck**: USB HID polling rate (as designed)

## Next Steps

Once end-to-end passthrough works:

1. **Add state machine** from Pico project
2. **Add macro playback** (expand single keypress → multiple reports)
3. **Add LED feedback** (PC → UART → Keyboard)
4. **Add NVS storage** for key definitions
5. **Add WiFi configuration** on ESP32 #1

## References

- [ESP-IDF TinyUSB Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_device.html)
- [TinyUSB HID Device](https://github.com/hathach/tinyusb/tree/master/examples/device/hid_composite)
- [USB HID Specification](https://www.usb.org/hid)
- [ESP32-S3 #1 README](../esp32_usb_host/README.md)
