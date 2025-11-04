# hid-proxy

A USB HID keyboard proxy for Raspberry Pi Pico (or Pico W) that intercepts and processes keystrokes between a physical keyboard and host computer. Provides encrypted text expansion/macros with optional NFC tag authentication and WiFi-based configuration (Pico W only).

**⚠️ WARNING: Do NOT use in production**

This is a proof-of-concept and has many bodges, including:

1. **No buffer overflow protection** - keystroke storage can overflow
2. **Basic encryption implementation** - single SHA256 round for key derivation, no authenticated encryption
3. **Poor user interface** - no visual feedback of current state
4. **Code quality issues** - acknowledged by author as requiring rewrite
5. **Known bugs** - see BUGS.md for comprehensive list

## Features

**Core features (Pico and Pico W):**
- **Pass-through mode**: Keystrokes normally pass directly from physical keyboard to host
- **Text expansion**: Define single keystrokes that expand to sequences (macros)
- **Encrypted storage**: Key definitions stored encrypted in flash memory
- **Passphrase unlock**: Decrypt key definitions with password
- **NFC authentication**: Optionally store/read encryption keys from NFC tags
- **Auto-lock**: Automatically locks after 120 minutes of inactivity

**Pico W exclusive features:**
- **WiFi/HTTP configuration**: Edit macros via HTTP API without USB re-enumeration
- **Physical web unlock**: Both-shifts+HOME enables 5-minute web access window
- **mDNS support**: Access device at `hidproxy.local`

## Hardware Requirements

### Required Components
- **Raspberry Pi Pico** (RP2040) - basic functionality
  - Or **Raspberry Pi Pico W** (RP2040 with CYW43 WiFi) - for WiFi/HTTP features
- **USB cables**:
  - One for connecting physical keyboard to Pico (requires USB-A to micro-USB adapter or cable)
  - One for connecting Pico to host computer (micro-USB to USB-A/C)
- **Physical keyboard** (any USB HID keyboard)

### Optional Components
- **PN532 NFC Reader** (for NFC tag authentication)
- **Mifare Classic NFC tags** (for storing encryption keys)

### Wiring

**USB Connections:**
- Physical keyboard → Pico USB host (via PIO-USB on GPIO 2/3)
- Pico USB device → Host computer

**NFC Reader (optional):**
- PN532 SDA → GPIO 4
- PN532 SCL → GPIO 5
- PN532 VCC → 3.3V
- PN532 GND → GND

**Debug UART (optional):**
- UART0 TX → GPIO 0
- UART0 RX → GPIO 1

## Architecture

The application uses both cores of the RP2040:

- **Core 0**: Main logic, TinyUSB device stack (acts as keyboard to host), NFC operations, encryption/decryption
- **Core 1**: TinyUSB host stack via PIO-USB (receives input from physical keyboard)

Communication between cores uses lock-free queues for HID reports and LED status.

## Building

### Prerequisites

1. **Pico SDK** installed (update path in CMakeLists.txt if not at `/home/paul/pico/pico-sdk`)
2. **Git submodules** (TinyUSB, Pico-PIO-USB, tiny-AES-c, tinycrypt)
3. **CMake** and **gcc-arm-none-eabi** toolchain

### Build Steps

```bash
# Clone repository
git clone <repository-url>
cd hid-proxy

# Initialize submodules
git submodule update --init --recursive

# Build
mkdir build && cd build
cmake ..
make
```

This produces:
- `hid_proxy.elf` - ELF binary for debugging
- `hid_proxy.uf2` - UF2 file for bootloader flashing

### Flashing Methods

#### Method 1: Bootloader (easiest)
1. Hold both shift keys + PAUSE simultaneously while device is running
2. Device reboots into BOOTSEL mode
3. Pico appears as USB drive `RPI-RP2`
4. Copy `hid_proxy.uf2` to the drive

#### Method 2: Debug Probe (OpenOCD)
```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "adapter speed 5000" \
  -c "program hid_proxy.elf verify reset exit"
```

#### Method 3: From IDE
Run "ocd" debug configuration from CLion or your IDE.

## Usage

### Normal Operation

By default, all keystrokes pass through transparently from the physical keyboard to the host computer.

### Command Mode ("Double Shift")

To access special functions:

1. Press **both shift keys** simultaneously
2. Release all keys
3. Press one of the command keys below

### Command Reference

| Key      | Description                                                                                                                                                  |
|----------|--------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `ENTER`  | **When locked:** Start passphrase entry to unlock encrypted key definitions. Type your passphrase, then press `ENTER` again to submit.                      |
| `INSERT` | **When unlocked:** Change passphrase and re-encrypt key definitions. Type new passphrase, then press `ENTER` to save.                                       |
| `ESC`    | Cancel the current operation (e.g., exit passphrase entry or key definition mode).                                                                          |
| `DEL`    | **Erase everything** - immediately deletes encryption key and all key definitions from flash. No confirmation prompt. Cannot be undone!                     |
| `END`    | Lock device and clear decrypted key definitions from memory. Encrypted data in flash is preserved. Re-enter passphrase (double-shift + `ENTER`) to unlock.  |
| `=`      | Start defining/redefining a key. Next keystroke is the trigger key, following keystrokes are the expansion. End with another double-shift.                  |
| `PRINT`  | Write the current encryption key to an NFC tag (requires PN532 reader and Mifare Classic tag).                                                              |
| `PAUSE`  | *While holding both shifts:* Reboot into bootloader mode for flashing new firmware.                                                                         |

### First-Time Setup

A freshly flashed device starts in the **unlocked** state with no passphrase or key definitions.

To set up encryption:
1. **Double-shift** + `INSERT` to start setting a passphrase
2. Type your desired passphrase (letters, numbers, symbols - any keys on your keyboard)
3. Press `ENTER` to save
4. The passphrase is used to derive an encryption key that protects your key definitions in flash

**Important:**
- Passphrases support any keyboard characters (keycodes only, not multi-byte Unicode)
- If you enter the wrong passphrase when unlocking, the device stays locked with no visible feedback (check serial debug output for errors)
- There's no password recovery - if you forget it, use double-shift + `DEL` to erase and start over

### Editing Macros via HTTP API (Pico W Only)

If you have a Pico W, use the WiFi/HTTP interface for easier macro editing. See **WIFI_SETUP.md** for complete guide.

**Quick start:**
1. Configure WiFi (see WIFI_SETUP.md for initial setup)
2. Press **both shift keys + HOME** on your keyboard to enable web access (5 minutes)
3. Download macros: `curl http://hidproxy.local/macros.txt > macros.txt`
4. Edit the file in your favorite text editor
5. Upload changes: `curl -X POST http://hidproxy.local/macros.txt --data-binary @macros.txt`

**Advantages:**
- No USB re-enumeration (keyboard stays functional)
- Edit from any device on your network
- No need to unplug/replug
- Faster iteration

**Note:** If using regular Pico (non-W), use the interactive "double shift + =" mode to define macros one at a time.

**Macro File Format:**

```
# Comments start with #
# Format: trigger { commands... }

a { "Hello, world!" }           # 'a' types text

F1 { "Help text" ENTER }        # F1 types text + Enter

F2 { ^C "pasted" ^V }           # F2: Ctrl+C, type "pasted", Ctrl+V

0x04 { [01:06] [00:00] }        # Hex trigger with raw HID reports
```

**Syntax:**
- `trigger { commands... }` - Define a macro (whitespace flexible)
- `"text"` - Type text (use `\"` for quotes, `\\` for backslash)
- `MNEMONIC` - Special keys: `ENTER`, `ESC`, `TAB`, `F1`-`F24`, `PAGEUP`, `PAGEDOWN`, arrow keys, etc.
- `^C` - Ctrl+key shorthand (^A through ^Z for Ctrl+A through Ctrl+Z)
- `[mod:key]` - Raw HID report in hex (mod = modifier byte, key = keycode)
- Triggers can be: single character (`a`), mnemonic (`F1`), or hex (`0x04`)

**Examples:**

```
# Simple text expansion
m { "meeting@example.com" }

# Multi-line with special keys
F5 { "Date: " TAB "Time: " ENTER }

# Copy/paste with formatting
F6 { ^C PAGEDOWN ^V }

# Complex sequence with raw reports
F7 { "Starting..." [01:06] [00:00] "Done!" }
```

**To save and exit:**
1. Save the file in your text editor
2. Eject/unmount the USB drive from your operating system
3. The device automatically reboots back to normal HID mode
4. Your macros are now active!

**Important Notes:**
- The 23KB buffer holds approximately 46 full keydefs with 10 reports each
- If serialization fails (too many macros), the file shows an error message
- Macros are stored unencrypted in MSC mode - encryption happens after ejecting
- Any parse errors will be logged to serial console (connect to see errors)

### Example: Creating a Text Expansion (Traditional Method)

Let's define the letter `m` to expand to "meeting@example.com":

1. **Double-shift** (press both shifts, release all keys)
2. Press `=` (enters definition mode)
3. Press `m` (this is the trigger key)
4. Type `meeting@example.com` (this is the expansion)
5. **Double-shift** again to finish

Now whenever you press `m` (while unlocked), it will type out "meeting@example.com".

**Note:** Key definitions trigger on the keycode alone, ignoring modifier keys. So defining `m` will also trigger when you press Shift+M (instead of typing 'M').

### Example: Using NFC Authentication

NFC tags can store your encryption key for quick unlock without typing a passphrase.

**Prerequisites:** You must have already set up a passphrase (see First-Time Setup above).

1. **Store key to tag:**
   - Ensure device is unlocked (if locked, double-shift + `ENTER`, type passphrase, `ENTER`)
   - Place Mifare Classic tag on PN532 reader
   - Double-shift + `PRINT`
   - Encryption key is written to tag block 0x3A

2. **Authenticate with tag:**
   - When device is locked, place the NFC tag on the reader
   - Device automatically reads key and unlocks (if tag contains valid key)
   - No passphrase needed!

## Debug/Serial Output

Connect to serial console for debug output:

```bash
minicom -D /dev/ttyACM0 -b 115200
```

Or use any serial terminal at 115200 baud on:
- **Linux/Mac**: `/dev/ttyACM0` (or `/dev/ttyUSB0` for UART)
- **Windows**: Check Device Manager for COM port

## Troubleshooting

### Mac Keyboard Setup Assistant Appears

When connecting the device, macOS may show the Keyboard Setup Assistant. To reset:

```bash
sudo rm /Library/Preferences/com.apple.keyboardtype.plist
sudo reboot
```

After reboot, go through the keyboard setup assistant when prompted.

### Device Not Recognized

1. Check both USB cables are connected properly
2. Verify the physical keyboard works when connected directly to host
3. Check debug serial output for errors
4. Try reflashing the firmware

### Key Definitions Not Working

1. Ensure device is unlocked (double-shift + `ENTER`, enter passphrase)
2. Check that key definition was saved (double-shift at end of definition)
3. Verify not in locked state (auto-locks after 120 minutes)

### Wrong Passphrase / Can't Unlock

1. Device provides **no visual feedback** for wrong passphrase - it just stays locked
2. Connect to serial console (see Debug/Serial Output section) to check for "Could not decrypt" errors
3. If you've forgotten the passphrase:
   - Double-shift + `DEL` to erase everything and start over
   - Warning: This permanently deletes all key definitions

### NFC Not Working

1. Verify wiring: SDA → GPIO 4, SCL → GPIO 5, 3.3V power
2. Check PN532 is in I2C mode (DIP switches)
3. Use Mifare Classic tags (not Ultralight or DESFire)
4. Ensure tag is positioned correctly on reader

### Queue Full Panic

If you see "Queue is full" errors:
- You may be sending key definitions too fast
- Try shorter expansions or slower typing
- Known issue - see BUGS.md #13

## Configuration

Key constants in `hid_proxy.h`:

| Constant                | Value      | Description                           |
|-------------------------|------------|---------------------------------------|
| `FLASH_STORE_SIZE`      | 64 KB      | Maximum size for key definitions      |
| `IDLE_TIMEOUT_MILLIS`   | 120 min    | Auto-lock timeout                     |

### Modifying Flash Storage Size

To change the amount of flash memory reserved for storing key definitions, you only need to edit one file:

1.  **`memmap_custom.ld`**: This linker script defines the actual memory region in flash.

**Example: Changing storage to 128KB**

1.  **Edit `memmap_custom.ld`**:
    *   **Calculate the new start address**: The total flash size is 2MB (0x200000). The storage is placed at the end of the flash. So, for 128KB (0x20000), the new start address will be `0x10200000 - 0x20000 = 0x101E0000`.
    *   **Update the size**: The size is `128 * 1024 = 131072`.
    *   Modify the `.flash_storage` section:
        ```ld
        SECTIONS
        {
            .flash_storage 0x101E0000 (NOLOAD) :
            {
                . = ALIGN(4);
                __flash_storage_start = .;
                . = . + 131072;
                __flash_storage_end = .;
            }
        }
        ```

3.  **Rebuild the project**.

## Known Issues

See **BUGS.md** for a comprehensive list of 34+ bugs including:
- Buffer overflows (#1, #2, #7, #14, #15)
- Race conditions (#3, #12)
- Memory safety issues (#5, #8-11)
- Weak encryption (#31, #32, #34)

**This is proof-of-concept code only. Do not use with sensitive data.**

## Technical Documentation

For detailed technical information, see:
- **CLAUDE.md** - Architecture, code locations, building instructions
- **BUGS.md** - Comprehensive bug analysis

## License

[Add license information here]

## Contributing

This is a proof-of-concept project. Contributions welcome but please note the author has indicated the code would be rewritten before serious use.
