# ESP32-S3 USB Host PoC - Project Summary

## What We Built

A minimal proof-of-concept ESP32-S3 application that:
1. Acts as USB host for HID keyboards and mice (using native ESP32-S3 USB OTG)
2. Forwards all HID reports over UART at 921600 baud using a simple packet protocol
3. No state machine, no WiFi, no encryption - just pure passthrough

This validates the basic architecture before adding complexity.

## Key Differences from Pico

| Aspect | Pico (Current) | ESP32-S3 #1 (New) |
|--------|----------------|-------------------|
| **USB Host** | PIO-USB (software) | Native USB OTG (hardware) |
| **Core Communication** | Pico SDK queue (dual-core) | UART packets (inter-chip) |
| **Task Model** | `multicore_launch_core1()` | FreeRTOS tasks |
| **USB Library** | TinyUSB (custom PIO config) | ESP-IDF USB Host + HID Host |
| **Serial Debug** | UART0 on GPIO0/1 | USB-Serial (automatic) |

## Project Structure

```
esp32_usb_host/
├── CMakeLists.txt                          # ESP-IDF project config
├── main/
│   ├── main.c                              # Entry point, UART init, main loop
│   ├── usb_host_hid.c                      # USB host HID event handlers
│   ├── usb_host_hid.h
│   └── CMakeLists.txt
├── components/
│   └── uart_protocol/                      # Reusable UART library
│       ├── uart_protocol.c                 # Send/receive packets
│       ├── include/uart_protocol.h         # Protocol definitions
│       └── CMakeLists.txt
├── tools/
│   └── uart_monitor.py                     # Python UART packet decoder
├── README.md                               # Full documentation
├── QUICKSTART.md                           # Build/flash instructions
└── .gitignore
```

## Component Design

### uart_protocol (Reusable Component)

**Purpose**: Shared library for packet-based UART communication

**API**:
- `uart_protocol_init(uart_num, tx_pin, rx_pin)` - Initialize UART hardware
- `uart_send_packet(type, data, len)` - Send framed packet with checksum
- `uart_recv_packet(packet, timeout)` - Receive and validate packet

**Features**:
- Simple framing: `[START][TYPE][LEN][PAYLOAD][CHECKSUM]`
- XOR checksum for error detection
- Sync byte recovery (0xAA)
- 921600 baud (configurable)

**Why reusable**: ESP32-S3 #2 will use the exact same component to receive packets.

### usb_host_hid (Main Logic)

**Purpose**: USB host for HID devices, forwards to UART

**Flow**:
1. Initialize USB Host Library
2. Install HID Host driver
3. Register keyboard/mouse protocol handlers
4. On HID report received → `uart_send_packet()`

**Callbacks**:
- `hid_host_device_event()` - Device connect/disconnect
- `hid_keyboard_report_callback()` - Forward keyboard reports
- `hid_mouse_report_callback()` - Forward mouse reports

## Testing Strategy

### Level 1: Serial Console Only
- Flash ESP32-S3
- Monitor serial output
- Plug in USB keyboard
- Verify "HID Device connected" and "Sent keyboard report" messages

### Level 2: UART Loopback (Next Step)
- Wire GPIO3 → GPIO4 (TX → RX on same device)
- Run `uart_monitor.py` on same ESP32-S3
- Verify packets are correctly framed and checksummed

### Level 3: Dual-Device (Future)
- Build ESP32-S3 #2 (USB device)
- Wire UART between ESP32-S3 #1 and #2
- Full keyboard → ESP32 #1 → UART → ESP32 #2 → PC

## Code Reuse from Pico Project

**Currently NOT ported** (intentionally kept simple for PoC):
- ❌ State machine (`key_defs.c`) - will add in Phase 2
- ❌ Encryption (`encryption.c`, `kvstore_init.c`)
- ❌ Key definitions (`keydef_store.c`, `macros.c`)
- ❌ WiFi (`wifi_config.c`, `http_server.c`, `mqtt_client.c`)
- ❌ NFC (`nfc_tag.c`)

**Ready to port** (code is platform-independent):
- ✅ USB descriptors (`usb_descriptors.c`) - compatible with TinyUSB
- ✅ Macro parser (`macros.c`) - pure C, no hardware deps
- ✅ State machine logic - just needs queue→UART translation

## Next Steps

### Immediate (To complete PoC)

1. **Test current build**:
   ```bash
   cd esp32_usb_host
   idf.py set-target esp32s3
   idf.py build
   idf.py -p /dev/ttyUSB0 flash monitor
   ```

2. **Verify USB keyboard detection** - look for "HID Device connected"

3. **Test UART output**:
   - Option A: Connect second ESP32/adapter, run `uart_monitor.py`
   - Option B: Loopback test (wire TX→RX, monitor same device)

### Phase 2: ESP32-S3 #2 (USB Device)

Create companion project:
```
esp32_usb_device/
├── components/uart_protocol/  (symlink to esp32_usb_host/components/uart_protocol)
├── main/
│   ├── main.c                 # UART RX → USB device TX
│   └── usb_device_hid.c       # TinyUSB device stack
```

### Phase 3: Add Intelligence

Port from Pico project incrementally:
1. State machine (double-shift commands)
2. Key definitions with NVS storage
3. Macro playback
4. WiFi/HTTP configuration
5. MQTT integration

## Build/Flash Commands Reference

```bash
# One-time setup
cd ~/esp/esp-idf && . ./export.sh
cd /path/to/hid-proxy2/esp32_usb_host
idf.py set-target esp32s3

# Regular build/flash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor

# Clean build
idf.py fullclean
idf.py build

# Configuration menu
idf.py menuconfig

# Monitor only (after flash)
idf.py -p /dev/ttyUSB0 monitor
```

## Troubleshooting Quick Reference

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| Build error: "hid_host.h not found" | Old ESP-IDF | Update to v5.0+ |
| "USB device not detected" | Wrong cable or bad device | Try different keyboard/cable |
| "Permission denied /dev/ttyUSB0" | User not in dialout group | `sudo usermod -a -G dialout $USER` |
| Checksum errors on UART | Baud mismatch or wiring | Verify 921600 baud, check connections |
| No serial output | Wrong USB port selected | Try `-p /dev/ttyUSB1` or check `dmesg` |

## Performance Notes

**Latency**:
- USB polling: ~1ms (USB Low Speed = 125Hz)
- UART @ 921600: ~86μs per byte
- Total keyboard report latency: <2ms

**Bandwidth**:
- Keyboard: 8 bytes @ ~100 reports/sec = 800 bytes/sec
- Mouse: 3-5 bytes @ ~125 reports/sec = 625 bytes/sec
- UART capacity: 92KB/sec
- **Utilization**: <2% of available bandwidth

**Conclusion**: UART is massively over-provisioned for this use case. Could easily go down to 115200 baud if needed.

## Known Limitations (PoC Only)

1. ⚠️ No LED feedback to keyboard (host → device LED reports not implemented)
2. ⚠️ No error recovery on UART (checksum failure = drop packet)
3. ⚠️ No flow control (assumes UART is always faster than USB)
4. ⚠️ Only boot protocol (report mode not supported)
5. ⚠️ Single keyboard + single mouse (no multi-device support)

These are acceptable for PoC. Will address in full implementation.

## Success Criteria

**PoC is successful if**:
- ✅ ESP32-S3 detects USB keyboard
- ✅ Keyboard reports appear in serial console
- ✅ UART packets are correctly framed and checksummed
- ✅ Can decode packets with `uart_monitor.py`

**Ready for Phase 2 (ESP32-S3 #2) when**:
- ✅ Above criteria met
- ✅ Tested with at least one keyboard and one mouse
- ✅ Verified UART packet integrity over 1+ hour of use

---

**Status**: Ready to build and test
**Date**: 2025-01-02
**Author**: Generated by Claude Code
