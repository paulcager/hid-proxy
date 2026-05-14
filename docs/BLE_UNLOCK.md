# BLE-Based Unlocking for Pico W

**Status**: Not implemented (design document)

**⚠️ IMPORTANT SECURITY WARNING:** Automatic proximity-based unlock (Approach 1, Approach 3) has a fundamental security flaw where an attacker at your desk can trigger the unlock while your phone (nearby but not at desk) auto-responds. See "Security Flaw Analysis" section below and `UNLOCK_OPTIONS_ANALYSIS.md` for full details. **Only Approach 2 (manual confirmation) is secure.**

**Goal**: Unlock the device by bringing your Android phone near the Pico W, using Bluetooth Low Energy (BLE) for
proximity detection and key transfer.

## Hardware Support

✅ **Pico W has built-in Bluetooth 5.2** via the CYW43439 chip (same chip that does WiFi)
✅ **Pico SDK includes BTstack** - full BLE stack support
✅ **No additional hardware needed** - works out of the box on Pico W

## Why BLE is Better Than NFC for This Use Case

| Feature         | NFC (PN532)              | BLE (Built-in)             |
|-----------------|--------------------------|----------------------------|
| Hardware        | ❌ External reader needed | ✅ Built into Pico W        |
| Size            | ❌ Bulky module           | ✅ No extra components      |
| Range           | ❌ ~4cm (must touch)      | ✅ 1-10m (configurable)     |
| Power           | ❌ Always scanning        | ✅ Low power when idle      |
| Security        | ⚠️ No encryption         | ✅ BLE pairing + encryption |
| Android support | ⚠️ HCE limited           | ✅ Full BLE API support     |
| Cost            | ❌ ~$10 for PN532         | ✅ $0 (included)            |

## BLE Unlock Approaches

### `Approach 1: Proximity-Based Auto-Unlock` (Recommended)

**How it works:**

1. Pico W acts as **BLE peripheral** (GATT server)
2. Android phone acts as **BLE central** (GATT client)
3. Phone app stays connected in background
4. When phone comes within range:
    - Phone app detects connection/`RSSI threshold`
    - Automatically sends encrypted key via BLE GATT characteristic
    - Pico unseals

**Advantages:**

- ✅ Most convenient (automatic, no interaction needed)
- ✅ Works while phone in pocket
- ✅ Configurable proximity threshold (RSSI-based)
- ✅ BLE encryption protects key in transit

**Disadvantages:**

- ❌ Requires custom Android app (or Tasker + BLE plugin)
- ❌ Phone must have app running (can use background service)
- ⚠️ Security risk: Anyone with your phone nearby can unlock

**Use case:** You walk up to your desk, device auto-unseals. Walk away, auto-seals.

### Approach 2: Manual Unlock via BLE GATT

**How it works:**

1. Pico W advertises as BLE device (e.g., "HIDProxy-XXXX")
2. User opens Android app, taps "Unlock"
3. App connects, writes 16-byte key to GATT characteristic
4. Pico validates key, unseals

**Advantages:**

- ✅ Explicit user action (better security)
- ✅ Can use generic BLE apps (nRF Connect, BLE Scanner)
- ✅ Simple implementation

**Disadvantages:**

- ❌ Requires opening app and tapping button
- ❌ Less convenient than NFC tap

**Use case:** Manual unlock when you want explicit control.

### Approach 3: BLE Beacon Scanning (Phone-Free)

**How it works:**

1. Android phone continuously broadcasts **BLE beacon** (iBeacon/Eddystone format)
2. Beacon payload includes encrypted key or identifier
3. Pico W scans for known beacon UUID
4. When detected, Pico unseals

**Advantages:**

- ✅ No interaction needed
- ✅ Phone can broadcast beacon in background (even when locked)
- ✅ Multiple authorized phones possible (different beacon UUIDs)

**Disadvantages:**

- ❌ Beacons are **unencrypted broadcasts** (anyone can sniff)
- ⚠️ Security risk if key is in beacon payload
- ❌ Better for "presence detection" than key transfer

**Use case:** Detect when you're nearby, but still require password/PIN for actual unlock.

## Recommended Implementation: Hybrid Approach

Combine proximity detection with secure key transfer:

### Design

**When device is sealed:**

1. Pico W advertises BLE service: `HIDProxy Unlock Service`
2. Android phone scans for this service
3. When detected within range (RSSI > threshold):
    - Phone app prompts for PIN or uses biometric
    - After auth, phone connects and sends encrypted key
    - Pico unseals

**Security features:**

- BLE pairing (bonding) on first use
- PIN/biometric on phone before sending key
- Encrypted GATT characteristic (BLE encryption)
- Timeout after unlock (re-seal after 120 minutes as currently)

**Convenience features:**

- Auto-seal when phone leaves range (optional)
- Multiple authorized devices (store multiple bond info)
- Battery-friendly (BLE advertising, not scanning)

## Technical Implementation

### Pico W Side (Firmware)

**Dependencies:**

- BTstack (included in Pico SDK 2.0+)
- `pico_cyw43_arch_lwip_threadsafe_background` (WiFi + BLE coexistence)

**CMakeLists.txt changes:**

```cmake
target_link_libraries(hid_proxy
        # ... existing libraries ...
        pico_btstack_ble
        pico_btstack_cyw43
)
```

**BLE Service Definition:**

```c
// ble_unlock.h
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Service UUID: 12345678-1234-5678-1234-56789abcdef0 (custom)
#define BLE_UNLOCK_SERVICE_UUID 0xDEF0, 0x9ABC, 0x5678, 0x1234, 0x5678, 0x1234, 0x1234, 0x5678

// Characteristic UUID for key write: 12345678-1234-5678-1234-56789abcdef1
#define BLE_KEY_CHAR_UUID 0xDEF1, 0x9ABC, 0x5678, 0x1234, 0x5678, 0x1234, 0x1234, 0x5678

/*! \brief Initialize BLE unlock service
 *
 * Sets up BLE stack, GATT server, and advertising.
 * Call this during main() initialization after cyw43_arch_init().
 *
 * \return true on success, false on failure
 */
bool ble_unlock_init(void);

/*! \brief BLE task - call periodically from main loop
 *
 * Processes BLE stack events.
 */
void ble_unlock_task(void);

/*! \brief Check if a device is currently connected
 *
 * \return true if BLE client connected
 */
bool ble_unlock_is_connected(void);
```

**BLE Service Implementation (simplified):**

```c
// ble_unlock.c
#include "ble_unlock.h"
#include "btstack.h"
#include "pico/cyw43_arch.h"
#include "kvstore_init.h"
#include "hid_proxy.h"

static uint8_t adv_data[] = {
    // Flags
    0x02, 0x01, 0x06,
    // Complete name
    0x0D, 0x09, 'H', 'I', 'D', 'P', 'r', 'o', 'x', 'y', '-', 'X', 'X', 'X',
    // Service UUID (custom)
    0x11, 0x07, /* ... UUID bytes ... */
};

static uint16_t key_char_handle;
static hci_con_handle_t connection_handle = HCI_CON_HANDLE_INVALID;

// GATT write callback - called when phone writes to key characteristic
static int att_write_callback(hci_con_handle_t con_handle, uint16_t att_handle,
                               uint16_t transaction_mode, uint16_t offset,
                               uint8_t *buffer, uint16_t buffer_size) {
    if (att_handle == key_char_handle) {
        if (buffer_size == 16) {
            printf("BLE: Received 16-byte key, attempting unseal...\n");
            if (kvstore_set_encryption_key(buffer)) {
                printf("BLE: Device unsealed!\n");
                unseal();
                return 0; // Success
            } else {
                printf("BLE: Invalid key\n");
                return ATT_ERROR_WRITE_NOT_PERMITTED;
            }
        } else {
            printf("BLE: Wrong key size: %d (expected 16)\n", buffer_size);
            return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        }
    }
    return 0;
}

bool ble_unlock_init(void) {
    // Initialize BTstack
    l2cap_init();
    sm_init();
    att_server_init(profile_data, NULL, att_write_callback);

    // Setup advertisements
    uint16_t adv_int_min = 0x0030; // 30ms
    uint16_t adv_int_max = 0x0030;
    uint8_t adv_type = 0; // Connectable undirected
    bd_addr_t null_addr = {0};

    gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type, 0, null_addr, 0x07, 0x00);
    gap_advertisements_set_data(sizeof(adv_data), adv_data);
    gap_advertisements_enable(1);

    printf("BLE unlock service initialized\n");
    return true;
}

void ble_unlock_task(void) {
    // BTstack runs in background with async callbacks
    // Nothing needed here unless you want periodic checks
}

bool ble_unlock_is_connected(void) {
    return connection_handle != HCI_CON_HANDLE_INVALID;
}
```

**Integration into main.c:**

```c
#ifdef PICO_CYW43_SUPPORTED
    wifi_init();
    ble_unlock_init();  // Initialize BLE unlock service
#endif

while (true) {
    // ... existing main loop ...
    ble_unlock_task();
}
```

### Android Side (Simple App)

**Option 1: Custom App (Kotlin example):**

```kotlin
// MainActivity.kt
class MainActivity : AppCompatActivity() {
    private val bluetoothAdapter: BluetoothAdapter? = BluetoothAdapter.getDefaultAdapter()
    private val SERVICE_UUID = UUID.fromString("12345678-1234-5678-1234-56789abcdef0")
    private val KEY_CHAR_UUID = UUID.fromString("12345678-1234-5678-1234-56789abcdef1")
    private val encryptionKey = byteArrayOf(/* your 16-byte key */)

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                gatt.discoverServices()
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            val service = gatt.getService(SERVICE_UUID)
            val characteristic = service?.getCharacteristic(KEY_CHAR_UUID)
            characteristic?.value = encryptionKey
            gatt.writeCharacteristic(characteristic)
        }

        override fun onCharacteristicWrite(gatt: BluetoothGatt, char: BluetoothGattCharacteristic, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Toast.makeText(this@MainActivity, "Device unsealed!", Toast.LENGTH_SHORT).show()
            }
            gatt.disconnect()
        }
    }

    fun unlockDevice() {
        // Scan for HIDProxy device
        val scanner = bluetoothAdapter?.bluetoothLeScanner
        scanner?.startScan(object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                if (result.device.name?.startsWith("HIDProxy") == true) {
                    scanner.stopScan(this)
                    result.device.connectGatt(this@MainActivity, false, gattCallback)
                }
            }
        })
    }
}
```

**Option 2: Use Generic BLE Apps:**

Use **nRF Connect** or **BLE Scanner**:

1. Scan for "HIDProxy-XXXX"
2. Connect to device
3. Find "Unlock Service"
4. Write 16 bytes (hex) to "Key Characteristic"

**Option 3: Tasker Automation:**

Use **Tasker + BLE Task Plugin**:

- Profile: BLE Near (HIDProxy-XXXX)
- Task: Connect + Write to characteristic
- Auto-unlock when phone in range

## Security Considerations

### BLE Security Features:

1. **Pairing/Bonding**: Require pairing on first use (PIN entry or Just Works)
2. **Encryption**: BLE link-layer encryption after pairing (AES-128)
3. **Authenticated writes**: Reject writes from non-bonded devices
4. **RSSI limiting**: Only unlock when phone very close (RSSI > -40 dBm)

### Recommended Security Model:

- **Layer 1**: BLE pairing (prevents random devices)
- **Layer 2**: Phone-side PIN/biometric before sending key
- **Layer 3**: Key validation (SHA256 hash check in kvstore)
- **Layer 4**: Auto-seal timeout (re-lock after 120 min)

### Attack Scenarios:

1. **Bluetooth sniffing**: Mitigated by BLE encryption after bonding
2. **Stolen phone**: Mitigated by phone PIN/biometric requirement
3. **Relay attack**: Mitigated by RSSI threshold (must be physically close)
4. **Malicious app on phone**: Can read key from app storage (encrypt key at rest)

## WiFi + BLE Coexistence

The CYW43439 can run WiFi and BLE simultaneously:

```c
// Initialize with both WiFi and BLE support
cyw43_arch_init_with_country(CYW43_COUNTRY_USA);

// WiFi still works
wifi_init();
http_server_init();

// BLE also works
ble_unlock_init();
```

**Tradeoffs:**

- Slightly higher power consumption (both radios active)
- Shared antenna (minimal interference with modern chip)
- Works well in practice for low-throughput BLE

## Power Consumption

Approximate current draw:

- **WiFi only**: ~80mA active, ~15mA idle
- **BLE advertising only**: ~20mA
- **WiFi + BLE**: ~90mA active, ~25mA idle

For USB-powered device (Pico W), this is not a concern.

## Implementation Effort

**Estimated time:**

- Pico W firmware: 4-6 hours (BLE service, GATT, integration)
- Android app (simple): 2-3 hours (scan, connect, write)
- Testing: 2-3 hours (pairing, encryption, range)
- Documentation: 1 hour

**Total**: ~10 hours for full implementation

## Alternatives to Custom Android App

1. **HTTP Shortcuts app** - create widget to call HTTP unlock endpoint (simpler, works today!)
2. **Tasker** - BLE automation triggers
3. **nRF Connect** - manual unlock (for testing/backup)
4. **Physical button** - Add a push button to Pico W GPIO (simplest hardware option)

## Recommendation

**For convenience NOW**: Use HTTP unlock endpoint with Android widget/shortcut
**For future enhancement**: Implement BLE proximity unlock with custom Android app

BLE provides the best balance of security, convenience, and no additional hardware.

## Security Flaw Analysis

### The Fundamental Problem with Auto-Unlock

**Any system combining these two properties is vulnerable:**

1. Physical trigger on the **Pico** (button, hall sensor, proximity detector)
2. Phone **auto-responds** without user confirmation

**Attack Scenario:**

```
[Attacker at desk] → Presses button / triggers sensor
                     ↓
[Pico W]           → Opens BLE unlock window (5 seconds)
                     ↓
[Your phone]       → Detects BLE beacon (you're in room, 3m away)
       ↓             ↓
Auto-sends key ← BLE connection
       ↓
[Pico W unseals] → Attacker has access
```

**Why mitigations don't work:**

| Mitigation | Why It Fails |
|------------|--------------|
| BLE bonding (only your phone) | Doesn't matter - YOUR phone responds when attacker triggers |
| Strict RSSI threshold (-40 dBm) | Phone in pocket vs hand varies ±8 dBm, unreliable |
| Very short window (1 second) | Attacker just waits until you're nearby |
| Accelerometer "placement" detection | False triggers (sitting down with phone in pocket) |
| Geofencing | You're at location, just not at desk |

**The core issue:** Physical trigger and authentication are **decoupled**. Different people can perform each action.

### What Actually Works

**Secure options require ONE of:**

1. **Physical token travels to reader:** NFC tag (token) to NFC reader
2. **Explicit user confirmation:** Tap phone screen/notification
3. **Fresh authentication:** Type password each time

**Approach 2 (Manual Unlock via BLE GATT) is secure** because it requires explicit user action (tapping button in app).

**Approach 1 and 3 are fundamentally insecure** for desk-level physical access scenarios.

### Acceptable Risk Scenarios

Auto-unlock MAY be acceptable if your threat model is:

- ✅ Prevent remote unlock from across building (BLE range limits this)
- ✅ Prevent random devices unlocking (BLE bonding prevents this)
- ❌ Prevent person at your desk unlocking when you're nearby (CANNOT prevent)

If you only care about the first two, and accept that **someone with physical access to your desk can unlock when you're in the room**, then auto-unlock with bonding + RSSI is "good enough."

## Next Steps

**Recommended approach:** Implement **Approach 2 (Manual Unlock via BLE GATT)** for secure proximity-assisted unlocking.

**Alternative:** Use existing HTTP `/unseal` endpoint with Android widget (works today, fully secure, no BLE implementation needed).

**See also:** `docs/UNLOCK_OPTIONS_ANALYSIS.md` for comprehensive comparison of all unlock methods and their trade-offs.

If you want to pursue secure BLE unlock:

1. I can implement the Pico W BLE service (`ble_unlock.c`, `ble_unlock.h`)
2. Provide a basic Android app template (Kotlin + Jetpack Compose)
3. Or document how to use Tasker for automation

Let me know if you want to proceed!
