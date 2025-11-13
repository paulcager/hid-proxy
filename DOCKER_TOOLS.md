# Docker Builder Image Tools

The Docker builder image (`pico-bld`) includes several pre-built tools to speed up development and enable advanced workflows.

## Pre-built Tools

All tools are available in the container's PATH and ready to use:

### 1. pioasm
**PIO Assembler** - Compiles PIO (Programmable I/O) assembly code for RP2040/RP2350.

**Usage:**
```bash
pioasm input.pio output.h
```

**Note:** Automatically used during builds, no manual invocation typically needed.

---

### 2. picotool
**Pico Device Tool** - Upload binaries, inspect devices, and manage RP2040/RP2350 boards.

**Common commands:**
```bash
# List connected Pico devices
picotool info

# Load a UF2 file to device
picotool load -f myprogram.uf2

# Load binary to specific flash address
picotool load -o 0x10000000 myprogram.bin

# Reboot device
picotool reboot

# Reboot to bootloader (BOOTSEL mode)
picotool reboot -u

# Save flash contents to file
picotool save -r 0x10000000 0x10200000 flash_dump.bin
```

**HID-Proxy specific:**
```bash
# Load firmware
picotool load -f hid_proxy.uf2

# Load pre-configured kvstore image to kvstore region
picotool load -o 0x101e0000 kvstore.bin

# Dump kvstore region from device
picotool save -r 0x101e0000 0x10200000 kvstore_backup.bin
```

---

### 3. kvstore-util
**KVStore Image Manipulation Tool** - Create and edit kvstore image files on the host before flashing to device.

**Commands:**

#### Create a new kvstore image
```bash
kvstore-util create -f kvstore.bin -s 131072
```
- Creates a 128KB image file (131072 bytes)
- Default size: 128KB if `-s` omitted

#### Set a key-value pair
```bash
# Unencrypted value
kvstore-util set -f kvstore.bin -k wifi.ssid -v "MyNetwork"

# Encrypted value (requires 32-char hex encryption key)
kvstore-util set -f kvstore.bin -k wifi.password -v "MyPassword" -e 0123456789abcdef0123456789abcdef
```

#### Get a value
```bash
# Get specific key
kvstore-util get -f kvstore.bin -k wifi.ssid

# Get encrypted key (must provide encryption key)
kvstore-util get -f kvstore.bin -k wifi.password -e 0123456789abcdef0123456789abcdef

# List all keys and values
kvstore-util get -f kvstore.bin
```

#### Delete a key
```bash
kvstore-util delete -f kvstore.bin -k wifi.ssid
```

#### Find keys by prefix
```bash
# List all keys starting with "wifi."
kvstore-util find -f kvstore.bin -k wifi.

# List all keys
kvstore-util find -f kvstore.bin
```

---

## Workflow Examples

### Example 1: Pre-configure WiFi Credentials

Instead of using the `.env` file at build time, you can create a kvstore image with WiFi credentials and flash it separately:

```bash
# 1. Create kvstore image
kvstore-util create -f wifi_config.bin -s 131072

# 2. Add WiFi credentials (unencrypted for initial setup)
kvstore-util set -f wifi_config.bin -k wifi.ssid -v "MyNetwork"
kvstore-util set -f wifi_config.bin -k wifi.password -v "MyPassword"
kvstore-util set -f wifi_config.bin -k wifi.country -v "US"
kvstore-util set -f wifi_config.bin -k wifi.enabled -v "1"

# 3. Flash firmware normally
picotool load -f build/hid_proxy.uf2

# 4. Flash WiFi config to kvstore region
picotool load -o 0x101e0000 wifi_config.bin

# 5. Reboot device
picotool reboot
```

### Example 2: Backup and Restore Configuration

Save your device's kvstore configuration for backup or transfer to another device:

```bash
# Backup kvstore from device
picotool save -r 0x101e0000 0x10200000 my_backup.bin

# Inspect what's in the backup
kvstore-util find -f my_backup.bin

# Restore to another device
picotool load -o 0x101e0000 my_backup.bin
picotool reboot
```

### Example 3: Pre-populate Macros

Create a device image with pre-configured macros (advanced):

```bash
# 1. Create kvstore image
kvstore-util create -f macros.bin -s 131072

# 2. Add macro definitions (would need to serialize keydef_t structures)
# Note: This is complex - HTTP API or interactive definition is easier for macros
# This example shows the concept for simple string data:

kvstore-util set -f macros.bin -k config.device_name -v "WorkKeyboard"

# 3. Flash to device
picotool load -o 0x101e0000 macros.bin
```

**Note:** For actual macro definitions (keydef_t structures), it's easier to use:
- Interactive definition (double-shift + = + key)
- HTTP API upload (if Pico W)
- Let the device create the kvstore entries at runtime

### Example 4: Extract Encryption Key from Device

If you need to access encrypted kvstore data offline:

```bash
# 1. Dump kvstore from device
picotool save -r 0x101e0000 0x10200000 device_kvstore.bin

# 2. Extract encrypted values (requires knowing the encryption key)
# The encryption key is derived from your passphrase via PBKDF2
# You would need to use the same derivation to get the key

# For demonstration with known key:
kvstore-util get -f device_kvstore.bin -k wifi.password -e YOUR_32_CHAR_HEX_KEY
```

---

## KVStore Memory Layout

The HID-Proxy uses the last 128KB of flash for kvstore:

```
Flash Layout (2MB total):
0x10000000 - 0x101E0000 : Firmware (1920KB)
0x101E0000 - 0x10200000 : KVStore (128KB)
```

**Important Addresses:**
- Kvstore start: `0x101E0000` (XIP base + 0x1E0000)
- Kvstore end: `0x10200000`
- Kvstore size: 128KB (131072 bytes)

---

## Building the Docker Image

**Important**: The Docker image must be built from the **project root** (not from the `docker/` directory) because it needs to copy the patched `pico-kvstore` submodule.

```bash
# From project root directory:
docker build -t pico-bld -f docker/Dockerfile .
```

Or use the build script (recommended):

```bash
./build.sh  # Automatically builds the Docker image if needed
```

**Do NOT run** `docker build` from inside the `docker/` directory - it will fail because the build context won't include the pico-kvstore submodule.

**Note:** The Dockerfile includes compatibility patches for pico-kvstore host build with Pico SDK 2.2.0:

1. **Interface library conflicts**: The SDK now provides `pico_rand`, `pico_mbedtls_crypto`, `pico_mbedtls_headers`, and `pico_unique_id` interface libraries in host mode. The patch wraps pico-kvstore's definitions in `if(NOT TARGET ...)` checks to avoid redefinition errors.

2. **Duplicate `get_rand_128` function**: The SDK's pico_rand now provides this function for host builds. The patch removes pico-kvstore's duplicate implementation (lines 47-73 of kvstore_securekvs.c) to avoid linker conflicts.

These patches are automatically applied during Docker image build and do not affect the project's pico-kvstore submodule (which has the mbedtls 3.x patches applied).

---

## Troubleshooting

### picotool can't find device

```bash
# Check USB devices
lsusb | grep -i "Raspberry Pi"

# If device is in bootloader mode, you should see:
# Bus XXX Device XXX: ID 2e8a:0003 Raspberry Pi RP2 Boot

# For picotool to work without sudo:
sudo cp 99-pico.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

### kvstore-util encryption key format

The encryption key must be exactly 32 hexadecimal characters (128 bits):
```bash
# Valid:
-e 0123456789abcdef0123456789abcdef

# Invalid (too short):
-e 0123456789abcdef

# Invalid (non-hex characters):
-e 0123456789abcdefghijklmnopqrstuv
```

To get the encryption key from your passphrase, you'd need to run the PBKDF2 derivation (same as device does). The device code is in `src/encryption.c:enc_derive_key_from_password()`.

---

## References

- **picotool documentation**: https://github.com/raspberrypi/picotool
- **pico-kvstore documentation**: https://github.com/oyama/pico-kvstore
- **Pico SDK documentation**: https://datasheets.raspberrypi.com/pico/raspberry-pi-pico-c-sdk.pdf
- **HID-Proxy kvstore migration**: See KVSTORE_MIGRATION.md in this repository
