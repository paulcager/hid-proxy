# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a **proof-of-concept** USB HID proxy for Raspberry Pi Pico W that intercepts keyboard input between a physical keyboard and host computer. It provides encrypted storage of key definitions (text expansion/macros) in flash memory, with optional NFC tag authentication and WiFi-based configuration via HTTP API.

**IMPORTANT: Backward Compatibility Policy**

This is a personal project with a single user. **Backward compatibility is NOT required.** Breaking changes to storage formats, keydef structures, or configuration are acceptable. Users can always wipe the device (both-shifts + DEL) and reconfigure from scratch. When implementing new features:

- ✅ **DO**: Make clean architectural changes that improve the codebase
- ✅ **DO**: Change storage formats if it makes the design better
- ✅ **DO**: Break APIs between firmware versions
- ❌ **DON'T**: Add complexity just to maintain backward compatibility
- ❌ **DON'T**: Keep migration code after initial implementation

When making breaking changes, simply document in MIGRATION_NOTES.md that users should wipe and reconfigure.

## Board Support

**Supported Hardware**:
- Raspberry Pi Pico (RP2040, no WiFi)
- Raspberry Pi Pico W (RP2040 with WiFi/HTTP)
- Raspberry Pi Pico2 (RP2350, no WiFi)
- Raspberry Pi Pico2 W (RP2350 with WiFi/HTTP)

**Pico2 (RP2350) Status**: Build support is provided via Pico SDK 2.2.0, but PIO-USB compatibility is uncertain and depends on GPIO selection. The project uses GPIO2/3 for PIO-USB host stack. Test thoroughly before deployment.

## Architecture

### Dual-Core Design

The application uses both cores of the RP2040/RP2350:

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

**Key Definitions** (key_defs.c): Stores mappings from single keystrokes to sequences of **mixed actions** (HID reports, MQTT publishes, delays, mouse movements). Definitions are loaded on-demand from kvstore as individual `keydef_t` structures.

**Action System** (hid_proxy.h): Keydefs use an extensible action-based architecture:
- `ACTION_HID_REPORT`: Send keyboard HID report to host
- `ACTION_MQTT_PUBLISH`: Publish message to MQTT broker (WiFi boards only)
- `ACTION_DELAY`: Delay in milliseconds (future)
- `ACTION_MOUSE_MOVE`: Mouse movement/clicks (future, see MOUSE_SUPPORT.md)

Each keydef contains an array of `action_t` structures, allowing macros to mix keyboard input with MQTT automation, delays, and future mouse actions.

**Macro Parsing/Serialization** (macros.c/h): Provides text format parser and serializer to convert between binary keydef format and human-readable text format with syntax: `[public|private] trigger { "text" MNEMONIC ^C MQTT("topic", "msg") [mod:key] }`. Used for HTTP-based configuration via `/macros.txt` endpoint.

**Storage** (kvstore_init.c, keydef_store.c): Uses pico-kvstore for persistent storage with encryption:
- blockdevice_flash: Raw flash access with wear leveling
- kvstore_logkvs: Log-structured key-value store
- **Custom encryption layer** (kvstore_init.c): AES-128-GCM encryption with header system

Key definitions are stored as individual key-value pairs (`keydef.0xHH`) and loaded on-demand, reducing memory usage. Public keydefs work when device is locked; private keydefs require unlock.

**Encryption Architecture** (kvstore_init.c): DIY encryption layer with header-based format:
- **Storage format:** `[header(1)][data...]` where header indicates encryption status
  - `0x00` = unencrypted (public keydefs, WiFi config)
  - `0x01` = encrypted with AES-128-GCM (private keydefs)
- **Encrypted format:** `[0x01][IV(12)][ciphertext][tag(16)]`
- **Algorithm:** AES-128-GCM (authenticated encryption)
- **Key derivation:** PBKDF2-SHA256 from user password with device-unique salt
- **Password validation:** SHA256 hash of derived key stored at `auth.password_hash`

On first use, any password is accepted and its hash is stored. Subsequent unlocks validate against this hash. Wrong passwords are rejected, keeping encryption key out of memory.

**Encryption** (encryption.c/h): PBKDF2 (SHA256-based) key derivation from passphrase. The derived 16-byte key is used by kvstore_init.c for AES-128-GCM operations via mbedtls. Legacy `store_encrypt()`/`store_decrypt()` functions marked obsolete.

**Flash Storage** (flash.c): Minimal file containing only `init_state()` which clears kvstore and resets device to blank state. All persistence is handled by kvstore.

**NFC Authentication** (nfc_tag.c): *Optional feature, disabled by default.* Interfaces with PN532 NFC reader via I2C (GPIO 4/5) to read/write 16-byte encryption keys from Mifare Classic tags. Supports multiple known authentication keys for tag access. Enable with `--nfc` build flag.

**USB Configuration** (tusb_config.h): Configures device stack with keyboard, mouse, and CDC interfaces. Host stack (via PIO-USB on GPIO2/3) configured for keyboard/mouse input.

**WiFi Configuration** (wifi_config.c/h): Manages WiFi connection using CYW43 chip on Pico W. Stores WiFi credentials in kvstore (`wifi.ssid`, `wifi.password`, `wifi.country`) unencrypted. WiFi credentials are not considered sensitive in this application context. Provides non-blocking WiFi connection that runs in background without affecting keyboard functionality.

**HTTP Server** (http_server.c/h): Implements lwIP-based HTTP server for macro configuration. Provides REST-like endpoints for GET/POST of macros in text format. Integrates with physical unlock system (both-shifts+SPACE) for security. Includes POST /unlock endpoint for remote unlocking with password (always available when WiFi connected).

**Web Access Control**: Requires physical presence (Double-shift then SPACE) to enable web access for 5 minutes. Prevents remote configuration without physical keyboard access.

**MQTT Client** (mqtt_client.c/h): Publishes lock/unlock events to MQTT broker for Home Assistant integration. Uses unique topic names based on board ID (e.g., `hidproxy-a3f4/lock`). Supports optional TLS for secure connections. Configured via .env file (MQTT_BROKER, MQTT_PORT, MQTT_USE_TLS). Non-blocking operation runs in lwIP background tasks.

## Building

### Prerequisites
- **Raspberry Pi Pico/Pico W/Pico2/Pico2 W**
- Docker (recommended) or local Pico SDK 2.2.0+
- Submodules initialized: `Pico-PIO-USB`, `pico-kvstore`, `tiny-AES-c`, `tinycrypt`

### Quick Build with Docker
```bash
# Build for default board (Pico W / RP2040)
./build.sh

# Build for specific board
./build.sh --board pico         # RP2040, no WiFi
./build.sh --board pico_w       # RP2040 with WiFi (default)
./build.sh --board pico2        # RP2350, no WiFi
./build.sh --board pico2_w      # RP2350 with WiFi

# Other options
./build.sh --stdio              # Enable USB CDC stdio for debugging
./build.sh --nfc                # Enable NFC tag authentication
./build.sh --clean              # Clean build
./build.sh --debug              # Debug build with symbols
./build.sh --help               # Show all options
```

### Manual Build (without Docker)
```bash
# Initial setup
git submodule update --init --recursive
export PICO_SDK_PATH=/path/to/pico-sdk
mkdir build && cd build

# Configure for specific board
cmake -DPICO_BOARD=pico_w ..    # or pico, pico2, pico2_w

# Build
make -j$(nproc)

# Flash via OpenOCD (with debug probe)
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "adapter speed 5000" \
  -c "program hid_proxy.elf verify reset exit"

# Alternative: Flash via bootloader
# Hold both shifts + HOME at runtime to enter bootloader mode
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
- `SPACE`: Print all key definitions to serial console (debug/diagnostic), and enable web access for 5 minutes (WiFi/HTTP configuration)
- `PRINT`: Write encryption key to NFC tag

## Special Key Combinations (Both Shifts Held)

These commands activate while holding both shift keys (not released):

- `HOME` (with both shifts held): Reboot to bootloader for flashing

## Macro Text Format

The text format for macros (used for HTTP/network configuration):
- `[public|private] trigger { actions... }` - whitespace flexible
- `[public]` - keydef works even when device is locked (no sensitive data)
- `[private]` - keydef requires device unlock (default, for passwords/secrets)
- `"text"` - quoted strings for typing (supports `\"` and `\\` escapes)
- `MNEMONIC` - special keys (ENTER, ESC, TAB, F1-F24, arrows, PAGEUP, etc.)
- `^C` - Ctrl+key shorthand (^A through ^Z)
- `[mod:key]` - explicit HID report in hex
- `MQTT("topic", "message")` - publish MQTT message (requires WiFi/MQTT configured)
- Triggers: single char (`a`), mnemonic (`F1`), or hex (`0x04`)

**Examples:**
```
[public] a { "Hello!" }
[private] F1 { "MyPassword123" ENTER }
[public] F2 { ^C "text" ^V }
[public] F5 { MQTT("hidproxy/light/bedroom", "ON") }
[public] F6 { "Lights off" ENTER MQTT("homeassistant/scene", "sleep") }
```

**Note:** Public keydefs can be executed when the device is locked (via double-shift + key), while private keydefs require the device to be unlocked first. MQTT actions require WiFi and MQTT broker configuration (see MQTT_SETUP.md).

## Important Code Locations

- Main state machine: `handle_keyboard_report()` in key_defs.c
- Key definition evaluation: `evaluate_keydef()` in key_defs.c (loads on-demand from kvstore)
- Interactive key definition: `start_define()` in key_defs.c (saves to kvstore when complete)
- Physical unlock trigger: key_defs.c (both-shifts+HOME handler)
- KVStore initialization: `kvstore_init()` in kvstore_init.c
- Password validation: `kvstore_set_encryption_key()` in kvstore_init.c (validates hash on unlock)
- Encryption operations: `encrypt_gcm()`, `decrypt_gcm()` in kvstore_init.c (AES-128-GCM via mbedtls)
- Storage wrappers: `kvstore_set_value()`, `kvstore_get_value()` in kvstore_init.c (handle headers + encryption)
- Keydef storage API: `keydef_save()`, `keydef_load()`, `keydef_delete()`, `keydef_list()` in keydef_store.c
- Macro parser: `parse_macros_to_kvstore()` in macros.c (parses text format, saves to kvstore)
- Macro serializer: `serialize_macros_from_kvstore()` in macros.c (loads from kvstore, outputs text)
- PBKDF2 key derivation: `enc_derive_key_from_password()` in encryption.c
- Lock function: `lock()` in hid_proxy.c (clears encryption keys from memory, publishes MQTT event)
- Unlock function: `unlock()` in hid_proxy.c (sets state to normal, publishes MQTT event)
- Legacy stubs: `store_encrypt()`/`store_decrypt()` in encryption.c (marked obsolete, kept for backward compatibility)
- NFC state machine: `nfc_task()` in nfc_tag.c:373
- USB host enumeration: `tuh_hid_mount_cb()` in usb_host.c:74
- WiFi initialization: `wifi_init()` in wifi_config.c
- WiFi task loop: `wifi_task()` in wifi_config.c (monitors connection, web timeout)
- HTTP server init: `http_server_init()` in http_server.c
- HTTP GET /macros.txt: `fs_open_custom()` in http_server.c
- HTTP POST /macros.txt: `http_post_*()` handlers in http_server.c
- HTTP POST /unlock: `httpd_post_finished()` in http_server.c (remote password unlock)
- HTTP GET /status: `status_cgi_handler()` in http_server.c
- MQTT client init: `mqtt_client_init()` in mqtt_client.c (connects to broker, sets up LWT)
- MQTT publish: `mqtt_publish_lock_state()` in mqtt_client.c (publishes lock/unlock events)

## Configuration Constants

- `KVSTORE_SIZE`: 128KB for kvstore flash storage (kvstore_init.h)
- `KVSTORE_OFFSET`: 0x1E0000 (last 128KB of 2MB flash) (kvstore_init.h)
- `IDLE_TIMEOUT_MILLIS`: 120 minutes before auto-lock (hid_proxy.h:20)
- Web access timeout: 5 minutes (wifi_config.c:web_access_enable)
- NFC key storage address: Block `0x3A` on Mifare tag (nfc_tag.c:18)
- I2C pins: SDA=4, SCL=5; PIO-USB pins: DP=2 (nfc_tag.c:12-13, usb_host.c:38)
- mDNS hostname: `hidproxy-XXXX.local` where XXXX = last 4 hex digits of board ID (wifi_config.c)
- Keydef key format: `keydef.0xHH` where HH is HID code (keydef_store.c)
- WiFi key format: `wifi.ssid`, `wifi.password`, `wifi.country`, `wifi.enabled` (wifi_config.c)
- Password hash key: `auth.password_hash` (32-byte SHA256 hash, unencrypted) (kvstore_init.h)
- Header bytes: `0x00` = unencrypted, `0x01` = encrypted (kvstore_init.h)
- Encryption overhead: 29 bytes per encrypted value (1 + 12 + 16) (kvstore_init.c)

## Known Issues/TODOs

From README.md and code comments:
1. ~~No buffer overflow protection on keystroke storage~~ ✅ Fixed: Keydef size limits enforced
2. ~~Basic encryption implementation (not production-ready)~~ ✅ Fixed: Now uses AES-128-GCM with authentication and password validation
3. ~~WiFi 10-second startup delay~~ ✅ Fixed: Removed
4. ~~kb.local_store memory allocation~~ ✅ Fixed: Removed, keydefs loaded on-demand from kvstore
5. ~~Password change not implemented~~ ✅ Fixed: kvstore_change_password() with full re-encryption
6. ~~Queue overflow handling needed~~ ✅ Fixed: Backpressure for macros, graceful degradation for passthrough
7. Poor user interface with no status feedback (no visual feedback for lock/unlock state)
8. HTTP POST /macros.txt not tested (code written but needs validation)
9. Legacy code can be removed: sane.c, old parse_macros()/serialize_macros(), store_t struct (kept for unit tests)

## WiFi/HTTP Configuration

**Status**: ✅ Implemented

The device now supports WiFi-based configuration via HTTP API. See WIFI_SETUP.md for complete guide.

**Key features:**
- Non-blocking WiFi connection (keyboard stays responsive)
- HTTP endpoints: GET/POST /macros.txt, GET /status
- Physical unlock required (both-shifts+SPACE) for 5-minute web access
- mDNS responder at `hidproxy-XXXX.local` (XXXX = last 4 hex digits of board ID)
- Text format upload/download for bulk editing
- No USB re-enumeration needed

**Usage:**
```bash
# 1. Press both-shifts+SPACE on keyboard to enable web access
# 2. Download macros (replace XXXX with your board ID)
curl http://hidproxy-XXXX.local/macros.txt > macros.txt
# 3. Edit and upload
curl -X POST http://hidproxy-XXXX.local/macros.txt --data-binary @macros.txt
# 4. Check status
curl http://hidproxy-XXXX.local/status
```

**Finding your board ID:** Check the serial console output when the device boots for the mDNS hostname, or use `avahi-browse -a` on Linux.

## Future Development

See CONFIGURATION_OPTIONS.md for additional planned features:
- ~~Serial console for WiFi setup~~ ✅ **Implemented**: Interactive WiFi config via UART (both-shifts+W)
- ~~MQTT publishing of keystroke events to Home Assistant~~ ✅ **Implemented**: Lock/unlock events now published (see MQTT_SETUP.md)
- ~~Password change support~~ ✅ **Implemented**: Full re-encryption with new password (both-shifts+INSERT)
- ~~Mouse forwarding~~ ✅ **Implemented**: Mouse reports forwarded in passthrough mode
- **Mouse macros**: Include mouse movements/clicks in key definitions (see MOUSE_SUPPORT.md for technical details)
- MQTT publishing of all keystroke events (optional, privacy concerns)
- MQTT configuration of macros (subscribe to config topic)
- Simple web UI for in-browser macro editing
- HTTP Basic Auth (optional secondary protection layer)
- MQTT auto-discovery for Home Assistant
