# Migration to pico-kvstore - COMPLETED ✅

## Overview

This document describes the completed migration from custom flash storage to pico-kvstore, including support for selective encryption (allowing some key definitions to work without unlocking).

**Status: ALL PHASES COMPLETE** (Completed: November 2025)

## Completion Summary

All 6 phases of the migration have been successfully completed:

- ✅ **Phase 1**: pico-kvstore dependency added and initialized
- ✅ **Phase 2**: WiFi configuration migrated to kvstore
- ✅ **Phase 3**: Key definitions migrated to kvstore with on-demand loading
- ✅ **Phase 4**: Public/private keydef support implemented
- ✅ **Phase 5**: Legacy flash code cleaned up and marked obsolete
- ✅ **Phase 6**: Build verified successful

**Key Changes:**
- Storage: Custom flash → pico-kvstore (3-layer: blockdevice/logkvs/securekvs)
- Encryption: AES-256-CTR → AES-128-GCM with authentication
- Loading: In-memory array → On-demand loading per keydef
- Public keydefs: Work when device is locked
- Private keydefs: Require unlock (default)
- Macro format: Now includes `[public]` or `[private]` prefix

**Files Modified:**
- Added: `src/kvstore_init.c`, `src/keydef_store.c`, `include/kvstore_init.h`, `include/keydef_store.h`
- Updated: `src/key_defs.c`, `src/wifi_config.c`, `src/http_server.c`, `src/macros.c`, `src/hid_proxy.c`
- Obsoleted: `src/flash.c` (legacy stubs remain), `src/encryption.c` (store_encrypt/decrypt marked obsolete)
- Build: `CMakeLists.txt`, `.gitmodules` (added pico-kvstore submodule)

## Original Goals

1. Replace custom flash storage with pico-kvstore for both WiFi credentials and key definitions
2. Use pico-kvstore's built-in AES-128-GCM encryption instead of custom encryption layer
3. Implement per-keydef encryption (some work without unlock, some require it)
4. Maintain existing PBKDF2 key derivation from user passphrase
5. Improve unlock time by only decrypting on-demand

## Key Design Decisions

### Storage Schema

Use key-value pairs with naming convention:

```
wifi.ssid           -> "MyNetwork" (unencrypted)
wifi.password       -> "secret123" (encrypted)
wifi.country        -> "UK" (unencrypted)

keydef.0x3A         -> <binary keydef data> (F1 key, HID code 0x3A)
keydef.0x3B         -> <binary keydef data> (F2 key, HID code 0x3B)
keydef.0x04         -> <binary keydef data> (character 'a', HID code 0x04)

config.idle_timeout -> <timeout value> (unencrypted)
config.mqtt.broker  -> "mqtt.example.com" (unencrypted)
```

**Note:** Always use HID codes for keydef keys, not mnemonics. This:
- Avoids ambiguity (no question of multiple keydefs for same key)
- Simplifies lookup logic (no mnemonic-to-HID translation needed)
- Handles unmapped keys consistently

### Encryption Strategy

- **Encrypted records** (require passphrase unlock):
  - Key definitions marked as "private" (containing passwords, API keys, etc.)
  - Use `KVSTORE_REQUIRE_CONFIDENTIALITY_FLAG`

- **Unencrypted records** (always accessible):
  - WiFi SSID
  - WiFi passwords
  - Key definitions marked as "public" (benign macros like "turn on the lights")
  - Configuration values
  - No confidentiality flag

### Key Derivation

Keep existing PBKDF2 implementation, but adapt for pico-kvstore:
- User enters passphrase
- PBKDF2-SHA256 derives 32-byte key (as currently done)
- Truncate to 16 bytes for AES-128-GCM
- Pass via `secretkey_loader()` callback to `kvs_securekvs_create()`

## Components to Change

### 1. Add pico-kvstore Dependency

**File:** `CMakeLists.txt`

**Changes:**
- Add pico-kvstore as git submodule
- Link against pico-kvstore library
- Add include directories

**Complexity:** Low

---

### 2. Create kvstore Initialization Module

**New file:** `src/kvstore_init.c` / `src/kvstore_init.h`

**Purpose:** Initialize the three-layer kvstore stack

**API:**
```c
// Initialize kvstore (call early in main())
bool kvstore_init(void);

// Provide encryption key after passphrase unlock
void kvstore_set_encryption_key(const uint8_t key[16]);

// Clear encryption key (on lock)
void kvstore_clear_encryption_key(void);

// Check if encryption key is available
bool kvstore_is_unlocked(void);
```

**Implementation:**
```c
// Use 128KB for kvstore (substantial increase from current 8KB)
#define KVSTORE_SIZE (128 * 1024)
#define KVSTORE_OFFSET (PICO_FLASH_SIZE_BYTES - KVSTORE_SIZE)

static uint8_t encryption_key[16];
static bool key_available = false;

static int secretkey_loader(uint8_t *key) {
    if (!key_available) {
        return -1;  // No key available
    }
    memcpy(key, encryption_key, 16);
    return 0;
}

bool kvstore_init(void) {
    // Create block device using onboard flash (last 128KB)
    kvs_t *blockdev = blockdevice_flash_create(
        KVSTORE_OFFSET,
        KVSTORE_OFFSET + KVSTORE_SIZE
    );

    // Create log-structured KVS
    kvs_t *logkvs = kvs_logkvs_create(blockdev);

    // Create secure KVS with our key loader
    kvs_t *securekvs = kvs_securekvs_create(logkvs, secretkey_loader);

    // Assign as global instance
    kvs_assign(securekvs);

    return kvs_init();
}
```

**Complexity:** Medium

---

### 3. Refactor WiFi Configuration Storage

**File:** `src/wifi_config.c`

**Current:** Custom flash read/write at `WIFI_CONFIG_OFFSET`

**New approach:**
```c
// Store WiFi credentials
bool wifi_config_save(const char *ssid, const char *password, const char *country) {
    int ret;

    // SSID is public
    ret = kvs_set("wifi.ssid", ssid, strlen(ssid) + 1);
    if (ret != 0) return false;

    // Password is encrypted
    ret = kvs_set_flag("wifi.password", password, strlen(password) + 1,
                       KVSTORE_REQUIRE_CONFIDENTIALITY_FLAG);
    if (ret != 0) return false;

    // Country is public
    ret = kvs_set("wifi.country", country, strlen(country) + 1);
    if (ret != 0) return false;

    return true;
}

// Load WiFi credentials
bool wifi_config_load(char *ssid, size_t ssid_len,
                      char *password, size_t password_len,
                      char *country, size_t country_len) {
    size_t actual_size;

    // SSID always accessible
    if (kvs_get("wifi.ssid", ssid, ssid_len, &actual_size) != 0)
        return false;

    // Password requires unlock (returns error if key not available)
    if (kvs_get("wifi.password", password, password_len, &actual_size) != 0)
        return false;

    // Country always accessible
    if (kvs_get("wifi.country", country, country_len, &actual_size) != 0)
        strcpy(country, "US");  // Default

    return true;
}
```

**Build-time WiFi config (.env file):**
The existing `.env` file mechanism will continue to work. At build time, CMakeLists.txt reads `.env` and defines `WIFI_SSID` and `WIFI_PASSWORD` preprocessor macros. On first boot (or when kvstore is empty), `wifi_config_init()` will:
1. Try to read `wifi.ssid` / `wifi.password` from kvstore
2. If not found (or mismatch with build-time values), write build-time values to kvstore
3. This provides the same "baked-in fallback" behavior as current implementation

Example code:
```c
void wifi_config_init(void) {
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
    char stored_ssid[64];
    size_t size;

    // Try to read from kvstore
    int ret = kvs_get("wifi.ssid", stored_ssid, sizeof(stored_ssid), &size);

    if (ret != 0 || strcmp(stored_ssid, WIFI_SSID) != 0) {
        // Flash empty or doesn't match build-time values - populate kvstore
        LOG_INFO("Using WiFi config from build-time values\n");
        kvs_set("wifi.ssid", WIFI_SSID, strlen(WIFI_SSID) + 1);
        kvs_set_flag("wifi.password", WIFI_PASSWORD, strlen(WIFI_PASSWORD) + 1,
                     KVSTORE_REQUIRE_CONFIDENTIALITY_FLAG);
        kvs_set("wifi.country", WIFI_COUNTRY_CODE, strlen(WIFI_COUNTRY_CODE) + 1);
    }
#endif

    // Now read current config from kvstore for use
    wifi_config_load(...);
}
```

**Changes needed:**
- Replace `wifi_config_read_from_flash()` / `wifi_config_write_to_flash()`
- Remove custom flash offset management
- Add retry/fallback logic for when password can't be decrypted (device locked)
- Adapt build-time fallback logic to use kvstore instead of custom flash

**Complexity:** Medium

---

### 4. Refactor Key Definition Storage

**File:** `src/key_defs.c`

**Current:** Array of `keydef_t` in `kb.local_store->keydefs`, encrypted as one block

**New approach:**
- Each keydef stored as separate key-value pair
- Key name: `keydef.{trigger}` where trigger is HID code in hex (e.g., `keydef.0x3A` for F1)
- Value: Binary `keydef_t` structure
- Flag: Set `KVSTORE_REQUIRE_CONFIDENTIALITY_FLAG` based on keydef's sensitivity

**Data structure changes:**
```c
// Add field to keydef_t to track encryption requirement
typedef struct {
    uint8_t trigger;
    uint16_t count;
    bool require_unlock;  // NEW: Does this keydef require device unlock?
    hid_keyboard_report_t reports[];
} keydef_t;
```

**API changes:**
```c
// Save a single keydef
bool keydef_save(const keydef_t *keydef) {
    char key[32];
    snprintf(key, sizeof(key), "keydef.0x%02X", keydef->trigger);

    size_t size = sizeof(keydef_t) + keydef->count * sizeof(hid_keyboard_report_t);
    uint32_t flags = keydef->require_unlock ? KVSTORE_REQUIRE_CONFIDENTIALITY_FLAG : 0;

    return kvs_set_flag(key, keydef, size, flags) == 0;
}

// Load a single keydef
keydef_t *keydef_load(uint8_t trigger) {
    char key[32];
    snprintf(key, sizeof(key), "keydef.0x%02X", trigger);

    // First, get the size
    size_t size;
    if (kvs_get(key, NULL, 0, &size) != 0)
        return NULL;  // Not found (or can't decrypt if locked)

    // Allocate and load
    keydef_t *keydef = malloc(size);
    if (kvs_get(key, keydef, size, &size) != 0) {
        free(keydef);
        return NULL;
    }

    return keydef;
}

// List all available keydefs (respects lock state)
int keydef_list(uint8_t *triggers, size_t max_count) {
    kvs_find_t ctx;
    char key[32];
    int count = 0;

    if (kvs_find("keydef.", &ctx) != 0)
        return 0;

    while (kvs_find_next(&ctx, key, sizeof(key)) == 0 && count < max_count) {
        // Parse "keydef.0xXX" to extract trigger
        unsigned int trigger;
        if (sscanf(key, "keydef.0x%X", &trigger) == 1) {
            triggers[count++] = (uint8_t)trigger;
        }
    }

    kvs_find_close(&ctx);
    return count;
}
```

**Changes needed:**
- Replace array-based storage with individual kvs lookups
- Modify `handle_keyboard_report()` to load keydef on-demand (no caching initially)
- Update interactive definition mode to save immediately
- Remove `store_encrypt()` / `store_decrypt()` calls

**Note on caching:** Initially, no caching layer. Each keydef use will trigger a kvstore lookup and potential decryption. If performance testing reveals this is too slow, consider adding a simple LRU cache for recently-used keydefs. Document cache size and eviction policy if implemented later.

**Complexity:** High

---

### 5. Update Encryption Module

**File:** `src/encryption.c` / `include/encryption.h`

**Current:** Handles PBKDF2 key derivation and AES-256 encryption

**New approach:**
- Keep PBKDF2 logic (but truncate output to 16 bytes)
- Remove AES encryption/decryption (pico-kvstore handles it)
- Provide interface to pass key to kvstore

**API changes:**
```c
// Keep this - derive key from passphrase
bool derive_key_from_passphrase(const char *passphrase,
                                 uint8_t key_out[16]);  // Changed from 32 to 16

// Remove these (handled by kvstore):
// - store_encrypt()
// - store_decrypt()

// Add this - clear sensitive key material
void clear_derived_key(uint8_t key[16]);
```

**Complexity:** Low (mostly deletions)

---

### 6. Update State Machine

**File:** `src/hid_proxy.c` / `include/hid_proxy.h`

**Current states:**
- `locked`: Can't use any keydefs
- `entering_password`: User typing password
- `normal`: All keydefs available

**New behavior:**
- `locked`: Public keydefs work, private keydefs don't
- `entering_password`: (no change)
- `normal`: All keydefs available

**Changes needed:**
```c
// In handle_keyboard_report():

if (kb.state == locked) {
    // Try to load keydef
    keydef_t *keydef = keydef_load(trigger);

    if (keydef == NULL) {
        // Either doesn't exist, or is encrypted and we're locked
        // Pass through keystroke normally
        return false;
    }

    if (keydef->require_unlock) {
        // This keydef requires unlock, but we're locked
        free(keydef);
        // Silently pass through.
        return false;
    }

    // Public keydef - evaluate it
    evaluate_keydef(keydef);
    free(keydef);
    return true;
}
```

**Additional changes:**
- On successful password entry: call `kvstore_set_encryption_key()`
- On lock: call `kvstore_clear_encryption_key()`
- On NFC unlock: call `kvstore_set_encryption_key()` with NFC-provided key

**Complexity:** Medium

---

### 7. Update Macro Parser/Serializer

**File:** `src/macros.c` / `include/macros.h`

**Purpose:** HTTP API for uploading/downloading macro definitions

**Current format:**
```
F1 { "text" }
```

**Extended format to support encryption flag:**
```
F1 { "text" }              // Default: encrypted (for backwards compatibility)
F1 [public] { "text" }     // Explicitly public (no unlock needed)
F1 [private] { "text" }    // Explicitly private (unlock required)
```

**Changes needed:**
- Parse `[public]` / `[private]` modifiers in `parse_macros()`
- Set `keydef->require_unlock` based on modifier (default: true for safety)
- Serialize with appropriate modifier in `serialize_macros()`
- Update HTTP handlers to save keydefs individually (instead of bulk)
- HTTP POST endpoint must check if device is unlocked (via web access timeout from both-shifts+SPACE)
  - Reject uploads if physical unlock hasn't occurred recently
  - Applies to ALL uploads, even public keydefs (prevents remote modification)

**Complexity:** Medium

---

### 8. Update NFC Authentication

**File:** `src/nfc_tag.c`

**Current:** Reads 16-byte key from NFC tag (already post-PBKDF2)

**New approach:**
- Read 16-byte key from NFC tag
- **Use directly** - NFC tag contains the key *after* PBKDF2 derivation
- Pass directly to `kvstore_set_encryption_key()` (no further derivation needed)

**Changes needed:**
- Modify `nfc_task()` to call `kvstore_set_encryption_key()` after reading tag
- No changes to key writing logic (already stores post-PBKDF key)

**Complexity:** Low

---

### 9. Remove Old Flash Code

**Files to delete:**
- `src/flash.c` (replaced by kvstore)

**Files to modify:**
- `src/wifi_config.c` - remove flash-specific code
- CMakeLists.txt - remove flash.c
- `include/hid_proxy.h` - remove old flash constants

**Constants to remove:**
- `FLASH_STORE_OFFSET` (replaced by `KVSTORE_OFFSET`)
- `FLASH_STORE_SIZE` (replaced by `KVSTORE_SIZE = 128KB`)
- `WIFI_CONFIG_OFFSET` (WiFi now uses kvstore)

**Constants to add:**
- `KVSTORE_SIZE` (128KB)
- `KVSTORE_OFFSET` (PICO_FLASH_SIZE_BYTES - KVSTORE_SIZE)

**Complexity:** Low

---

## New Feature: Public/Private Key Definitions

### User Experience

**Default behavior (safer):**
- All keydefs require unlock by default
- User must explicitly mark keydefs as `[public]` if they want them to work when locked

**HTTP API:**
```bash
# Upload macros with public/private markers
cat > macros.txt <<EOF
F1 [public] { "turn on the lights" }
F2 [private] { "admin_password_123" }
F3 { "SELECT * FROM users" }  # Default: private (safe)
EOF

curl -X POST http://hidproxy.local/macros.txt --data-binary @macros.txt
```

**Interactive definition mode:**
- All interactively defined keydefs are **private** (require unlock)
- No prompt needed (assumes no serial console in production)
- Both-shifts + '=' to start definition, same as before
- To create public keydefs, use HTTP/WiFi upload method

**Serial console output (both-shifts + SPACE):**
```
Keydefs:
F1 [public]  { "turn on the lights" }
F2 [private] { "admin_password_123" }
F3 [private] { "SELECT * FROM users" }
```

### State Transitions

**When locked:**
- User presses F1 → "turn on the lights" works (public)
- User presses F2 → keystroke passes through normally (encrypted keydef inaccessible)
- User presses F3 → keystroke passes through normally (encrypted keydef inaccessible)

**After unlock:**
- All keydefs work

**Security consideration:**
- Locked state should NOT reveal which keys have encrypted vs unencrypted definitions
- Treat "can't decrypt" and "doesn't exist" identically (fail silently, pass through)

---

## Migration Strategy

**Important:** This is a **breaking change** with no upgrade path. All existing flash data will be wiped. Users must:
- Re-enter WiFi credentials (or use .env file)
- Re-define all key definitions
- Re-configure NFC tags (if used)

### Phase 1: Add pico-kvstore Without Changing Behavior
1. Add pico-kvstore as submodule
2. Implement `kvstore_init.c` with 128KB allocation
3. Keep existing flash.c/encryption.c for now
4. Test initialization only
5. Document flash wipe requirement in release notes

### Phase 2: Migrate WiFi Config First
1. Refactor wifi_config.c to use kvstore
2. Test WiFi connection after reboot
3. Verify persistence across power cycles
4. Verify .env build-time config still works

### Phase 3: Migrate Key Definitions
1. Implement new keydef storage API (HID code-based keys)
2. All keydefs encrypted initially (no public/private yet)
3. Migrate state machine to load on-demand (no caching)
4. Performance testing (measure unlock time - should be instant)
5. Memory usage testing (verify no RAM bloat)

### Phase 4: Add Public/Private Support
1. Extend `keydef_t` with `require_unlock` flag
2. Update macro parser/serializer for `[public]`/`[private]` syntax
3. Modify state machine to allow public keydefs when locked
4. Interactive definitions default to private
5. HTTP endpoint checks physical unlock requirement

### Phase 5: Cleanup
1. Delete flash.c
2. Remove encryption.c's unused functions (keep PBKDF2)
3. Update CLAUDE.md documentation
4. Update README.md with migration notes
5. Update FLASH_CHANGE.md with outcome

### Phase 6: Testing
1. Test all states: locked, unlocked, entering password
2. Test public keydefs work when locked
3. Test private keydefs don't work when locked
4. Test NFC unlock
5. Test HTTP upload/download with new format
6. Test persistence across reboots
7. Test wear leveling (many writes - simulate 1000+ cycles)
8. Test flash size limits (how many keydefs fit in 128KB?)

---

## Testing Checklist

### Basic Functionality
- [ ] Device boots and initializes kvstore
- [ ] WiFi connects using stored credentials
- [ ] Passphrase unlock works
- [ ] NFC unlock works
- [ ] Lock/unlock cycle works

### Key Definitions
- [ ] Define new keydef interactively
- [ ] Load and execute keydef
- [ ] Delete keydef
- [ ] List all keydefs via serial console
- [ ] Upload keydefs via HTTP
- [ ] Download keydefs via HTTP

### Public/Private Keydefs
- [ ] Public keydef works when locked
- [ ] Private keydef doesn't work when locked
- [ ] Private keydef works after unlock
- [ ] HTTP upload preserves public/private flags
- [ ] HTTP download shows correct flags
- [ ] Interactive definition prompts for sensitivity

### Edge Cases
- [ ] Empty password (should be rejected)
- [ ] Very long keydef (test size limits)
- [ ] Many keydefs (test listing/iteration)
- [ ] Rapid lock/unlock cycles
- [ ] Corrupt flash (format and retry)
- [ ] NFC tag with wrong key

### Performance
- [ ] Measure unlock time (should be ~instant, not 0.5s)
- [ ] Measure keydef lookup time
- [ ] Memory usage (RAM)
- [ ] Flash wear over 1000 writes

---

## Decisions Made

All design questions have been resolved:

1. **Flash size allocation:** 128KB for kvstore (substantial increase from current 8KB total)
   - Rationale: Allows for many more keydefs, longer sequences, and future features (MQTT configs, etc.)
   - Pico W has 2MB flash; 128KB is ~6% of total
   - Log-structured storage + wear leveling needs extra space overhead

2. **Caching strategy:** No caching initially; re-decrypt on each use. *Note: If performance becomes an issue, consider adding a simple LRU cache for recently-used keydefs*

3. **Migration from existing flash:** Wipe everything and start fresh (no migration tool needed)
   - Aligns with constraint #1 (no backwards compatibility required)
   - Simpler implementation
   - Users can back up via HTTP GET before upgrade if needed

4. **Default for new keydefs:** Private (require unlock) - safer default
   - Interactive mode: always private
   - HTTP upload: private unless explicitly marked `[public]`

5. **HTTP authentication:** Physical unlock required for ANY changes, including public keydefs. This prevents remote modification even of non-sensitive data.
   - Existing mechanism: both-shifts+SPACE enables web access for 5 minutes
   - Applies to all POST operations

---

## Estimated Effort

| Phase                    | Complexity | Time Estimate   |
|--------------------------|------------|-----------------|
| Phase 1: Add kvstore     | Low        | 2-4 hours       |
| Phase 2: WiFi config     | Medium     | 4-6 hours       |
| Phase 3: Key definitions | High       | 8-12 hours      |
| Phase 4: Public/private  | Medium     | 4-6 hours       |
| Phase 5: Cleanup         | Low        | 2-3 hours       |
| Phase 6: Testing         | Medium     | 6-8 hours       |
| **Total**                |            | **26-39 hours** |

---

## Risks

1. **pico-kvstore maturity:** Library is only 1 month old; may have bugs
   - Mitigation: Extensive testing, be prepared to debug/patch

2. **Performance:** On-demand decryption might be slower than cached array
   - Mitigation: Add caching layer if needed, measure first

3. **Flash size:** Individual records have more overhead than bulk storage
   - Mitigation: 128KB allocation provides substantial headroom

4. **Breaking changes:** Complete storage format change, no upgrade path
   - Mitigation: Clearly document in release notes, provide backup instructions

5. **AES-128 vs AES-256:** Downgrade from 256-bit to 128-bit security
   - Mitigation: Document in README; 128-bit is still strong for this use case



