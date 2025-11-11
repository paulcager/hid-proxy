# Dual-Key Encryption System Migration

**Date**: 2025-11-11
**Status**: ✅ Implemented and building successfully

## Problem

The previous architecture attempted to mix encrypted and unencrypted data in the same kvstore using per-item flags (`KVSTORE_REQUIRE_CONFIDENTIALITY_FLAG`). This created complications:
- SecureKVS layer wrapping everything required a key even for public data
- When device locked (`key_available = false`), `secretkey_loader()` returned -1
- Public data became inaccessible when it shouldn't require the secure key
- Complex flag-based logic scattered throughout the codebase

## Solution

Implemented a **dual-key system** based on proof-of-concept in `../kv-poc`:
- Single kvstore with SecureKVS layer
- Two encryption keys managed by `secretkey_loader()` callback
- Dynamic key switching based on data privacy requirements
- Transparent fallback for reading both public and private data when unlocked

## Architecture

### Two Encryption Keys

1. **default_key** (kvstore_init.c:18-21)
   - Hardcoded constant: `0xDE 0xFA 0x17 0x00 0x00 0x11 0x22 0x33...`
   - Always available (even when device locked)
   - Used for: WiFi config (SSID, password, country), public keydefs

2. **secure_key** (kvstore_init.c:22)
   - Derived from user password via PBKDF2
   - Only available after unlock
   - Used for: private keydefs (passwords, sensitive macros)

### Key Selection

**Writing**:
- Call `kvstore_use_default_key()` before writing public data
- Call `kvstore_use_secure_key()` before writing private data
- `secretkey_loader()` returns the active key

**Reading**:
- Use `kvs_get_any()` for transparent access
- Tries current key first
- On `KVSTORE_ERROR_AUTHENTICATION_FAILED`, retries with opposite key
- Returns first successful read

### State Transitions

**Device Locked** (boot or timeout):
```
use_secure_key = false
secure_key_available = false
→ Only default_key available
→ Can read: WiFi config, public keydefs
→ Cannot read: private keydefs (auth failure)
```

**Device Unlocked** (password entered):
```
use_secure_key = true
secure_key_available = true
→ Both keys available via kvs_get_any() fallback
→ Can read: WiFi config, public keydefs, private keydefs
```

## Implementation Details

### Modified Files

1. **kvstore_init.c/h**
   - Added `default_key` constant and `secure_key` variable
   - Added `use_secure_key` flag
   - Modified `secretkey_loader()` to switch keys dynamically (always succeeds)
   - Added `kvstore_use_default_key()` / `kvstore_use_secure_key()` functions
   - Added `kvs_get_any()` helper for transparent key fallback
   - Updated `kvstore_set_encryption_key()` to set secure key and switch mode
   - Updated `kvstore_clear_encryption_key()` to clear secure key and switch to default

2. **keydef_store.c**
   - Removed `kvs_set_flag()` with `KVSTORE_REQUIRE_CONFIDENTIALITY_FLAG`
   - Added key switching in `keydef_save()` based on `keydef->require_unlock`
   - Updated `keydef_load()` to use `kvs_get_any()` for transparent access
   - Simplified logic - no more flag management

3. **wifi_config.c**
   - All WiFi config (including password) now uses `default_key`
   - Added `kvstore_use_default_key()` calls in save/load functions
   - Removed special encryption handling for WiFi password
   - Added comment explaining WiFi creds are not considered sensitive

4. **CLAUDE.md**
   - Added "Dual-Key Encryption System" section documenting architecture
   - Updated WiFi Configuration section to note public encryption
   - Added key management functions to "Important Code Locations"
   - Added default_key constant to "Configuration Constants"

## Testing Checklist

- [x] Build succeeds (tested: 2025-11-11, build/hid_proxy.uf2 created)
- [x] Device boots successfully
- [ ] Create public keydef while unlocked
- [ ] Create private keydef while unlocked
- [ ] Lock device → verify public keydef works, private fails
- [ ] Unlock device → verify both public and private keydefs work
- [ ] WiFi credentials persist across lock/unlock
- [ ] HTTP API access to macros works when unlocked

## Key Functions

### kvstore_init.c

```c
// Set secure key and switch to secure mode (called after password entry)
void kvstore_set_encryption_key(const uint8_t key[16]);

// Clear secure key and switch to default mode (called on lock)
void kvstore_clear_encryption_key(void);

// Manually switch active key (for write operations)
void kvstore_use_default_key(void);
void kvstore_use_secure_key(void);

// Transparent read with fallback (tries both keys)
int kvs_get_any(const char *key, void *buffer, size_t bufsize, size_t *actual_size);
```

### Usage Pattern

**Writing public data**:
```c
kvstore_use_default_key();
kvs_set("wifi.ssid", ssid, strlen(ssid) + 1);
```

**Writing private data**:
```c
kvstore_use_secure_key();  // No-op if device locked
kvs_set("keydef.0x3A", keydef, size);
```

**Reading any data**:
```c
// Automatically tries both keys
kvs_get_any("keydef.0x3A", buffer, bufsize, &size);
```

## Migration from Previous System

Old code using flags:
```c
// OLD: Required explicit flag management
kvs_set_flag(key, data, size, KVSTORE_REQUIRE_CONFIDENTIALITY_FLAG);
```

New code with key switching:
```c
// NEW: Switch key before write
kvstore_use_secure_key();  // or kvstore_use_default_key()
kvs_set(key, data, size);
```

For reads, replace `kvs_get()` with `kvs_get_any()` to enable transparent fallback.

## Security Considerations

1. **Default key is not cryptographically secure** - it's security-by-obscurity
   - Consider using a device-unique key derived from flash ID + salt
   - Current implementation prioritizes simplicity over maximum security

2. **WiFi password stored with default key**
   - Acceptable for this PoC - WiFi creds are not user secrets
   - Physical access to device required to extract (flash read)

3. **Secure key properly cleared on lock**
   - Uses `memset()` to zero memory
   - Timeout auto-locks after 120 minutes of inactivity

## References

- Proof-of-concept: `../kv-poc/main.c`
- pico-kvstore: `pico-kvstore/` submodule
- mbedtls AES-128-GCM: Used by kvstore_securekvs layer
