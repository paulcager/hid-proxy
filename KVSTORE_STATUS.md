# KVStore Migration Status

## Overview

The HID proxy has been successfully migrated from legacy flash-based storage to a modern kvstore-based architecture with AES-128-GCM encryption. This document tracks the current status and remaining work.

**Branch:** `kvstore`
**Base:** `master`

---

## ✅ Completed Work

### Phase 1: Simplification (Remove Broken Encryption)
- ✅ Removed non-functional `kvs_securekvs` layer
- ✅ Removed dual-key system
- ✅ Switched to plain `kvs_logkvs` (log-structured KVS with wear leveling)
- ✅ Fixed `keydef_load()` to handle kvstore's size query behavior
- ✅ All keydefs load/save working with unencrypted storage

### Phase 2: Header System (Support Mixed Encryption)
- ✅ Implemented header byte system:
  - `0x00` = unencrypted value
  - `0x01` = encrypted value
- ✅ Created wrapper functions:
  - `kvstore_set_value()` - stores with header byte
  - `kvstore_get_value()` - reads and strips header byte
- ✅ Updated all storage code to use wrappers
- ✅ Tested mixed encrypted/unencrypted data

### Phase 3: AES-128-GCM Encryption
- ✅ Implemented `encrypt_gcm()` using mbedtls
- ✅ Implemented `decrypt_gcm()` with authentication
- ✅ Added random IV generation (12 bytes per encryption)
- ✅ Added authentication tags (16 bytes per encryption)
- ✅ Storage format: `[header(1)][IV(12)][ciphertext][tag(16)]`
- ✅ Private keydefs encrypt automatically on save
- ✅ Public keydefs stored unencrypted
- ✅ Encryption working end-to-end

### Phase 4: Password Validation
- ✅ Implemented password hash storage (`auth.password_hash`)
- ✅ SHA256 hash of PBKDF2-derived key
- ✅ First-time setup detection (no hash = accept any password)
- ✅ Subsequent unlocks validate against stored hash
- ✅ Wrong passwords rejected correctly
- ✅ Lock function clears encryption keys from memory

### Phase 5: HTTP/WiFi Integration
- ✅ Fixed GET `/macros.txt` - `serialize_macros_from_kvstore()`
- ✅ Fixed POST `/macros.txt` - `parse_macros_to_kvstore()`
- ✅ Fixed GET `/status` - counts keydefs from kvstore
- ✅ Removed 10-second WiFi startup delay
- ✅ Web access control working (both-shifts + SPACE)

### Phase 6: Code Cleanup
- ✅ Removed `kb.local_store` allocation and references
- ✅ Cleaned up `lock()` function
- ✅ Cleaned up `flash.c` (removed local_store usage)
- ✅ Removed `assert_sane()` calls
- ✅ Added missing includes
- ✅ Moved `PASSWORD_HASH_KEY` to public header
- ✅ Clean build with no warnings

---

## 🏗️ Current Architecture

### Storage Layers
```
┌─────────────────────────────────────┐
│  Application (key_defs.c, etc.)     │
├─────────────────────────────────────┤
│  keydef_store.c (keydef_save/load)  │
├─────────────────────────────────────┤
│  kvstore_init.c (wrappers + crypto) │
│  - kvstore_set_value() / get_value()│
│  - encrypt_gcm() / decrypt_gcm()    │
├─────────────────────────────────────┤
│  kvs_logkvs (wear leveling)         │
├─────────────────────────────────────┤
│  blockdevice_flash (raw flash I/O)  │
└─────────────────────────────────────┘
```

### Key Files
- **src/kvstore_init.c** - Encryption layer, header handling, password validation
- **src/keydef_store.c** - Keydef persistence API
- **src/key_defs.c** - Main state machine, keydef evaluation
- **src/macros.c** - Text format parser/serializer for HTTP
- **src/http_server.c** - WiFi/HTTP configuration interface
- **src/wifi_config.c** - WiFi connection management
- **src/flash.c** - Legacy stubs (mostly obsolete)

### Storage Keys
- `keydef.0xHH` - Individual keydefs (HH = HID keycode in hex)
- `auth.password_hash` - SHA256 hash of encryption key (unencrypted)
- `wifi.ssid` - WiFi SSID (unencrypted)
- `wifi.password` - WiFi password (unencrypted)
- `wifi.country` - WiFi country code (unencrypted)
- `wifi.enabled` - WiFi enable flag (unencrypted)

### Encryption Details
- **Algorithm:** AES-128-GCM (authenticated encryption)
- **Key derivation:** PBKDF2-SHA256 with device-unique salt
- **Key size:** 16 bytes (AES-128)
- **IV size:** 12 bytes (random, per-encryption)
- **Tag size:** 16 bytes (authentication tag)
- **Overhead:** 29 bytes per encrypted value (1 + 12 + 16)

### Public vs Private Keydefs
- **Private keydefs** (default): `require_unlock = true`
  - Encrypted with AES-128-GCM
  - Only accessible when device unlocked
  - Contains sensitive data (passwords, secrets)

- **Public keydefs**: `require_unlock = false`
  - Stored unencrypted
  - Accessible even when device locked
  - Non-sensitive shortcuts (e.g., text expansion)

---

## 📝 Remaining Work

### High Priority

#### 1. Password Change Support
**Status:** Not implemented
**Location:** src/key_defs.c:167-169 (TODO comment)
**Description:** Currently, to change password, user must erase device (double-shift + DEL) and start over. Need to implement:
- Read all encrypted keydefs with old key
- Derive new key from new password
- Re-encrypt all keydefs with new key
- Update password hash
- Save re-encrypted keydefs

**Estimated effort:** Medium (2-3 hours)

#### 2. Test HTTP POST /macros.txt
**Status:** Code written, not tested
**Description:** The new `parse_macros_to_kvstore()` function needs testing:
- Upload macros.txt via HTTP POST
- Verify keydefs saved to kvstore
- Verify public/private markers honored
- Test error handling

**Estimated effort:** Small (30 minutes)

### Medium Priority

#### 3. Remove Fully Deprecated Code
**Status:** Code present but unused
**Files to clean up:**
- `src/sane.c` - Sanity checking (no-op in release builds)
- `parse_macros()` function - Superseded by `parse_macros_to_kvstore()`
- `serialize_macros()` function - Superseded by `serialize_macros_from_kvstore()`
- `store_t` struct - Legacy type definition

**Estimated effort:** Small (1 hour)

#### 4. Interactive Public/Private Keydef Selection
**Status:** Not implemented
**Current behavior:** All interactive keydefs default to private
**Desired:** During keydef definition (double-shift + =), allow user to mark as public/private

**Possible UI:**
- Before entering definition, prompt for privacy level
- Use a specific key (e.g., 'P' for public, 'S' for secret/private)
- Default remains private for safety

**Estimated effort:** Medium (2 hours)

#### 5. Password Strength Indicator
**Status:** Not implemented
**Description:** Add feedback during password entry:
- Minimum length requirement (e.g., 6 keys)
- Visual indicator via LED blinking pattern
- Reject too-short passwords

**Estimated effort:** Small (1-2 hours)

### Low Priority

#### 6. Documentation Updates
**Files to update:**
- `CLAUDE.md` - Update architecture section with Phase 3/4 completion
- `README.md` - Document password validation behavior
- `WIFI_SETUP.md` - Verify HTTP POST instructions accurate

**Estimated effort:** Small (1 hour)

#### 7. Metrics/Diagnostics
**Description:** Add kvstore health monitoring:
- Flash wear statistics
- Encryption operation counters
- Failed unlock attempt tracking

**Estimated effort:** Medium (3 hours)

#### 8. Backup/Restore
**Description:** Export/import functionality:
- Serialize entire kvstore (including WiFi config)
- Protect exported data with user-provided key
- Import from backup file

**Estimated effort:** Large (6-8 hours)

---

## 🐛 Known Issues

### None Currently

All previously identified issues have been resolved:
- ✅ securekvs authentication failures → Replaced with DIY crypto
- ✅ kvs_get() size queries returning 0 → Workaround in keydef_load()
- ✅ print_keydefs showing wrong sizes → Fixed to load actual keydefs
- ✅ Lock not clearing encryption keys → Fixed in lock() function
- ✅ WiFi 10-second delay → Removed

---

## 🧪 Testing Status

### Tested & Working
- ✅ First-time password setup
- ✅ Correct password unlock
- ✅ Incorrect password rejection
- ✅ Lock/unlock cycle
- ✅ Interactive keydef definition (private)
- ✅ Keydef execution (private, when unlocked)
- ✅ Keydef execution (public, when locked)
- ✅ List keydefs (double-shift + SPACE)
- ✅ Delete all (double-shift + DEL)
- ✅ HTTP GET /macros.txt
- ✅ HTTP GET /status
- ✅ WiFi connection (no delay)
- ✅ Web access control (5-minute timeout)

### Not Yet Tested
- ⏳ HTTP POST /macros.txt (bulk upload)
- ⏳ Password change (not implemented)
- ⏳ Public keydef creation via HTTP
- ⏳ Flash wear over many write cycles
- ⏳ Power loss during write operations

---

## 🎯 Recommended Next Steps

1. **Test HTTP POST** - Verify bulk macro upload works
2. **Implement password change** - Complete the encryption re-keying flow
3. **Add public/private UI** - Allow marking keydefs as public during definition
4. **Clean up deprecated code** - Remove sane.c and old parse/serialize functions
5. **Update documentation** - Sync CLAUDE.md and README.md

---

## 📊 Code Metrics

### Lines of Code Changes
- **Added:** ~800 lines (encryption, wrappers, password validation)
- **Modified:** ~400 lines (migration from local_store to kvstore)
- **Removed:** ~200 lines (dual-key system, local_store allocation)

### Files Modified
- **Core storage:** 4 files (kvstore_init.c, keydef_store.c, flash.c, key_defs.c)
- **HTTP/WiFi:** 3 files (http_server.c, wifi_config.c, macros.c)
- **Headers:** 3 files (kvstore_init.h, hid_proxy.h, macros.h)
- **Total:** 10 files

### Build Status
- **Compiler:** ✅ Clean (no warnings)
- **Linker:** ✅ Success
- **Size:** 965 KB (fits in 2MB flash with 128KB reserved for kvstore)

---

## 🔗 Related Documents

- **KVSTORE_MIGRATION.md** - Original migration plan and rationale
- **CLAUDE.md** - Project overview and architecture guide
- **WIFI_SETUP.md** - WiFi and HTTP configuration guide
- **README.md** - User-facing documentation

---

**Last Updated:** 2025-11-11
**Status:** ✅ Core functionality complete, ready for extended testing and enhancements
