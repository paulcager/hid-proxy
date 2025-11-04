# WiFi Configuration Guide

## Overview

The HID Proxy now supports WiFi-based configuration via HTTP API, eliminating the need for USB re-enumeration. Configuration is done through a simple HTTP interface accessible via curl or a web browser.

## Hardware Requirements

- **Raspberry Pi Pico W** (with CYW43 WiFi chip)
- Physical keyboard connected via PIO-USB
- NFC reader (optional, for key storage)

## Initial WiFi Setup

Since WiFi configuration is stored in flash, you need to set it up once. You have three options:

### Option 1: Build-Time Configuration via .env file (Recommended)

The easiest way to configure WiFi is to create a `.env` file in the root of the project directory. If this file exists when you build the firmware, the credentials will be automatically baked into the image.

1.  **Create a `.env` file** in the project's root directory.
2.  **Add your credentials** to the file like this:

    ```
    WIFI_SSID="Your-SSID"
    WIFI_PASSWORD="Your-Password"
    ```

3.  **Build and flash** the firmware as usual.

On the first boot, the device will automatically save these credentials to its flash memory and reboot. Subsequent boots will use the saved credentials.

### Option 2: Manual Flash Programming (Advanced)

If you cannot use the `.env` method, you can manually write the WiFi configuration directly to the device's flash memory.

1. Build and flash the firmware normally
2. Use a flash programming tool to write WiFi config at offset `FLASH_STORE_OFFSET + 4096`
3. Format:
   ```c
   struct {
       char magic[8];      // "hidwifi1"
       char ssid[32];      // Your WiFi SSID
       char password[64];   // Your WiFi password
       bool enable_wifi;    // 1 = enabled
       uint8_t reserved[...];
   }
   ```

### Option 2: Wait for Serial Console (Recommended)

The serial console implementation (coming soon) will allow you to configure WiFi via UART:

```
> wifi set MySSID MyPassword
WiFi configured: MySSID
> wifi enable
WiFi enabled, connecting...
```

## Using the HTTP API

Once WiFi is configured and connected:

### 1. Enable Web Access

Press **both shifts + HOME** on the physical keyboard. This enables web access for 5 minutes.

### 2. Check Status

```bash
curl http://hidproxy.local/status
```

Response:
```json
{
  "locked": false,
  "web_enabled": true,
  "expires_in": 287000,
  "macros": 5,
  "uptime": 3600000,
  "wifi": true
}
```

### 3. Get Current Macros

```bash
curl http://hidproxy.local/macros.txt
```

Response:
```
# Macros file - Format: trigger { commands... }
# Commands: "text" MNEMONIC ^C [mod:key]

F1 { "Hello World" ENTER }
F2 { ^C "clipboard text" ^V }
a { "expanded text" }
```

### 4. Update Macros

Edit your macros in a local file, then upload:

```bash
# Download current macros
curl http://hidproxy.local/macros.txt > my_macros.txt

# Edit the file
vi my_macros.txt

# Upload changes
curl -X POST http://hidproxy.local/macros.txt --data-binary @my_macros.txt
```

The device will:
1. Parse the text format
2. Convert to binary format
3. Encrypt with your passphrase
4. Write to flash
5. Return success/error page

## Macro Text Format

The text format supports:

- **Quoted strings**: `"Hello World"` - Types text with proper shift/modifiers
- **Mnemonics**: `ENTER`, `ESC`, `TAB`, `F1-F24`, `LEFT_ARROW`, `PAGEUP`, etc.
- **Ctrl shortcuts**: `^C` (Ctrl+C), `^V` (Ctrl+V), `^A` through `^Z`
- **Raw HID reports**: `[mod:key]` - Hex values for modifier and keycode
- **Triggers**: Single char (`a`), mnemonic (`F1`), or hex (`0x04`)

### Examples

```
# Simple text expansion
email { "user@example.com" }

# Multi-line with special keys
signature {
    "Best regards," ENTER
    "Your Name" ENTER
    "Company"
}

# Copy-paste workflow
F5 { ^C "Modified: " ^V ENTER }

# Raw HID report (Ctrl+Alt+Del simulation)
F12 { [05:4c] }  # Ctrl+Alt+Delete

# Hex trigger
0x3a { "F1 key pressed" }
```

## Security

### Physical Unlock Required

HTTP endpoints are protected by physical unlock:
- Press **both shifts + HOME** to enable web access
- Access expires after 5 minutes
- Any keystroke resets the timer
- Automatically locks on timeout

### Access Control

- `/macros.txt` GET/POST: Requires web access + unlocked device
- `/status` GET: Always available (shows if web access enabled)
- Device must be unlocked (passphrase or NFC) before accessing macros

### Best Practices

1. **Local network only**: HTTP is unencrypted, use on trusted LAN
2. **Physical presence**: Web unlock requires physical keyboard access
3. **Short timeout**: 5-minute window limits exposure
4. **Passphrase protection**: Flash data remains encrypted

## Troubleshooting

### WiFi Not Connecting

Check UART output for errors:
```bash
minicom -D /dev/ttyACM0 -b 115200
```

Look for:
```
WiFi initialized, connecting to 'YourSSID'...
WiFi connected! IP: 192.168.1.100
mDNS responder started: hidproxy.local
HTTP server started
```

### Web Access Denied (403)

You forgot to enable web access:
1. Press both shifts + HOME on the keyboard
2. Check status: `curl http://hidproxy.local/status`
3. Verify `"web_enabled": true`

### Device Locked (423)

The device is locked:
1. Unlock via passphrase (double-shift + ENTER, type password, ENTER)
2. Or unlock via NFC tag
3. Then retry web access

### mDNS Not Resolving

If `hidproxy.local` doesn't resolve:
1. Check your router supports mDNS/Bonjour
2. Use IP address directly (check UART output for IP)
3. On Linux, install `avahi-daemon`

### Macro Parse Errors

Check syntax:
- All quotes must be escaped: `"test \"quoted\" text"`
- Valid mnemonics only (see list in CLAUDE.md)
- Balanced braces: `trigger { ... }`
- One definition per line

## Network Performance

- **Non-blocking**: WiFi runs in background, keyboard stays responsive
- **No re-enumeration**: USB stays connected during config
- **Automatic reconnect**: WiFi retries on disconnection
- **Minimal overhead**: lwIP runs in background thread

## Future Enhancements

Planned features (see CONFIGURATION_OPTIONS.md):
- Serial console for WiFi setup
- MQTT publishing of keystroke events
- Home Assistant integration
- Simple web UI for macro editing
- HTTP Basic Auth (optional secondary protection)

## API Reference

### GET /status

Returns device status in JSON.

**Response:**
```json
{
  "locked": bool,           // Device encryption status
  "web_enabled": bool,      // Web access currently enabled
  "expires_in": int,        // Milliseconds until web access expires
  "macros": int,            // Number of macros defined
  "uptime": int,            // Milliseconds since boot
  "wifi": bool              // WiFi connection status
}
```

### GET /macros.txt

Returns current macros in text format.

**Requirements:**
- Web access enabled (both-shifts+HOME)
- Device unlocked

**Response:** Plain text macro definitions

### POST /macros.txt

Updates macros from text format.

**Requirements:**
- Web access enabled (both-shifts+HOME)
- Device unlocked

**Request body:** Plain text macro definitions

**Response:** HTML success or error page

## Example Workflow

```bash
# 1. Enable web access (press both-shifts+HOME on keyboard)

# 2. Download current config
curl http://hidproxy.local/macros.txt > my_config.txt

# 3. Edit locally
cat >> my_config.txt << 'EOF'
F10 { "New macro added via HTTP!" ENTER }
EOF

# 4. Upload changes
curl -X POST http://hidproxy.local/macros.txt --data-binary @my_config.txt

# 5. Verify
curl http://hidproxy.local/status

# 6. Test: Press F10 on your keyboard
```

## Tips

1. **Keep backups**: Download `macros.txt` regularly
2. **Test first**: Verify syntax with small changes before big updates
3. **Use git**: Version control your macro configurations
4. **Script it**: Automate common macro updates with shell scripts
5. **Monitor logs**: Watch UART for encryption/parse errors
