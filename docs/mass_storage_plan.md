# Plan: Macro Editing via USB Mass Storage

This document outlines the plan to implement a "macro editing mode" by presenting the Pico as a USB Mass Storage Device. This will allow a user to edit their key definitions in a simple text file.

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
    *   **Trigger:** The user will enter edit mode by holding **both Shift keys and the `=` key** simultaneously during power-on or reset.
    *   On boot, check for this trigger. If active, enter MSC mode.
    *   On MSC eject, trigger the parsing and flash write, then reboot into HID mode.
