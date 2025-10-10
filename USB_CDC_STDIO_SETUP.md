# USB CDC stdio Setup - Implementation Summary

## Status: ✅ WORKING

USB CDC stdio debugging has been successfully implemented with conditional compilation support.

## Features Implemented

### 1. USB CDC stdio Support
- Added `cdc_stdio_lib` as a submodule for TinyUSB host compatibility
- Conditional compilation via `ENABLE_USB_STDIO` flag
- 10-second startup delay (with USB processing) to allow CDC enumeration
- Printf/scanf over USB serial at `/dev/ttyACM0` (115200 baud)

### 2. Build System Integration
- `./build.sh --stdio` flag to enable USB CDC debugging
- `./build.sh --debug` flag now properly sets CMake build type
- Automatic detection and display of build configuration

### 3. WiFi/HTTP Support (Pico W)
- Full WiFi connectivity with mDNS at `hidproxy.local`
- HTTP server for macro configuration
- Physical unlock (both-shifts+HOME) for web access
- Build-time WiFi credentials via `.env` file

### 4. DMA Conflict Resolution
- PIO-USB moved to DMA channel 2
- CYW43 WiFi uses DMA channel 0
- NFC I2C coexists with WiFi
- All features work simultaneously

### 5. lwIP Memory Pool Optimization
- Increased `MEMP_NUM_SYS_TIMEOUT` from 6 to 16
- Added `MEMP_NUM_NETBUF` pool size of 8
- Prevents pool exhaustion with mDNS + HTTP server

## Build Commands

### Development Build (with USB debugging)
```bash
./build.sh --stdio --board pico_w
```

### Production Build (no USB debugging)
```bash
./build.sh --board pico_w
```

### Debug Build (with symbols, no optimization)
```bash
./build.sh --debug --stdio --board pico_w
```

### Clean Build
```bash
./build.sh --clean --stdio --board pico_w
```

## Runtime Behavior

### With `--stdio` Flag
```
USB CDC stdio initialized
Core 0 (tud) running
Build config: WiFi=YES USB_STDIO=YES
Delaying WiFi initialization for 10 seconds (USB CDC debug mode)...
Starting WiFi initialization...
Using WiFi config from build-time values
Initializing WiFi (CYW43)...
WiFi initialized, connecting to 'YourSSID'...
WiFi connected! IP: 192.168.x.x
mDNS responder started: hidproxy.local
Starting HTTP server...
HTTP server started
```

### Without `--stdio` Flag
- No USB CDC interface
- No 10-second delay
- Immediate WiFi connection
- Faster boot time

## Connecting to USB CDC stdio

```bash
# Using minicom
minicom -D /dev/ttyACM0 -b 115200

# Using screen
screen /dev/ttyACM0 115200

# Using cu
cu -l /dev/ttyACM0 -s 115200
```

## File Changes

### Modified Files
- `CMakeLists.txt` - Added `ENABLE_USB_STDIO` option, library linking, preprocessor definitions
- `build.sh` - Added `--stdio` and `--debug` flags with proper CMake integration
- `src/hid_proxy.c` - Added CDC initialization, WiFi delay loop, build config display
- `src/usb_host.c` - Changed PIO-USB DMA channel from 0 to 2
- `src/wifi_config.c` - Fixed early-boot flash write issue, removed auto-reboot
- `src/http_server.c` - Added `httpd_cgi_handler` stub for lwIP compatibility
- `include/usb_descriptors.h` - Changed `LIB_PICO_STDIO_USB` → `ENABLE_USB_STDIO`
- `src/usb_descriptors.c` - Changed `LIB_PICO_STDIO_USB` → `ENABLE_USB_STDIO` (3 locations)
- `include/lwipopts.h` - Increased memory pool sizes
- `test/mocks/pico_mocks.h` - Added mock flash storage symbols
- `test/mocks/pico_mocks.c` - Implemented mock flash storage

### New Files
- `cdc_stdio_lib/` - Submodule for USB CDC stdio support
- `CUSTOM_STDIO_DRIVER.md` - Complete implementation guide
- `USB_CDC_STDIO_SETUP.md` - This file

## Technical Details

### DMA Channel Allocation
- **Channel 0**: CYW43 WiFi driver
- **Channel 2**: PIO-USB host stack
- **Other channels**: Available for I2C (NFC), etc.

### USB Interfaces (with --stdio)
- Interface 0: HID Keyboard
- Interface 1: HID Mouse
- Interface 2: CDC Control
- Interface 3: CDC Data

### USB Interfaces (without --stdio)
- Interface 0: HID Keyboard
- Interface 1: HID Mouse

## Known Limitations

1. **Build-time WiFi credentials only** - WiFi config from `.env` is used on every boot, not saved to flash during initialization (to avoid flash write before Core 1 is running)

2. **10-second boot delay in debug builds** - Only when `--stdio` is enabled, allows CDC enumeration before WiFi starts

3. **Binary size increase** - USB CDC stdio adds ~20KB to the firmware

## Troubleshooting

### No /dev/ttyACM0 appears
- Ensure you built with `--stdio` flag
- Check dmesg: `dmesg | tail -20`
- Verify USB descriptors show 4 interfaces (not 2)

### WiFi not connecting
- Check that `.env` file exists with `WIFI_SSID` and `WIFI_PASSWORD`
- Verify build output shows "Using WiFi config from build-time values"
- Check serial output for WiFi error messages

### DMA panic on boot
- Should be resolved - PIO-USB uses DMA channel 2
- If still occurring, check what's claiming DMA channel 0

### lwIP pool exhaustion
- Should be resolved - pool sizes increased
- If still occurring, increase `MEMP_NUM_SYS_TIMEOUT` further in `include/lwipopts.h`

## Success Criteria ✅

All of the following are working:
- [x] USB CDC stdio enumeration and printf output
- [x] WiFi connection with build-time credentials
- [x] mDNS responder at `hidproxy.local`
- [x] HTTP server for macro configuration
- [x] NFC tag authentication (coexists with WiFi)
- [x] PIO-USB host for keyboard input
- [x] No DMA conflicts
- [x] No lwIP pool exhaustion
- [x] Build tests pass (`./test.sh`)
- [x] Conditional compilation (production builds have no USB CDC overhead)

## References

- [CUSTOM_STDIO_DRIVER.md](CUSTOM_STDIO_DRIVER.md) - Detailed implementation guide
- [cdc_stdio_lib GitHub](https://github.com/rppicomidi/cdc_stdio_lib)
- [Pico SDK stdio_driver documentation](https://www.raspberrypi.com/documentation/pico-sdk/structstdio__driver.html)
