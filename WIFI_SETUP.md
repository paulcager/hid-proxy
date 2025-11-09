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

**Note:** With the kvstore migration (November 2025), WiFi credentials are now stored in kvstore key-value pairs rather than a fixed flash sector. Manual flash programming is no longer recommended. Use Option 1 (`.env` file) or wait for Option 3 (serial console).

### Option 3: Wait for Serial Console (Future)

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

Press **both shifts + SPACE** on the physical keyboard. This enables web access for 5 minutes.

### 2. Check Status

**Note:** The mDNS hostname includes the last 4 digits of your board's unique ID. Check your serial console output for the exact hostname (e.g., `hidproxy-a1b2.local`).

```bash
curl http://hidproxy-XXXX.local/status
```
(Replace `XXXX` with your board ID)

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
curl http://hidproxy-XXXX.local/macros.txt
```

Response (updated format as of November 2025):
```
# Macros file - Format: [public|private] trigger { commands... }
# Commands: "text" MNEMONIC ^C [mod:key]
# [public] keydefs work when device is locked
# [private] keydefs require device unlock (default)

[private] F1 { "Hello World" ENTER }
[private] F2 { ^C "clipboard text" ^V }
[public] a { "expanded text" }
```

### 4. Update Macros

Edit your macros in a local file, then upload:

```bash
# Download current macros
curl http://hidproxy-XXXX.local/macros.txt > my_macros.txt

# Edit the file
vi my_macros.txt

# Upload changes
curl -X POST http://hidproxy-XXXX.local/macros.txt --data-binary @my_macros.txt
```

The device will:
1. Parse the text format (including `[public]`/`[private]` markers)
2. Convert to binary format
3. Store private macros encrypted in kvstore (AES-128-GCM)
4. Store public macros unencrypted in kvstore
5. Return success/error page

**Note:** With the kvstore migration (November 2025), macros are stored individually in key-value pairs rather than as a single encrypted block. This enables on-demand loading and selective encryption.

## Macro Text Format

The text format supports:

- **Privacy markers**: `[public]` or `[private]` prefix (new in November 2025)
  - `[public]` - Macro works even when device is locked (no sensitive data)
  - `[private]` - Macro requires device unlock (default, for passwords/secrets)
- **Quoted strings**: `"Hello World"` - Types text with proper shift/modifiers
- **Mnemonics**: `ENTER`, `ESC`, `TAB`, `F1-F24`, `LEFT_ARROW`, `PAGEUP`, etc.
- **Ctrl shortcuts**: `^C` (Ctrl+C), `^V` (Ctrl+V), `^A` through `^Z`
- **Raw HID reports**: `[mod:key]` - Hex values for modifier and keycode
- **Triggers**: Single char (`a`), mnemonic (`F1`), or hex (`0x04`)

### Examples

```
# Public macro - works when locked (no sensitive data)
[public] email { "user@example.com" }

# Private macro - requires unlock (contains password)
[private] login { "username" TAB "MyPassword123" ENTER }

# Public multi-line with special keys
[public] signature {
    "Best regards," ENTER
    "Your Name" ENTER
    "Company"
}

# Private copy-paste workflow
[private] F5 { ^C "Modified: " ^V ENTER }

# Public raw HID report (Ctrl+Alt+Del simulation)
[public] F12 { [05:4c] }  # Ctrl+Alt+Delete

# Private hex trigger
[private] 0x3a { "F1 key pressed with secret data" }

# If no privacy marker specified, defaults to [private]
F2 { "This is private by default" }
```

## Security

### Physical Unlock Required

HTTP endpoints are protected by physical unlock:
- Press **both shifts + SPACE** to enable web access
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
4. **Passphrase protection**: Private macros remain encrypted with AES-128-GCM
5. **Use `[public]` wisely**: Only mark macros as public if they contain no sensitive data
6. **Default to private**: When in doubt, leave the `[private]` marker or omit it (defaults to private)

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
mDNS responder started: hidproxy-a1b2.local
HTTP server started
```

**Note the hostname** shown in the mDNS responder line - this is what you'll use to access the device.

### Web Access Denied (403)

You forgot to enable web access:
1. Press both shifts + SPACE on the keyboard
2. Check status: `curl http://hidproxy-XXXX.local/status` (use your board ID)
3. Verify `"web_enabled": true`

### Device Locked (423)

The device is locked:
1. Unlock via passphrase (double-shift + ENTER, type password, ENTER)
2. Or unlock via NFC tag
3. Then retry web access

### mDNS Not Resolving

If `hidproxy-XXXX.local` doesn't resolve:
1. Check your router supports mDNS/Bonjour
2. Use IP address directly (check UART output for IP)
3. On Linux, install `avahi-daemon`
4. Make sure you're using the correct hostname - check serial console for the exact mDNS name with your board ID

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
- Web access enabled (both-shifts+SPACE)
- Device unlocked

**Response:** Plain text macro definitions

### POST /macros.txt

Updates macros from text format.

**Requirements:**
- Web access enabled (both-shifts+SPACE)
- Device unlocked

**Request body:** Plain text macro definitions

**Response:** HTML success or error page

## Example Workflow

```bash
# 1. Enable web access (press both-shifts+SPACE on keyboard)

# 2. Download current config (replace XXXX with your board ID)
curl http://hidproxy-XXXX.local/macros.txt > my_config.txt

# 3. Edit locally
cat >> my_config.txt << 'EOF'
[private] F10 { "New macro added via HTTP!" ENTER }
[public] F11 { "Public macro - works when locked" }
EOF

# 4. Upload changes
curl -X POST http://hidproxy-XXXX.local/macros.txt --data-binary @my_config.txt

# 5. Verify
curl http://hidproxy-XXXX.local/status

# 6. Test: Press F10 on your keyboard
```

**Tip:** Find your board ID by checking the serial console output when the device boots, or use `avahi-browse -a` on Linux to list all mDNS services.

## Tips

1. **Keep backups**: Download `macros.txt` regularly
