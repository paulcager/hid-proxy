# mbedtls 3.x API Compatibility Patch for pico-kvstore

## Summary

The pico-kvstore library currently uses mbedtls 2.x API calls that are incompatible with mbedtls 3.x (used by Pico SDK 2.2.0+). This document describes the necessary changes to support mbedtls 3.x while maintaining backward compatibility with 2.x.

## Issue

When building pico-kvstore with Pico SDK 2.2.0 or later (which includes mbedtls 3.6.1), the following compilation errors occur in `src/kvstore_securekvs.c`:

```
error: too many arguments to function 'mbedtls_gcm_starts'
error: too many arguments to function 'mbedtls_gcm_update'
error: too many arguments to function 'mbedtls_gcm_finish'
```

## Root Cause

The mbedtls GCM API changed between versions 2.x and 3.x:

### mbedtls_gcm_starts()
**mbedtls 2.x signature:**
```c
int mbedtls_gcm_starts(mbedtls_gcm_context *ctx,
                       int mode,
                       const unsigned char *iv,
                       size_t iv_len,
                       const unsigned char *add,
                       size_t add_len);
```

**mbedtls 3.x signature:**
```c
int mbedtls_gcm_starts(mbedtls_gcm_context *ctx,
                       int mode,
                       const unsigned char *iv,
                       size_t iv_len);
```

The AAD (Additional Authenticated Data) parameters were removed and moved to a separate function `mbedtls_gcm_update_ad()`.

### mbedtls_gcm_update()
**mbedtls 2.x signature:**
```c
int mbedtls_gcm_update(mbedtls_gcm_context *ctx,
                       size_t length,
                       const unsigned char *input,
                       unsigned char *output);
```

**mbedtls 3.x signature:**
```c
int mbedtls_gcm_update(mbedtls_gcm_context *ctx,
                       const unsigned char *input,
                       size_t input_length,
                       unsigned char *output,
                       size_t output_size,
                       size_t *output_length);
```

Parameter order changed and an output length parameter was added.

### mbedtls_gcm_finish()
**mbedtls 2.x signature:**
```c
int mbedtls_gcm_finish(mbedtls_gcm_context *ctx,
                       unsigned char *tag,
                       size_t tag_len);
```

**mbedtls 3.x signature:**
```c
int mbedtls_gcm_finish(mbedtls_gcm_context *ctx,
                       unsigned char *output,
                       size_t output_size,
                       size_t *output_length,
                       unsigned char *tag,
                       size_t tag_len);
```

Additional output buffer parameters were added.

## Patch

The following changes to `pico-kvstore/src/kvstore_securekvs.c` provide compatibility with mbedtls 3.x:

### Location 1: encrypt_value() function (around line 88)

**Original code (mbedtls 2.x):**
```c
ret = mbedtls_gcm_starts(gcm_ctx, encrypt, iv, iv_len, aad, aad_len);
if (ret != 0)
    return ret;
return mbedtls_gcm_update(gcm_ctx, length, input, output);
```

**Updated code (mbedtls 3.x):**
```c
ret = mbedtls_gcm_starts(gcm_ctx, encrypt, iv, iv_len);
if (ret != 0)
    return ret;

if (aad_len > 0) {
    ret = mbedtls_gcm_update_ad(gcm_ctx, aad, aad_len);
    if (ret != 0)
        return ret;
}

size_t olen;
return mbedtls_gcm_update(gcm_ctx, input, length, output, length, &olen);
```

### Location 2: finish_encryption() function (around line 99)

**Original code (mbedtls 2.x):**
```c
return mbedtls_gcm_finish(gcm_ctx, tag, tag_len);
```

**Updated code (mbedtls 3.x):**
```c
size_t olen = 0;
return mbedtls_gcm_finish(gcm_ctx, NULL, 0, &olen, tag, tag_len);
```

## Complete Patched Functions

Here are the complete updated functions for reference:

```c
static int encrypt_value(mbedtls_gcm_context *gcm_ctx, int encrypt,
                         const uint8_t *iv, size_t iv_len,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *input, uint8_t *output, size_t length) {
    int ret;

    // mbedtls 3.x: AAD moved to separate function
    ret = mbedtls_gcm_starts(gcm_ctx, encrypt, iv, iv_len);
    if (ret != 0)
        return ret;

    // Only call update_ad if we have AAD
    if (aad_len > 0) {
        ret = mbedtls_gcm_update_ad(gcm_ctx, aad, aad_len);
        if (ret != 0)
            return ret;
    }

    // mbedtls 3.x: Added output_length parameter
    size_t olen;
    return mbedtls_gcm_update(gcm_ctx, input, length, output, length, &olen);
}

static int finish_encryption(mbedtls_gcm_context *gcm_ctx, uint8_t *tag, size_t tag_len) {
    // mbedtls 3.x: Added output buffer parameters
    size_t olen = 0;
    return mbedtls_gcm_finish(gcm_ctx, NULL, 0, &olen, tag, tag_len);
}
```

## Testing

The patch has been tested with:
- **Pico SDK**: 2.2.0
- **mbedtls**: 3.6.1 (included with Pico SDK)
- **Platform**: RP2040 (Raspberry Pi Pico W)
- **Build system**: CMake with Docker

Verified operations:
- Encryption of key-value pairs with KVSTORE_REQUIRE_CONFIDENTIALITY_FLAG
- Decryption of encrypted values with correct key
- Authentication failure on incorrect key
- GCM tag verification

## Backward Compatibility Consideration

If you want to maintain backward compatibility with mbedtls 2.x, you could use version detection:

```c
#include <mbedtls/version.h>

#if MBEDTLS_VERSION_MAJOR >= 3
    // mbedtls 3.x code path
    ret = mbedtls_gcm_starts(gcm_ctx, encrypt, iv, iv_len);
    if (ret != 0) return ret;

    if (aad_len > 0) {
        ret = mbedtls_gcm_update_ad(gcm_ctx, aad, aad_len);
        if (ret != 0) return ret;
    }

    size_t olen;
    ret = mbedtls_gcm_update(gcm_ctx, input, length, output, length, &olen);
#else
    // mbedtls 2.x code path
    ret = mbedtls_gcm_starts(gcm_ctx, encrypt, iv, iv_len, aad, aad_len);
    if (ret != 0) return ret;

    ret = mbedtls_gcm_update(gcm_ctx, length, input, output);
#endif
```

## References

- mbedtls 3.0 Migration Guide: https://github.com/Mbed-TLS/mbedtls/blob/development/docs/3.0-migration-guide.md
- mbedtls GCM API documentation: https://mbed-tls.readthedocs.io/en/latest/kb/how-to/encrypt-with-gcm/
- Pico SDK repository: https://github.com/raspberrypi/pico-sdk
- pico-kvstore repository: https://github.com/oyama/pico-kvstore

## Contact

This patch was developed during integration of pico-kvstore with a USB HID proxy project using Pico SDK 2.2.0.

For questions or discussion, please open an issue on the pico-kvstore repository.
