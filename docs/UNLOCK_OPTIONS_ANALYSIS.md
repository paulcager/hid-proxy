# Unlock Options: Comprehensive Analysis

**Date:** 2025-12-14
**Status:** Design analysis (explored but not implemented)

## Summary

This document analyzes various methods for unlocking the device (unsealing encrypted key definitions) and explains the fundamental trade-offs that make certain combinations impossible.

## The Impossible Triangle

There is a **fundamental trade-off** between three desirable properties:

```
        No Phone Interaction
               /\
              /  \
             /    \
            /      \
           /        \
          /          \
         /            \
        /______________\
   Small Hardware    Secure
```

**You can only pick two:**

| Combination | Example | Trade-off |
|-------------|---------|-----------|
| Secure + No Interaction | NFC tag authentication | ❌ Requires bulky NFC reader |
| Small + Secure | HTTP widget, BLE + notification | ❌ Requires tapping phone screen |
| Small + No Interaction | BLE auto-unlock on proximity | ❌ Insecure (see below) |

## The Core Security Problem

**Any system with these properties is vulnerable:**

1. **Physical trigger is on the Pico** (button, hall sensor, capacitive touch, etc.)
2. **Phone auto-responds** without user confirmation (background app)

**Attack scenario:**
1. Attacker triggers Pico (press button, place magnet, etc.)
2. Victim's phone (in pocket, nearby) detects BLE signal
3. Phone auto-sends encryption key
4. Device unseals for attacker

**Why this is unfixable:**
- Physical trigger and authentication are **decoupled**
- Different people can perform each action
- BLE range (~1-10m) is too large to guarantee "person at desk = person with phone"
- RSSI is too unreliable to distinguish "at desk" vs "in room"

## Secure Unlock Methods

### 1. NFC Tag Authentication (Current Implementation)

**Hardware:** PN532 NFC reader module (40mm x 40mm)

**How it works:**
- Tap Mifare Classic tag to reader
- Tag contains 16-byte encryption key
- Pico reads key, validates, unseals

**Pros:**
- ✅ Secure (tag UID + key data, very short range ~4cm)
- ✅ No phone interaction needed
- ✅ Works when device sealed
- ✅ Multiple tags supported (one per user)

**Cons:**
- ❌ Requires external NFC reader (bulky)
- ❌ Need to carry physical tag
- ❌ Android cannot emulate Mifare Classic (HCE limitation)

**Status:** ✅ Implemented (requires `--nfc` build flag)

**See also:** `docs/NFC_NDEF_SUPPORT.md` for Android-compatible NDEF tags

### 2. HTTP Widget/Shortcut (Most Practical)

**Hardware:** None (uses built-in WiFi on Pico W)

**How it works:**
- Tap Android widget/shortcut
- Sends POST to `http://hidproxy-XXXX.local/unseal` with password
- Pico validates password, unseals

**Pros:**
- ✅ Fully secure (explicit action + password validation)
- ✅ No extra hardware
- ✅ Already implemented
- ✅ Can add geofencing (only works at home/office)
- ✅ Works from anywhere on WiFi network

**Cons:**
- ❌ Must unlock phone and tap widget
- ❌ Requires WiFi connectivity
- ❌ Must type/store password on phone

**Status:** ✅ Already implemented (POST /unseal endpoint exists)

**Implementation:**
- Use "HTTP Shortcuts" Android app
- Create POST request with password in body
- Add to home screen as widget

### 3. Manual Password Entry (Built-in)

**Hardware:** None

**How it works:**
- Press both shifts + ENTER on physical keyboard
- Type password on keyboard
- Press ENTER to submit
- Pico validates via PBKDF2 + SHA256 hash

**Pros:**
- ✅ Fully secure
- ✅ No extra hardware or phone needed
- ✅ Already implemented

**Cons:**
- ❌ Must type password every time (annoying for long passwords)
- ❌ Password visible via USB keylogger/screen capture
- ❌ Slower than tap-to-unlock

**Status:** ✅ Implemented (default method)

### 4. BLE with Manual Confirmation (Documented, Not Implemented)

**Hardware:** None (uses built-in BLE on Pico W)

**How it works:**
- Pico W advertises BLE service
- Android app shows notification when in range
- User taps notification to confirm
- App sends key via BLE GATT characteristic

**Pros:**
- ✅ Secure (explicit confirmation)
- ✅ No extra hardware
- ✅ Proximity check (must be nearby)

**Cons:**
- ❌ Requires custom Android app (or Tasker)
- ❌ Must tap notification on phone
- ❌ Phone app must run in background (battery drain)
- ❌ Not yet implemented

**Status:** 📄 Design documented in `docs/BLE_UNLOCK.md`

## Insecure Unlock Methods (Why They Don't Work)

### ❌ BLE Auto-Unlock on Proximity

**Concept:** Phone auto-sends key when BLE detected

**Why it fails:**
- Attacker at desk presses button/trigger → your phone (nearby) responds
- RSSI cannot reliably distinguish "at desk" vs "in room"
- Phone orientation/pocket placement varies RSSI by ±10 dBm

**Attack scenario:**
1. You stand 2m from desk (RSSI -55 dBm)
2. Attacker sits at desk, triggers unlock
3. Your phone thinks you sat down, auto-unlocks
4. Attacker has access

**Mitigations that don't fully work:**
- BLE bonding (only YOUR phone) → doesn't prevent attack if you're nearby
- Strict RSSI threshold → unreliable, false negatives when phone in pocket
- Short unlock window → attacker just waits for you to be nearby

**Verdict:** Fundamentally insecure for desk-level physical access

### ❌ Hall Sensor + BLE Auto-Unlock

**Concept:** Detect phone's magnet with hall sensor → trigger BLE unlock

**Why it fails:**
- Same problem as above
- Anyone's phone/magnet triggers sensor
- Your phone responds if in range

**Verdict:** Same security flaw

### ❌ Capacitive Touch + BLE Auto-Unlock

**Concept:** Touch sensor detects hand/phone → trigger BLE unlock

**Why it fails:**
- Same decoupling problem
- False positives (coffee mug, leaning on desk)

**Verdict:** Unreliable + same security flaw

### ❌ BLE Beacon Broadcasting

**Concept:** Phone broadcasts BLE beacon with key

**Why it fails:**
- Beacons are unencrypted broadcasts
- Anyone with BLE scanner can sniff key
- No authentication

**Verdict:** Catastrophically insecure

## Threat Model Considerations

**Choose unlock method based on what you're protecting against:**

### Threat: Remote unlock (across room/building)
**Solutions that work:**
- BLE with bonding + RSSI threshold (prevents non-paired devices, limits range)
- HTTP with geofencing (only works at specific location)
- NFC (very short range)

### Threat: Desk-level physical access
**Solutions that work:**
- Manual password entry (keyboard)
- HTTP widget with password
- NFC tag (must possess physical tag)
- BLE with explicit confirmation (notification tap)

**Solutions that DON'T work:**
- Any auto-unlock with physical trigger on Pico

### Threat: Shoulder surfing / keylogger
**Solutions that work:**
- NFC tag (no password visible)
- BLE with key stored on phone (no typing)

**Solutions that DON'T work:**
- Manual password entry
- HTTP widget (password stored in app)

## Recommendations by Use Case

### Home Office (Low Physical Security Concerns)
**Recommended:** HTTP widget + geofencing
- Most convenient
- Already implemented
- No extra hardware
- Good enough if you live alone

### Shared Office (Moderate Security)
**Recommended:** NFC tag authentication
- Physical token possession required
- Very short range
- No password typing (shoulder-surf resistant)
- Worth the NFC reader bulk

### High Security Environment
**Recommended:** Manual password entry + auto-seal timeout
- No persistent keys on phone/tags
- Fresh authentication each time
- Most annoying, but most secure

### Mobile/Laptop Use
**Recommended:** Manual password entry
- No dependency on WiFi/NFC reader
- Works anywhere
- Keyboard always available

## Technical Limitations Discovered

### Android NFC HCE
- ❌ Cannot emulate Mifare Classic tags
- ✅ Can emulate Type 4 tags with NDEF
- **Implication:** Firmware must support NDEF parsing to use phone emulation

### BLE RSSI Accuracy
- Typical variance: ±10-15 dBm
- Phone in pocket vs hand: ±8 dBm
- Obstacles (body, laptop): ±10 dBm
- **Implication:** Cannot reliably distinguish <1m distances

### Android Background Restrictions
- Modern Android aggressively kills background services
- BLE scanning limited for battery preservation
- **Implication:** "Always-on" phone scanning unreliable

### Phone Magnet Availability
- iPhone 12+ have MagSafe magnets
- Most Android phones have Qi alignment magnets
- Magnet strength/position varies
- **Implication:** Hall sensor approach not universal

## Future Options (Not Explored)

### Camera + QR Code
- Add camera module to Pico (e.g., Arducam)
- Display QR code on phone screen
- Pico scans QR, extracts key
- **Pros:** No typing, visual confirmation
- **Cons:** Camera module cost/complexity, requires phone screen interaction

### Ultrasonic Pairing
- Phone plays ultrasonic chirp (18-20kHz)
- Pico microphone detects chirp + pattern
- Enables short BLE unlock window
- **Pros:** Direction-sensitive, small hardware
- **Cons:** Still requires phone interaction, unreliable

### Fingerprint Sensor on Pico
- Add fingerprint sensor module to Pico GPIO
- Store fingerprint templates in flash
- **Pros:** No phone needed, secure
- **Cons:** Module cost (~$15), enrollment UX, template security

### Smart Watch Integration
- Use BLE on smartwatch instead of phone
- Watch has better "on wrist" detection
- **Pros:** More reliable proximity (watch always on wrist)
- **Cons:** Not everyone has smartwatch, same BLE range issues

## Conclusion

**The fundamental insight:**

For truly secure "tap and forget" unlocking, the **physical authentication token must travel to the reader**. This is what NFC does (tag travels to reader) and why it works despite the bulk.

All attempts to separate the physical trigger (on Pico) from the authentication (on phone) introduce a security vulnerability where an attacker can trigger one while the victim's device performs the other.

**Practical recommendations:**

1. **For most users:** Use HTTP widget (works today, fully secure, no hardware)
2. **For high security:** Keep using manual password entry
3. **If NFC worth it:** Accept the reader bulk, it's the only "tap and forget" that works
4. **For future:** BLE with manual confirmation (best balance if implemented)

**Don't pursue:**
- Auto-unlock on proximity (insecure)
- Hall sensor tricks (same flaw)
- Beacon broadcasting (extremely insecure)

## References

- `docs/NFC_NDEF_SUPPORT.md` - Adding Android HCE support
- `docs/BLE_UNLOCK.md` - BLE implementation details
- `WIFI_SETUP.md` - HTTP unlock endpoint documentation
- `CLAUDE.md` - Current authentication architecture

## Lessons Learned

1. **Physics is undefeatable:** You cannot precisely measure distance with RSSI
2. **UX vs Security trade-off is real:** Convenience requires compromise
3. **Existing solutions often best:** HTTP widget works great, no need to reinvent
4. **Hardware has hidden costs:** "Small" module still needs wiring, mounting, power
5. **Phone restrictions matter:** Android background limits are real constraints

---

**Bottom line:** If the NFC reader is too bulky, just use the HTTP widget. It's secure, convenient enough, and already works.
