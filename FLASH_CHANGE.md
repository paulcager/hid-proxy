# Potential Changes to Flash Storage

## Purpose

1. The current scheme is disjointed, with different code models for storing WiFi details in flash, and for storing keydefs in flash. See wifi_config.c and flash.c - it would be nice if they could both use the same retrieval mechanism.
2. Decryption currently takes about 0.5s. I want to make the area much larger so we can store longer definitions etc., but that would lead to an unacceptably large unlock time.
    * It might be better to have a "decrypt on demand" method rather than "decrypt everything at startup".
    * **This isn't something to worry about now**, but pulling things from Flash into RAM at first use might help reduce the amount of RAM we use - the RAM could be more of a cached version of decrypted data.
3. The current format *may* not be generic enough to store other ideas I have for the future. For example, defining a keystroke to send a message to an MQTT server.
4. Reading / writing to Flash is not what interests me personally, I am more interested in the "trapping keystrokes and doing useful stuff with them" part. Ideally, flash read / write would be handled by a third party library I don't need to worry about.
5. If we are making more use of flash, we need to consider wear levelling. This is more of a "nice to have" rather than a concrete requirement; it just feels "wrong" no to do it.

## Constraints

1. We do **not** need to provide backwards compatability, or an upgrade path. We would erase all data and start again.
2. I am happy with AES-128 encryption (even though we currently use AES-256).

## Possible Library Candidates

### pico-kvstore

Most of what we do could be represented as "key-value" operations. E.g. "Get me the keydef for keycode X", or "get WiFi SSID name".

**Repository:** https://github.com/oyama/pico-kvstore

#### Investigation Results

**Maintenance Status** ✅
- **Very new library** - Created April 7, 2025
- **Actively maintained** - Last commit April 18, 2025 (fixing GCM AAD handling)
- Recent activity shows bug fixes for boundary checks, deletion issues, and cryptographic improvements
- Developer (oyama) appears active in Pico ecosystem (has other Pico libraries, contributes to pico-sdk)

**API Overview** ✅
Simple 4-operation interface:
```c
bool kvs_init(void);
int kvs_set(const char *key, const void *value, size_t size);
int kvs_get(const char *key, void *value, size_t buffer_size, size_t *value_size);
int kvs_delete(const char *key);
int kvs_find(const char *prefix, kvs_find_t *ctx);  // prefix search
```

Layered architecture:
1. Block device (flash access)
2. Log-structured KVS (wear leveling)
3. Secure KVS (optional encryption layer)

**Encryption Implementation** ⚠️ Good news / Bad news

Algorithm: AES-128-GCM (authenticated encryption via mbedtls)

Key handling:
```c
kvs_t *kvs_securekvs_create(kvs_t *underlying_kvs,
                            int (*secretkey_loader)(uint8_t *key));
```
- ✅ You can provide your own 16-byte key via callback!
- ✅ Your existing PBKDF2 code can feed into this
- ⚠️ Key size is 128-bit (you're using 256-bit AES currently)
- Default (NULL callback) uses device ID, marked as "NOT SECURE"

Per-entry security:
- ✅ IV: Randomly generated per record (16 bytes)
- ✅ Salt: Derived from key name + "ENC" prefix via HKDF
- ✅ Each record gets its own IV/salt - addresses your decrypt-on-demand goal

**Wear Leveling** ✅
Yes, built-in through log-structured storage design.

**Size/Performance**
- No explicit limits on key count or value sizes documented
- "Slight overhead due to AES-128-GCM encryption"
- Latency scales with record count but "remains practical"

---

### LittleFS

**Repository:** https://github.com/littlefs-project/littlefs

#### Investigation Results

**Core Library Status** ✅
- **Very mature library** - Created Feb 2017 (8 years old)
- **Actively maintained** - Latest release v2.11.2 (Sept 29, 2025)
- **Battle-tested** - 964 commits, widely adopted in embedded systems
- **Industry standard** - Used by ARM Mbed OS and many commercial products

**Pico SDK Integration Options**

*Option 1: pico-vfs (Recommended)*
- **Repository:** https://github.com/oyama/pico-vfs (same author as pico-kvstore!)
- **Status:** Active, 157 commits, created May 2024
- **PR to pico-sdk:** #1715 (open since May 2024, positive feedback, targeting SDK 2.3.0)
- **API:** POSIX-like (`open`, `read`, `write`, `fopen`, `fread`, `fwrite`)
- **Default:** 1.4MB littlefs mounted at `/` using onboard flash
- **Setup:** Simple 3-step CMake integration + `fs_init()` call

*Option 2: Direct littlefs integration*
- Use canonical littlefs library directly
- Requires writing block device callbacks (read/prog/erase/sync)
- More work, but full control

*Option 3: pico-littlefs* ❌
- **Status:** ARCHIVED Oct 2022, read-only - **Do not use**

**Wear Leveling** ✅
- **Dynamic wear leveling** built-in
- Linear block allocation with circular wrap
- Random start offset on each mount (prevents early-block bias)
- Detects and works around bad blocks automatically

**Encryption Support** ❌
No built-in encryption - you would need to implement at application layer

---

## Comparison Matrix

| Feature                  | pico-kvstore                     | LittleFS (via pico-vfs)          |
|--------------------------|----------------------------------|----------------------------------|
| **Maturity**             | ⚠️ 1 month old (April 2025)      | ✅ 8 years old (Feb 2017)        |
| **Maintenance**          | ✅ Active (same author as vfs)   | ✅ Very active (Sept 2025)       |
| **API Style**            | Key-Value (4 functions)          | POSIX Filesystem (many funcs)    |
| **Encryption**           | ✅ Built-in AES-128-GCM          | ❌ None (DIY)                    |
| **Wear Leveling**        | ✅ Yes (log-structured)          | ✅ Yes (dynamic)                 |
| **Decrypt-on-demand**    | ✅ Per-record encryption         | ❌ Would need custom layer       |
| **RAM Usage**            | Unknown                          | ✅ Bounded, configurable         |
| **Code Complexity**      | Low (minimal API)                | Medium (POSIX API + driver)      |
| **Your Use Case Fit**    | ✅ Perfect for config/keydefs    | ⚠️ Filesystem might be overkill  |
| **Integration**          | Simple (`kvs_init()`)            | Simple via pico-vfs (`fs_init()`)|
| **Future-proofing**      | ⚠️ Very new, unproven            | ✅ Industry standard, stable     |

---

## Decision

**Chosen: pico-kvstore**

**Rationale:**
- Built-in AES-128-GCM solves problem #2 (decrypt-on-demand)
- Directly integrates with existing PBKDF2 key derivation
- Key-value model perfect for WiFi config + keydefs
- Simpler API for our use case
- Per-record encryption = fast unlock
- Same author (oyama) maintains both pico-kvstore AND pico-vfs, showing commitment to Pico ecosystem

**Risk:** Very new library (1 month old), but willing to maintain/fork if needed.
