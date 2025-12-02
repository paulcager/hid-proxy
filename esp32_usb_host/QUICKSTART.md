# Quick Start Guide

## 1. Install ESP-IDF (one-time setup)

```bash
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
```

## 2. Set up environment (every new terminal)

```bash
cd ~/esp/esp-idf
. ./export.sh
```

## 3. Build and flash

```bash
cd /path/to/hid-proxy2/esp32_usb_host

# First time only
idf.py set-target esp32s3

# Build and flash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## 4. Connect hardware

1. **Connect ESP32-S3 to computer** via USB (for power and serial console)
2. **Connect USB keyboard** to ESP32-S3 via USB OTG cable/adapter
3. **Wait for "HID Device connected"** message in serial console

## 5. Expected output

```
I (1234) main: ESP32-S3 USB Host to UART Passthrough PoC
I (1235) uart_protocol: UART protocol initialized on UART1 (TX:17, RX:18, baud:921600)
I (1240) usb_host_hid: USB Host initialized, waiting for HID devices...
I (5678) usb_host_hid: HID Device connected: VID=0x046D, PID=0xC534, Protocol=Keyboard

# When you press keys:
I (6000) usb_host_hid: Sent keyboard report: mod=00 keys=[04 00 00 00 00 00]
I (6100) usb_host_hid: Sent keyboard report: mod=00 keys=[00 00 00 00 00 00]
```

## 6. Monitor UART output (optional)

**Connect second device/adapter:**
- ESP32-S3 GPIO3 (TX) → Other device RX
- ESP32-S3 GPIO4 (RX) → Other device TX
- ESP32-S3 GND → Other device GND

**Run monitor:**
```bash
./tools/uart_monitor.py /dev/ttyUSB1 921600
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "No such file or directory: 'idf.py'" | Run `. ~/esp/esp-idf/export.sh` |
| "USB device not detected" | Check OTG cable, try different keyboard |
| "Permission denied /dev/ttyUSB0" | `sudo usermod -a -G dialout $USER` then logout/login |
| Build errors | Update ESP-IDF: `cd ~/esp/esp-idf && git pull` |

## Pin Reference

| Function | GPIO | Notes |
|----------|------|-------|
| USB Host D+ | 19 | Hardware fixed |
| USB Host D- | 20 | Hardware fixed |
| UART TX | 17 | Configurable in code |
| UART RX | 18 | Configurable in code |
| Serial Console | USB | Automatic |

## Next: Build ESP32-S3 #2 (USB Device)

Once this works, create the matching receiver that reads UART and acts as USB keyboard to PC.
