# KVStore Migration Notes - November 2025

This document provides a quick reference for the completed migration from custom flash storage to pico-kvstore.

## What Changed

### Storage Layer
- **Before**: Custom flash sectors with manual erase/program operations
- **After**: pico-kvstore with 3-layer architecture (blockdevice → logkvs → securekvs)

### Encryption
- **Before**: AES-256-CTR (encrypt-only, no authentication)
- **After**: AES-128-GCM with authentication tag (via mbedtls)

### Key Definitions
- **Before**: In-memory array, all loaded at startup
- **After**: On-demand loading per keydef, reduced memory usage

### New Feature: Public/Private Keydefs
- **Public keydefs**: Stored unencrypted, work when device is locked
- **Private keydefs**: Stored encrypted, require device unlock (default)

## Migration Cost

- **Development time**: ~2.5 hours (wall time: ~2 hours)
- **API cost**: $9.70
- **Code changes**: 873 lines added, 362 lines removed
- **Files added**: 4 new files (kvstore_init.c/h, keydef_store.c/h)
- **Files modified**: 7 existing files updated
- **Build size**: ~984KB firmware (minimal change)

## Key Benefits

1. **Better Security**: Authentication prevents tampering (GCM vs CTR)
2. **Wear Leveling**: Log-structured storage extends flash life
3. **Selective Encryption**: Public macros usable without unlock
4. **Memory Efficiency**: On-demand loading reduces RAM usage
5. **Granular Updates**: Save one keydef without rewriting everything

## Breaking Changes

### For Users
- **Macro format**: Now includes `[public]` or `[private]` prefix in HTTP downloads
- **Storage format**: Old flash format incompatible - will need to re-enter macros after upgrade
- **Defaults**: All new keydefs default to private (encrypted) for safety

### For Developers
- `save_state()` / `read_state()` are now no-ops (legacy compatibility stubs)
- `store_encrypt()` / `store_decrypt()` marked obsolete
- Must call `kvstore_init()` at startup
- Use new API: `keydef_save()`, `keydef_load()`, `keydef_delete()`, `keydef_list()`

## Testing Recommendations

Before deploying to your device, test:

1. ✅ **Build verification** - Firmware compiles successfully
2. ⏳ **Interactive keydef creation** - Define a macro via double-shift + = + key
3. ⏳ **Keydef execution** - Verify macro executes correctly
4. ⏳ **Password change** - Change password, verify keydefs re-encrypt
5. ⏳ **Lock/unlock cycle** - Lock device, unlock with password
6. ⏳ **Public keydefs** - Create public macro, verify it works when locked
7. ⏳ **HTTP upload/download** - Download macros.txt, edit, upload (Pico W only)
8. ⏳ **Persistence** - Power cycle, verify keydefs survive reboot
9. ⏳ **NFC unlock** - Write key to NFC, unlock device (if NFC enabled)

## Rollback Plan

If issues arise, you can revert to the previous version:

```bash
git checkout <commit-before-migration>
git submodule update --init --recursive
./build.sh --board pico_w
```

Note: Rolling back will erase all keydefs stored with the new format.

## Flash Usage

- **KVStore allocation**: 128KB at offset 0x1E0000 (last 128KB of 2MB flash)
- **Firmware size**: ~984KB (leaves ~896KB for code/data)
- **Estimated capacity**: ~300-500 keydefs (depends on macro complexity)

## Configuration Storage Schema

### WiFi Configuration
```
wifi.ssid       -> "MyNetwork"      (public)
wifi.password   -> "secret123"      (encrypted)
wifi.country    -> "US"             (public)
wifi.enabled    -> true/false       (public)
```

### Key Definitions
```
keydef.0x3A     -> <binary keydef>  (F1 key, conditional encryption)
keydef.0x3B     -> <binary keydef>  (F2 key, conditional encryption)
keydef.0x04     -> <binary keydef>  ('a' key, conditional encryption)
```

Each keydef is a variable-length structure:
```c
typedef struct {
    uint8_t trigger;           // HID keycode
    uint16_t count;            // Number of reports
    bool require_unlock;       // Encryption flag
    hid_keyboard_report_t reports[count];
} keydef_t;
```

## References

- **KVSTORE_MIGRATION.md** - Complete migration plan and implementation details
- **CLAUDE.md** - Updated architecture documentation
- **README.md** - Updated user-facing documentation
- **pico-kvstore repo**: https://github.com/oyama/pico-kvstore
