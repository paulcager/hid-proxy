# Configuration Options for HID Proxy

## Requirements
- Must work offline (no WiFi dependency for core keyboard functionality)
- Pico W with optional WiFi for configuration
- One-way integration: keystrokes → MQTT commands to Home Assistant
- Tech-savvy users (text format acceptable)

## Storage Format Decision

### Recommendation: Keep Binary in Flash (Extended Format)

**Rationale:**
- Fast decrypt/unlock (no parsing overhead)
- Compact storage (RAM is limited)
- Robust (less prone to corruption)
- WiFi config converts binary ↔ text on demand
- **Future-proof**: Can support actions beyond keystrokes (MQTT messages, delays, etc.)

**Flash Layout:**
```
[magic][iv][encrypted_magic][binary_actions...]
```

**Current action type:**
```c
typedef struct keydef {
    int keycode;         // trigger
    int used;            // number of reports
    hid_keyboard_report_t reports[0];  // variable-length array
} keydef_t;
```

**Future action types** (to be added):
```c
typedef enum {
    ACTION_HID_REPORTS = 0,  // Current: send HID reports
    ACTION_MQTT_PUBLISH = 1, // Future: publish MQTT message
    ACTION_DELAY = 2,        // Future: delay N milliseconds
    ACTION_CONDITIONAL = 3,  // Future: if/then logic
} action_type_t;

typedef struct action {
    int keycode;         // trigger
    action_type_t type;  // what kind of action
    int data_len;        // length of action-specific data
    uint8_t data[0];     // variable-length action data
} action_t;
```

**Text format will extend** to support new actions:
```
# Current (keystrokes only):
F1 { "Hello World" ENTER }

# Future (MQTT + keystrokes):
F2 { mqtt "homeassistant/lights/living_room" "ON" }
F3 { "Typing..." mqtt "homeassistant/notify/phone" "User pressed F3" }
F4 { delay 1000 "One second later" }
```

**Binary format remains compact and fast to evaluate at runtime.**

---

## Configuration Method Options

### Option 1: HTTP API + Text Format (Recommended)

**Online mode:**
```bash
# First: Press both shifts + SPACE on physical keyboard to enable web access

# List macros (returns text format)
curl http://hidproxy.local/macros.txt

# Update all macros
curl -X POST http://hidproxy.local/macros.txt \
  --data-binary @macros.txt

# Get status (shows if web access enabled)
curl http://hidproxy.local/status
# Returns: {"locked":false,"web_enabled":true,"expires_in":287,"macros":5,"uptime":3600}
```

**Offline mode:**
- Traditional "double shift" commands still work
- NFC tag authentication works
- Previous configuration persists in flash

**Pros:**
- Simple HTTP server (~200 lines with lwIP)
- Standard tools (curl, wget, browser)
- Text format editable in any editor
- Can serve static HTML page for browser editing

**Cons:**
- Need mDNS for `.local` hostname (or use IP)
- HTTP is unencrypted (fine for local network)

**Implementation:**
```c
// Endpoints (all require physical unlock via both-shifts+SPACE):
GET  /macros.txt          // serialize_macros() → text (if unlocked)
POST /macros.txt          // parse_macros() → binary → flash (if unlocked)
GET  /status              // JSON: {locked, web_enabled, num_macros, uptime}
POST /lock                // Lock the device (always allowed)
GET  /                    // Simple HTML editor page

// Physical unlock required before any GET/POST to /macros.txt
// Press both shifts + SPACE to enable web access for 5 minutes
```

---

### Option 2: Serial/CDC Console

**Always available (USB or WiFi):**
```
> list
F1 { "Hello World" }
F2 { ^C "copied text" ^V }

> define F3 { "New macro" ENTER }
OK

> delete F1
OK

> save
Encrypted and saved to flash
```

**Pros:**
- Works via USB (no WiFi needed)
- Can also expose via telnet (port 23) when WiFi available
- Minimal code (~150 lines)
- Interactive and scriptable

**Cons:**
- Requires terminal program (minicom, screen, PuTTY)
- Less user-friendly than web interface
- Need to parse commands (but simpler than full text format)

**Implementation:**
- Reuse CDC interface (already in tusb_config.h)
- Add telnet server when WiFi connected
- Simple command parser with readline-style editing

---

### Option 3: Hybrid - HTTP for Bulk + Serial for Debug

**Combines best of both:**
- HTTP `/macros.txt` for editing full config
- Serial console for status/debug/quick changes
- Both work offline (serial) or online (HTTP + telnet)

**Example workflow:**
```bash
# Via HTTP (WiFi) - requires physical unlock first
# 1. Press both shifts + SPACE on keyboard (enables web access for 5 min)
# 2. Then use HTTP:
curl http://hidproxy.local/macros.txt > my_macros.txt
vi my_macros.txt
curl -X POST http://hidproxy.local/macros.txt --data-binary @my_macros.txt

# Via Serial (USB)
screen /dev/ttyACM0 115200
> status
> define F5 { "Quick macro" }
> web unlock          # Alternative to physical both-shifts+SPACE
```

**Pros:**
- Best of both worlds
- Serial always works (debug/recovery)
- HTTP for convenient bulk editing

**Cons:**
- More code to maintain (~350 lines total)

---

### Option 4: MQTT Configuration

**Online mode:**
```bash
# Publish full config
mosquitto_pub -t "hidproxy/config/set" -f macros.txt

# Subscribe to status
mosquitto_sub -t "hidproxy/status"

# Trigger actions
mosquitto_pub -t "hidproxy/lock/set" -m "true"
```

**Offline mode:**
- Traditional double-shift commands work
- No dependency on MQTT broker

**Pros:**
- Native Home Assistant integration
- Pub/sub model (multiple listeners)
- QoS ensures delivery
- Can log all keystrokes to MQTT topic

**Cons:**
- Requires MQTT broker (mosquitto)
- More complex than HTTP
- Harder to debug (need MQTT client)

**Implementation:**
```c
// Topics:
hidproxy/config/set          // Publish macros.txt here
hidproxy/config/current      // Device publishes current config
hidproxy/status              // {state, macros, uptime, wifi}
hidproxy/keystroke           // Publish intercepted keystrokes
hidproxy/lock/set            // true/false
hidproxy/trigger/{keycode}   // Manual trigger from HA
```

---

## Network Stack Considerations

### WiFi Connection Strategy

**Non-blocking WiFi:**
```c
// At boot:
1. Initialize hardware (USB, HID, queues)
2. Start WiFi connection in background (if configured)
3. Begin keyboard operation immediately
4. When WiFi connects: start HTTP/MQTT servers

// If WiFi fails:
- Retry every 60 seconds
- Log to serial/LED blink pattern
- Keyboard continues working normally
```

**Configuration storage:**
```c
typedef struct {
    // Existing encrypted storage
    char magic[8];
    uint8_t iv[16];
    char encrypted_magic[8];
    keydef_t keydefs[0];
} store_t;

// Add separate WiFi config (separate flash sector)
typedef struct {
    char ssid[32];
    char password[64];
    char mqtt_broker[64];  // or leave empty for HTTP-only
    uint16_t mqtt_port;
    bool enable_wifi;
} wifi_config_t;
```

**Flash layout:**
```
0x00000-0x7FFFF:  Program code
0x80000:          Encrypted keydefs (4KB)
0x81000:          WiFi config (unencrypted, 4KB)
```

---

## Recommended Implementation Plan

### Phase 1: Core Network (1-2 days)
1. Add WiFi connection logic (non-blocking)
2. Simple HTTP server with lwIP
3. Endpoints: `/macros.txt` (GET/POST), `/status` (GET)
4. mDNS responder for `hidproxy.local`

### Phase 2: Enhanced Config (1 day)
5. Serial console (CDC) for offline/debug
6. WiFi configuration via serial: `wifi ssid password`
7. LED status indicators (WiFi connected, etc.)

### Phase 3: MQTT Integration (2-3 days)
8. MQTT client library (paho-mqtt or similar)
9. Publish keystroke events to `hidproxy/keystroke`
10. Subscribe to `hidproxy/config/set`
11. Home Assistant discovery messages

### Phase 4: Cleanup (1 day)
12. **Remove MSC mode entirely**:
    - Delete `msc_disk.c` (~180 lines)
    - Remove MSC callbacks and FAT filesystem simulation
    - Keep `serialize_macros()` and `parse_macros()` in `macros.c` for HTTP API
    - Remove `MSC_BOOT_MAGIC` and related logic from `hid_proxy.c`
    - Remove double-shift + Equal trigger for MSC mode
13. Update README with network configuration
14. Update CLAUDE.md to reflect MSC removal

---

## Code Size Impact

**Current codebase:**
- MSC mode: ~700 lines (macros.c + msc_disk.c)

**Estimated additions:**
- HTTP server (lwIP): ~200 lines
- WiFi management: ~150 lines
- Serial console: ~150 lines
- MQTT client: ~300 lines

**Net change:**
- Option 1 (HTTP only): -350 lines
- Option 2 (Serial only): -550 lines
- Option 3 (HTTP + Serial): -200 lines
- Option 4 (MQTT + HTTP): +100 lines

---

## Security Considerations

### Network Exposure
- **HTTP is unencrypted** (acceptable for trusted LAN)
- **Decrypted macros must be protected** - use one of:
  1. **Physical unlock required** (Recommended): HTTP endpoints only work after **both shifts + SPACE** keystroke on physical keyboard. Timeout after 5 minutes of inactivity.
  2. **HTTP Basic Auth**: Simple `Authorization: Basic <base64(user:pass)>` header check
  3. **Both**: Require physical unlock AND password on first access per session

### Recommended: Physical Unlock for Web Access

**New "triple key" command:**
- Press **both shifts + SPACE** (all at same time): Enable web access for 5 minutes
- Similar to existing both shifts + SPACE (bootloader mode)
- LED indicator shows web access enabled
- Any keystroke resets 5-minute timer
- Automatic lock after timeout or explicit lock command

**Implementation:**
```c
typedef struct {
    bool web_access_enabled;
    absolute_time_t web_access_expires;
} web_state_t;

// In keyboard report handler (similar to shift+shift+HOME detection):
if (both_shifts_pressed(report) && report->keycode[0] == HID_KEY_SPACE) {
    web_state.web_access_enabled = true;
    web_state.web_access_expires = make_timeout_time_ms(5 * 60 * 1000);
    // Don't forward this combo to host
    return;
}

// In HTTP request handler:
if (!web_state.web_access_enabled ||
    absolute_time_diff_us(get_absolute_time(), web_state.web_access_expires) > 0) {
    return HTTP_403_FORBIDDEN;
}
```

**Why this approach:**
- Physical presence required (can't attack remotely)
- No password to remember/configure
- Visual confirmation (LED)
- Auto-locks if you forget
- Unlikely key combo (won't conflict with normal usage)
- Can still use Basic Auth as secondary layer

### Passphrase Protection
- **Never allow passphrase change via network** (only serial or NFC)
- Require device physical access for critical operations
- Rate limit unlock attempts (network and physical)
- **Never expose encryption key via network** (only decrypted macro contents)

---

## Example macros.txt Format (No Changes)

```
# Macros file - Format: trigger { commands... }
# Commands: "text" MNEMONIC ^C [mod:key]

F1 { "Hello World" ENTER }
F2 { ^C "clipboard text" ^V }
F3 { "http://homeassistant.local:8123" ENTER }
a { "expanded text for 'a'" }
0x3a { [01:04] [00:05] }  # Ctrl+A, B
```

---

## Fallback Strategy

If WiFi is unavailable or fails:
1. Device boots normally as keyboard
2. LED blinks pattern indicating "no WiFi"
3. Serial console remains available via USB
4. Traditional "double shift" commands work
5. NFC authentication works
6. All core functionality intact

**User can:**
- Configure via serial: `> define F1 { "text" }`
- Set WiFi: `> wifi MySSID MyPassword`
- Check status: `> status`
- Force WiFi retry: `> wifi reconnect`

---

## Recommendation Summary

**For your use case (techie users, HA integration, offline-capable):**

→ **Option 3: Hybrid HTTP + Serial**

**Rationale:**
1. HTTP `/macros.txt` is simple and standard (curl, wget, browser)
2. Serial console provides offline config and debug
3. Text format is fine for tech users
4. Can add MQTT later for HA keystroke events
5. No dependency on external services (MQTT broker)
6. Minimal code compared to MSC mode

**Implementation priority:**
1. HTTP API (`/macros.txt`, `/status`)
2. Serial console (reuse existing UART/CDC)
3. WiFi config storage
4. MQTT publish for keystroke events (optional)

This keeps the core keyboard functionality simple and reliable while adding powerful network configuration when available.
