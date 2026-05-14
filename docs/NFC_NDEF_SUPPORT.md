# Adding NDEF Support for Android NFC Tools Pro Emulation

**Status**: Not implemented (documented for future reference)

**Goal**: Allow Android phones using NFC Tools Pro card emulation to unlock the device by tapping the PN532 reader.

## Current Implementation

The existing NFC code (`src/nfc_tag.c`) reads **raw Mifare Classic blocks**:
- Authenticates to sector using known keys (array of 16 common keys)
- Reads block 0x3A (bytes 0-15) and 0x3B (bytes 16-31) directly
- No NDEF parsing - expects raw binary key data

## Android HCE Limitation

Android Host Card Emulation (HCE) **cannot** emulate Mifare Classic tags, but **can** emulate:
- ISO 14443-4 Type 4A/4B tags
- NDEF (NFC Data Exchange Format) messages

NFC Tools Pro uses NDEF format when emulating.

## Required Changes

### 1. Detect Type 4 Tags

Add Type 4 tag detection to `nfc_tag.c`:

```c
// Current: InListPassiveTarget for ISO14443A (Mifare)
// Add: Check SAK byte to distinguish Type 4 from Mifare Classic
//
// In read_passive_response_data_t:
//   sel_res == 0x08 or 0x18 → Mifare Classic
//   sel_res == 0x20 or 0x60 → Type 4 (ISO 14443-4)

if (response->sel_res & 0x20) {
    // Type 4 tag - use NDEF reading
    nfc_state.tag_type = TYPE4;
} else {
    // Mifare Classic - use existing block read
    nfc_state.tag_type = MIFARE_CLASSIC;
}
```

### 2. Add NDEF Reading Commands

For Type 4 tags, use different PN532 commands:

```c
// Step 1: Select NDEF application (AID = D2760000850101)
// Command: InDataExchange
// APDU: 00 A4 04 00 07 D2760000850101 00

// Step 2: Select Capability Container (CC file)
// APDU: 00 A4 00 0C 02 E103

// Step 3: Read CC to get NDEF file size
// APDU: 00 B0 00 00 0F

// Step 4: Select NDEF file
// APDU: 00 A4 00 0C 02 E104

// Step 5: Read NDEF message
// APDU: 00 B0 00 00 [length]
```

### 3. Parse NDEF Message Structure

NDEF messages have a specific format:

```
[NDEF Message]
  [NDEF Record 1]
    [Header byte] (MB=1, ME=1, CF=0, SR=1, IL=0, TNF=0x02 for MIME)
    [Type Length] (1 byte)
    [Payload Length] (1 byte for SR, 4 bytes otherwise)
    [Type] (e.g., "application/octet-stream")
    [Payload] (16-byte encryption key)
```

**Simple parser** (assuming single record with binary payload):

```c
typedef struct {
    uint8_t header;
    uint8_t type_length;
    uint8_t payload_length;  // For short record (SR bit set)
    // uint8_t type[type_length];
    // uint8_t payload[payload_length];
} ndef_record_header_t;

bool parse_ndef_key(const uint8_t *ndef_data, size_t len, uint8_t key_out[16]) {
    if (len < 3) return false;

    ndef_record_header_t *rec = (ndef_record_header_t *)ndef_data;

    // Check for short record (SR bit = 0x10)
    if (!(rec->header & 0x10)) {
        // Long record - 4-byte payload length (not implemented)
        return false;
    }

    // Skip header (1) + type_length (1) + payload_length (1) + type
    const uint8_t *payload = ndef_data + 3 + rec->type_length;

    if (rec->payload_length != 16) {
        printf("NDEF payload wrong size: %d (expected 16)\n", rec->payload_length);
        return false;
    }

    memcpy(key_out, payload, 16);
    return true;
}
```

### 4. State Machine Updates

Add new states to `nfc_state.status`:

```c
enum {
    // ... existing states ...
    waiting_for_type4_app_select,    // Selecting NDEF application
    waiting_for_type4_cc_select,     // Selecting CC file
    waiting_for_type4_cc_read,       // Reading CC
    waiting_for_type4_ndef_select,   // Selecting NDEF file
    waiting_for_type4_ndef_read,     // Reading NDEF message
    // ...
};
```

### 5. Integration Points

**In `nfc_task()` (nfc_tag.c:373):**

```c
case waiting_for_type4_ndef_read:
    if (frame_received) {
        uint8_t key[16];
        if (parse_ndef_key(response_data, response_len, key)) {
            printf("Successfully read key from NDEF tag\n");
            if (kvstore_set_encryption_key(key)) {
                printf("Device unsealed via NDEF tag!\n");
                unseal();
            }
        }
        nfc_state.status = idle_until;
    }
    break;
```

## NFC Tools Pro Configuration

On Android phone with NFC Tools Pro:

1. **Card Emulation** tab
2. Create new emulated card
3. Add NDEF record:
   - **Type**: Custom MIME type → `application/octet-stream`
   - **Payload**: Your 16-byte encryption key in hex format
   - Example: `1A2B3C4D5E6F7081920A3B4C5D6E7F80`

4. Enable emulation
5. Tap phone to PN532 reader

## Testing Strategy

1. **Phase 1**: Test with physical NTAG tags first
   - NTAG213/215/216 are Type 4 tags
   - Write NDEF message using NFC Tools Pro "Write" mode
   - Verify Pico can read and parse NDEF

2. **Phase 2**: Test with Android HCE
   - Configure NFC Tools Pro emulation
   - Verify Pico detects Type 4 tag
   - Verify NDEF parsing works

3. **Phase 3**: Maintain backward compatibility
   - Keep Mifare Classic support
   - Auto-detect tag type
   - Test with both tag types

## References

- **NDEF Specification**: NFC Forum NDEF 1.0
- **Type 4 Tag Operation**: NFC Forum Type 4 Tag Operation Specification
- **PN532 User Manual**: NXP PN532 User Manual (InDataExchange for ISO 14443-4)
- **Android HCE**: https://developer.android.com/guide/topics/connectivity/nfc/hce

## Estimated Effort

- **Code changes**: ~200 lines (NDEF parser, Type 4 commands, state machine)
- **Testing**: 2-3 hours (physical tags + phone emulation)
- **Documentation**: 1 hour

## Alternative: Use Existing NDEF Libraries

Consider using an existing NDEF parsing library:
- **nfcpy** (Python, for reference)
- **ndef** (embedded C library) - might be overkill for this use case

For a 16-byte key payload, a custom minimal parser (as shown above) is sufficient.

## Security Considerations

- NDEF messages are **not encrypted** over NFC
- Anyone with physical access can read the key from your phone screen while you unlock
- Consider adding a **PIN/biometric** requirement in the Android HCE app before emitting the key
- Or use BLE instead (see BLE_UNLOCK.md for encrypted proximity-based unlocking)
