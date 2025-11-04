# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a **proof-of-concept** USB HID proxy for Raspberry Pi Pico W that intercepts keyboard input between a physical keyboard and host computer. It provides encrypted storage of key definitions (text expansion/macros) in flash memory, with optional NFC tag authentication and WiFi-based configuration via HTTP API.

**WARNING**: This is explicitly marked as "Do NOT use in production" with known security issues including lack of buffer overflow protection and basic encryption implementation.

## Architecture

### Dual-Core Design

The application uses both cores of the RP2040:

- **Core 0** (`main()` in hid_proxy.c): Handles TinyUSB device stack (acts as keyboard to host computer), main state machine, NFC operations, and flash encryption/decryption
- **Core 1** (`core1_main()` in usb_host.c): Handles TinyUSB host stack via PIO-USB (receives input from physical keyboard)

Communication between cores uses three queues:
- `keyboard_to_tud_queue`: HID reports from physical keyboard (Core 1) to device processing (Core 0)
- `tud_to_physical_host_queue`: Processed reports from Core 0 to host computer
- `leds_queue`: LED status updates from host to physical keyboard

### Key Components

**State Machine** (hid_proxy.h:45-55): The main state transitions through:
- `locked`: Encrypted data locked, awaiting passphrase or NFC authentication
- `entering_password`: User entering passphrase to decrypt key definitions
- `normal`: Unlocked, keystrokes pass through normally
- `seen_magic`/`expecting_command`: "Double shift" command mode activated
- `defining`: Recording new key definition

**Key Definitions** (key_defs.c): Stores mappings from single keystrokes to sequences of HID reports. Definitions are stored in `kb.local_store->keydefs` as a variable-length array of `keydef_t` structures.

**Macro Parsing/Serialization** (macros.c/h): Provides text format parser and serializer to convert between binary keydef format and human-readable text format with syntax: `trigger { "text" MNEMONIC ^C [mod:key] }`. These functions will be used for future network-based configuration (HTTP API).

**Encryption** (encryption.c/h): Uses SHA256 for key derivation from passphrase and AES (via tiny-AES-c) for encrypting key definitions stored in flash. IV is randomly generated and stored alongside encrypted data.

**Flash Storage** (flash.c): Persists encrypted key definitions at `FLASH_STORE_OFFSET` (512KB). Uses Pico SDK's `flash_safe_execute` for multicore-safe flash writes.

**NFC Authentication** (nfc_tag.c): Interfaces with PN532 NFC reader via I2C (GPIO 4/5) to read/write 16-byte encryption keys from Mifare Classic tags. Supports multiple known authentication keys for tag access.

**USB Configuration** (tusb_config.h): Configures device stack with keyboard, mouse, and CDC interfaces. Host stack (via PIO-USB on GPIO2/3) configured for keyboard/mouse input.

**WiFi Configuration** (wifi_config.c/h): Manages WiFi connection using CYW43 chip on Pico W. Stores WiFi credentials in flash (separate from encrypted keydefs). Provides non-blocking WiFi connection that runs in background without affecting keyboard functionality.

**HTTP Server** (http_server.c/h): Implements lwIP-based HTTP server for macro configuration. Provides REST-like endpoints for GET/POST of macros in text format. Integrates with physical unlock system (both-shifts+HOME) for security.

**Web Access Control**: Requires physical presence (both-shifts+HOME keystroke) to enable web access for 5 minutes. Prevents remote configuration without physical keyboard access.

## Building

### Prerequisites
- **Raspberry Pi Pico W** (with CYW43 WiFi chip)
- Pico SDK installed at `/home/paul/pico/pico-sdk` (or update `PICO_SDK_PATH` in CMakeLists.txt)
- Submodules initialized: `Pico-PIO-USB`, `tiny-AES-c`, `tinycrypt`

### Build Commands
```bash
# Initial setup
git submodule update --init --recursive
mkdir build && cd build
cmake ..

# Build
make

# Flash via OpenOCD (with debug probe)
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "adapter speed 5000" \
  -c "program hid_proxy.elf verify reset exit"

# Alternative: Flash via bootloader
# Hold both shifts + PAUSE at runtime to enter bootloader mode
# Then copy .uf2 file to RPI-RP2 drive
```

### Debug/Serial
- UART0 debug output on default GPIO pins (0/1)
- Serial console: `minicom -D /dev/ttyACM0 -b 115200`
- USB CDC also available when enabled (currently disabled in CMakeLists.txt:60)

## "Double Shift" Commands

The UI is activated by pressing both shift keys simultaneously, then releasing all keys. The next keystroke determines the action (see README.md):

- `ENTER`: Start passphrase entry to unlock (when locked)
- `INSERT`: Change passphrase and re-encrypt key definitions (when unlocked)
- `ESC`: Cancel operation
- `DEL`: Erase everything (flash + encryption key)
- `END`: Lock key definitions
- `=`: Start defining/redefining a key (interactive mode)
- `PRINT`: Write encryption key to NFC tag
- `HOME`: Enable web access for 5 minutes (WiFi/HTTP configuration)
- `PAUSE` (with both shifts held): Reboot to bootloader for flashing

## Macro Text Format

The text format for macros (used for future HTTP/network configuration):
- `trigger { commands... }` - whitespace flexible
- `"text"` - quoted strings for typing (supports `\"` and `\\` escapes)
- `MNEMONIC` - special keys (ENTER, ESC, TAB, F1-F24, arrows, PAGEUP, etc.)
- `^C` - Ctrl+key shorthand (^A through ^Z)
- `[mod:key]` - explicit HID report in hex
- Triggers: single char (`a`), mnemonic (`F1`), or hex (`0x04`)

**Example:**
```
a { "Hello!" }
F1 { "Help" ENTER }
F2 { ^C "text" ^V }
```

## Important Code Locations

- Main state machine: `handle_keyboard_report()` in key_defs.c
- Key definition evaluation: `evaluate_keydef()` in key_defs.c (called from state machine)
- Interactive key definition: `start_define()` in key_defs.c
- Physical unlock trigger: key_defs.c (both-shifts+HOME handler)
- Macro parser: `parse_macros()` in macros.c:262
- Macro serializer: `serialize_macros()` in macros.c:321
- Flash encryption/decryption: encryption.c with `store_encrypt()`/`store_decrypt()`
- NFC state machine: `nfc_task()` in nfc_tag.c:373
- USB host enumeration: `tuh_hid_mount_cb()` in usb_host.c:74
- WiFi initialization: `wifi_init()` in wifi_config.c
- WiFi task loop: `wifi_task()` in wifi_config.c (monitors connection, web timeout)
- HTTP server init: `http_server_init()` in http_server.c
- HTTP GET /macros.txt: `fs_open_custom()` in http_server.c
- HTTP POST /macros.txt: `http_post_*()` handlers in http_server.c
- HTTP GET /status: `status_cgi_handler()` in http_server.c

## Configuration Constants

- `FLASH_STORE_OFFSET`: 512KB (hid_proxy.h:17)
- `FLASH_STORE_SIZE`: One flash sector (4KB) (hid_proxy.h:12)
- `WIFI_CONFIG_OFFSET`: FLASH_STORE_OFFSET + 4KB (wifi_config.c)
- `WIFI_CONFIG_SIZE`: One flash sector (4KB) (wifi_config.h)
- `IDLE_TIMEOUT_MILLIS`: 120 minutes before auto-lock (hid_proxy.h:20)
- Web access timeout: 5 minutes (wifi_config.c:web_access_enable)
- NFC key storage address: Block `0x3A` on Mifare tag (nfc_tag.c:18)
- I2C pins: SDA=4, SCL=5; PIO-USB pins: DP=2 (nfc_tag.c:12-13, usb_host.c:38)
- mDNS hostname: `hidproxy.local` (wifi_config.c:106)

## Known Issues/TODOs

From README.md and code comments:
1. No buffer overflow protection on keystroke storage
2. Basic encryption implementation (not production-ready)
3. Poor user interface with no status feedback
4. Code quality issues acknowledged by author
5. Queue overflow handling needed (hid_proxy.c comment)
6. USB stdio disabled for production (CMakeLists.txt:84-85 TODOs)

## WiFi/HTTP Configuration

**Status**: ✅ Implemented

The device now supports WiFi-based configuration via HTTP API. See WIFI_SETUP.md for complete guide.

**Key features:**
- Non-blocking WiFi connection (keyboard stays responsive)
- HTTP endpoints: GET/POST /macros.txt, GET /status
- Physical unlock required (both-shifts+HOME) for 5-minute web access
- mDNS responder at `hidproxy.local`
- Text format upload/download for bulk editing
- No USB re-enumeration needed

**Usage:**
```bash
# 1. Press both-shifts+HOME on keyboard to enable web access
# 2. Download macros
curl http://hidproxy.local/macros.txt > macros.txt
# 3. Edit and upload
curl -X POST http://hidproxy.local/macros.txt --data-binary @macros.txt
# 4. Check status
curl http://hidproxy.local/status
```

## Future Development

See CONFIGURATION_OPTIONS.md for additional planned features:
- Serial console for WiFi setup (currently requires manual flash programming or a .env file at build time)
- MQTT publishing of keystroke events to Home Assistant
- Simple web UI for in-browser macro editing
- HTTP Basic Auth (optional secondary protection layer)
