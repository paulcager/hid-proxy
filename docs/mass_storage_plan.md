# Plan: Macro Editing via USB Mass Storage

This document outlines the plan to implement a "macro editing mode" by presenting the Pico as a USB Mass Storage Device. This will allow a user to edit their key definitions in a simple text file.

**STATUS: ✅ IMPLEMENTED** (with format modifications - see Implementation Notes below)

## 1. Overview

The goal is to provide a user-friendly way to download, edit, and upload key definitions without needing a separate client or terminal.

The proposed user workflow is:

1.  The user triggers "edit mode" on the device (e.g., via a "double shift" command or by holding a button on boot).
2.  The Pico reboots as a USB Mass Storage Device (like a flash drive).
3.  The host computer mounts the drive, which contains a single file: `macros.txt`.
4.  The `macros.txt` file contains a text representation of all the currently stored key definitions.
5.  The user opens this file, makes changes, and saves it.
6.  The user ejects the USB drive.
7.  The Pico detects the eject, reads the `macros.txt` file, parses it, writes the new definitions to its internal flash, and reboots back into normal HID proxy mode.

## 2. Feasibility Analysis

This approach is **feasible**. The RP2040 has sufficient RAM (264KB) and processing power to handle the required tasks.

-   **Binary-to-Text (Serialization):** This is straightforward. The device will read its internal flash and write out the `macros.txt` file when it boots into mass storage mode.
-   **Text-to-Binary (Deserialization):** This is the most complex part. The device needs a parser to read the `macros.txt` file. This parser must be lightweight but robust against user error. A line-by-line parser reading into a 4KB RAM buffer (matching the flash size) is the recommended approach.

## 3. Proposed `macros.txt` Format

To balance user-friendliness with power, the format supports a flexible, line-based syntax.

-   **Comments:** Lines starting with `#` are ignored.
-   **Definitions:** A macro definition starts with `define <trigger>` and ends with `end`.
    -   The `<trigger>` can be a single character (e.g., `a`), a hex code (e.g., `0x04`), or a special key mnemonic (e.g., `F1`, `ENTER`, `PGDN`).
-   **Commands:** Inside a definition, you can mix `type` and `report` commands.
    -   **`type "string"`:** For human-friendly text entry. The parser will convert this into the necessary key-press/release events based on a standard **English (UK)** layout stored in the firmware.
    -   **`report mod=<m> keys=<k1>,...`:** For low-level control, allowing precise definition of a single HID report. This is necessary for special key combinations (e.g., `Ctrl+C`) or non-standard keyboard layouts.

### Example:

```
# Map the 'a' key to type the string "Hello!"
define a
  type "Hello!"
end

# ===============================================

# Map the F1 key to copy selected text and paste it.
# This demonstrates mixing 'type' and 'report' commands.
define F1
  type "Copying..."
  report mod=0x01 keys=0x06  # Press Ctrl+C
  report mod=0x00 keys=0x00  # Release all keys
  type "Pasting..."
  report mod=0x01 keys=0x19  # Press Ctrl+V
  report mod=0x00 keys=0x00  # Release all keys
end
```

## 4. Implementation Steps

1.  **Add USB Mass Storage (MSC) Class:**
    *   Integrate TinyUSB's MSC class.
    *   Develop the logic to switch the USB personality between HID and MSC. This will likely involve a reboot.
    *   Implement the MSC callbacks to serve the virtual `macros.txt` file from a RAM buffer.

2.  **Implement the Serializer (`keydefs` -> text):**
    *   Create a function `void serialize_keydefs_to_buffer(char* buffer, size_t size)`.
    *   This function will iterate through the `keydef_t` structures in flash.
    *   It will use `snprintf` to write the text representation into the buffer. It should serialize definitions using the human-readable mnemonics and characters where possible.

3.  **Create HID Lookup Tables:**
    *   Create a `static const` lookup table for **English (UK)** layout that maps printable ASCII characters to `struct { uint8_t mod; uint8_t key; }`.
    *   Create a second `static const` table that maps special key mnemonics (e.g., "ENTER", "F1", "PGDN") to their respective HID keycodes.

4.  **Implement the Parser (text -> `keydefs`):**
    *   Create a function `bool parse_macros_to_flash(char* buffer)`.
    *   This function will read the `macros.txt` buffer line by line.
    *   The `define` parser must handle characters, hex codes, and mnemonics by checking both lookup tables.
    *   For `type "string"`, it will use the ASCII lookup table to generate key-press and key-release reports.
    *   For `report`, it will parse the raw `mod` and `keys` values.
    *   It will reconstruct the `keydef_t` structures in a temporary RAM buffer (`uint8_t temp_flash_buffer[FLASH_STORE_SIZE]`).
    *   If parsing is successful, it will erase the flash sector and write the new data from the temporary buffer.

5.  **Integrate the Workflow:**
    *   **Trigger:** The user will enter edit mode by holding **both Shift keys and the `=` key** simultaneously.
    *   On boot, check for this trigger. If active, enter MSC mode.
    *   On MSC eject, trigger the parsing and flash write, then reboot into HID mode.

---

## Implementation Notes

The plan has been fully implemented with the following modifications:

### Format Changes

The original proposed format was verbose and required ~3.8x expansion over binary. A more compact format was implemented instead:

**Original Proposed Format:**
```
define a
  type "Hello!"
end
```

**Implemented Format:**
```
a { "Hello!" }
```

### New Syntax Features

- **Compact brace syntax**: `trigger { commands... }` with flexible whitespace
- **Ctrl+key shorthand**: `^C`, `^V` for common operations (maps to Ctrl+A through Ctrl+Z)
- **Explicit reports**: `[mod:key]` in hex for precise control
- **Multiple command types**: `"text"`, `MNEMONIC`, `^X`, `[mod:key]` can be mixed freely
- **Smart triggers**: Single char (`a`), mnemonic (`F1`), or hex (`0x04`)

**Examples:**
```
# Simple text
m { "meeting@example.com" }

# Mixed commands
F1 { "Help:" TAB "Available commands" ENTER }

# Ctrl shortcuts
F2 { ^C PAGEDOWN ^V }

# Raw HID
F3 { [01:06] [00:00] }
```

### Buffer Size

- Increased from 4KB (FLASH_STORE_SIZE) to 23KB (MSC_DISK_BUFFER_SIZE)
- Handles approximately 46 full keydefs with 10 reports each
- Text format expansion ratio: ~2-2.5x vs 3.8x for original format
- Panics on buffer overflow (experimental code)

### Implementation Status

✅ **Step 1 - MSC Class Integration**: Complete
- tusb_config.h: MSC enabled (CFG_TUD_MSC = 1)
- usb_descriptors.c/h: MSC interface added to USB descriptors
- msc_disk.c: All TinyUSB MSC callbacks implemented
- Boot mode flag in uninit data section persists across reboots

✅ **Step 2 - Serializer**: Complete (macros.c:321-418)
- Converts binary keydefs to human-readable text
- Intelligent text sequence detection
- Outputs mnemonics/^C notation where possible
- Falls back to [mod:key] for complex reports

✅ **Step 3 - HID Lookup Tables**: Complete (macros.c:17-216)
- UK keyboard layout (ascii_to_hid[])
- Comprehensive mnemonic table (F1-F24, arrows, etc.)
- Reverse lookup functions for serialization

✅ **Step 4 - Parser**: Complete (macros.c:262-380)
- Whitespace-flexible parsing
- Quoted string support with escape sequences
- Mnemonic keyword recognition
- Ctrl shorthand (^C style)
- Explicit report format [mod:key]
- Robust error handling

✅ **Step 5 - Workflow Integration**: Complete
- Trigger: Double-shift + Equal (key_defs.c:41-46)
- Boot detection: msc_boot_mode_flag check (hid_proxy.c:79-95)
- Eject handling: Parse and write to flash (msc_disk.c:92-105)
- Automatic reboot back to HID mode via watchdog

### Files Modified/Created

- `macros.c` - Parser and serializer (complete rewrite)
- `macros.h` - API and next_keydef() helper
- `msc_disk.c` - MSC callbacks (new file)
- `tusb_config.h` - MSC enabled
- `usb_descriptors.c/h` - MSC interface added
- `CMakeLists.txt` - Added msc_disk.c
- `hid_proxy.h` - MSC_BOOT_MAGIC constant
- `hid_proxy.c` - Boot mode detection
- `key_defs.c` - MSC boot trigger

### Testing Notes

- Serial console shows parse/serialize status
- Buffer overflow triggers panic (crashes device)
- Parse errors logged but don't prevent reboot
- Macros stored unencrypted in MSC mode; encrypted after eject
