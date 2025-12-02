# ESP32-S3 Migration Plan

## Executive Summary

This document outlines a plan to migrate the HID proxy project from Raspberry Pi Pico/Pico W to a dual ESP32-S3 architecture. The migration addresses the current PIO-USB compatibility issues with RP2350 chips by using native USB host/device support.

**Difficulty Rating**: 6/10 (Moderate)
**Estimated Timeline**: 4-6 weeks
**Code Reusability**: ~80%
**Cost**: $16-24 for hardware (vs $6 for Pico W)

---

## ⚡ Architectural Decision (Updated 2025-01-02)

**All application logic will run on ESP32-S3 #2 (USB Device / PC Side)**

### Why This Decision

1. **UART Buffer Overflow Prevention**: Large macros (200+ keystrokes) would overflow UART buffers if expanded on ESP32 #1
2. **Matches Pico Architecture**: Original Pico had all logic on Core 0 (device side)
3. **Simpler UART Protocol**: Only raw HID reports traverse UART, expansion happens in RAM
4. **Natural USB Rate Limiting**: Device side directly controls USB ready state

### System Overview

```
Keyboard → ESP32 #1 (Simple: USB Host → UART)
                ↓ Raw HID reports only
           ESP32 #2 (🧠 All Logic: State Machine, Macros, WiFi, Storage)
                ↓ USB
           PC
```

**See [ARCHITECTURE_DECISION.md](ARCHITECTURE_DECISION.md) for detailed rationale**

---

## Table of Contents

1. [Current Architecture Analysis](#1-current-architecture-analysis)
2. [ESP-IDF NVS Storage Deep Dive](#2-esp-idf-nvs-storage-deep-dive)
3. [Power Consumption Analysis](#3-power-consumption-analysis)
4. [Alternative Microcontrollers](#4-alternative-microcontrollers)
5. [Detailed Migration Plan](#5-detailed-migration-plan)
6. [Code Portability Summary](#6-code-portability-summary)
7. [Timeline & Effort Estimate](#7-timeline--effort-estimate)
8. [Final Recommendations](#8-final-recommendations)

---

## 1. Current Architecture Analysis

### Current Setup (RP2040/RP2350)

The project currently uses:
- **Core 0**: TinyUSB device stack (acts as keyboard to host), state machine, NFC, flash encryption, WiFi/HTTP/MQTT
- **Core 1**: TinyUSB host stack via **PIO-USB** (receives from physical keyboard)
- **Communication**: Three Pico SDK queues (`keyboard_to_tud_queue`, `tud_to_physical_host_queue`, `leds_queue`)

### Key Data Structures

From `hid_proxy.h:32-44`:
```c
typedef struct {
    uint8_t dev_addr;
    uint8_t instance;
    uint16_t len;
    union hid_reports data;  // 8 bytes for keyboard, 5 for mouse
} hid_report_t;
```

### Data Volume

**Very low bandwidth**:
- Keyboard reports: 8 bytes per keystroke (modifier + 6 keycodes)
- Mouse reports: 5 bytes per movement
- LED updates: 1 byte
- Typical rate: <1KB/sec even with heavy typing

---

## 2. ESP-IDF NVS Storage Deep Dive

### What is NVS?

NVS (Non-Volatile Storage) is ESP-IDF's built-in key-value storage system, purpose-built for flash memory with wear leveling and optional encryption. It's conceptually similar to the current `kvstore` implementation but more mature and hardware-optimized.

### Feature Comparison

| Feature | NVS | Current kvstore |
|---------|-----|-----------------|
| **Storage type** | Key-value pairs | Key-value pairs |
| **Max key length** | 15 characters | Varies (custom) |
| **Data types** | int8-64, string, blob | Binary blobs |
| **Wear leveling** | ✅ Built-in | ✅ Via pico-kvstore |
| **Encryption** | ✅ XTS-AES or HMAC-based | ✅ AES-128-GCM (DIY) |
| **Namespaces** | ✅ Up to 254 | ❌ Single namespace |
| **String limit** | 4000 bytes | Limited by KVSTORE_SIZE |
| **Blob limit** | 508KB or 97.6% partition | Limited by KVSTORE_SIZE |

### Storage Format

NVS uses a **log-structured** design similar to kvstore:
- Flash divided into **pages** (typically 4KB each)
- Each entry has: namespace, key, type, value
- Automatic compaction when pages fill up
- **Page states**: Empty → Active → Full → Freeing

### Encryption Support

Two encryption modes (both superior to DIY approach):

#### 1. Flash Encryption-Based

```c
// Keys stored in separate encrypted partition
nvs_flash_secure_init(nvs_sec_cfg_t *cfg);
```
- Uses hardware flash encryption
- Keys protected by eFuse
- Most secure option

#### 2. HMAC-Based (RECOMMENDED)

```c
// Keys derived from eFuse HMAC key at runtime
nvs_flash_secure_init_partition("nvs", &nvs_sec_cfg);
```
- **No keys stored in flash** (derived on-demand)
- Based on XTS-AES (IEEE P1619 disk encryption standard)
- Each entry treated as a sector
- Hardware-accelerated on ESP32-S3

### Performance

- **Init time**: ~0.5s per 1000 keys
- **RAM usage**: 22KB per 1MB of NVS partition
- **Typical partition**: 24KB-512KB

### API Comparison

**Current code:**
```c
// kvstore_init.c
kvstore_set_value("keydef.0x3A", data, len, true);  // encrypted=true
kvstore_get_value("keydef.0x3A", buffer, &len);
```

**ESP-IDF NVS equivalent:**
```c
// With encryption enabled during nvs_flash_init()
nvs_handle_t handle;
nvs_open("storage", NVS_READWRITE, &handle);
nvs_set_blob(handle, "keydef.0x3A", data, len);  // Auto-encrypted
nvs_get_blob(handle, "keydef.0x3A", buffer, &len);
nvs_commit(handle);  // Write to flash
nvs_close(handle);
```

### Migration Strategy

#### Option A: Direct port to NVS (RECOMMENDED)
- Replace `kvstore_set_value()` → `nvs_set_blob()`
- Replace `kvstore_get_value()` → `nvs_get_blob()`
- Use HMAC-based encryption (no password management needed)
- **Effort**: 2-3 days

#### Option B: Keep kvstore
- Port pico-kvstore library to ESP32
- Replace flash APIs (`flash_safe_execute()` → `spi_flash_write()`)
- Keep your AES-128-GCM encryption layer
- **Effort**: 1 week

**Recommendation**: Use NVS. It's battle-tested, hardware-accelerated, and removes ~500 lines of encryption code (kvstore_init.c, encryption.c).

---

## 3. Power Consumption Analysis

### Active Operation Comparison

The HID proxy is **always active** when in use (keyboard passthrough).

| Mode | Pico W | ESP32-S3 (Dual) | Winner |
|------|--------|-----------------|--------|
| **Active USB only** | 25-50mA | 40-80mA (×2) | 🟢 Pico W |
| **Active WiFi** | 125mA | 160-240mA (×2) | 🟢 Pico W |
| **Deep sleep** | 1.3mA | 10µA (×2) | 🟢 ESP32 |

### Reality Check for This Application

- 🔌 **USB-powered** (not battery) - power consumption irrelevant
- ⌨️ **Always active** during use - no sleep mode benefit
- 📶 **WiFi on-demand** (5 min web access window)

**Verdict**: Pico W is more power-efficient, but **it doesn't matter** for a USB-powered keyboard proxy. Both consume <1W, negligible for AC-powered computers.

### Thermal Considerations

- Pico W: Runs cool (~40°C under load)
- ESP32-S3 (×2): Warmer (~50-60°C with WiFi active)
- Neither requires heatsinks for this use case

### Cost Comparison

| Board | Price (qty 1) | Dual Setup |
|-------|---------------|------------|
| Pico W | $6 | N/A |
| ESP32-S3-DevKitC | $8-12 | $16-24 |

**Total cost difference**: ~$10-18 more for dual ESP32 solution.

---

## 4. Alternative Microcontrollers

### No True Single-Chip Solution Exists

Research shows no microcontroller currently offers **dual USB (host+device) + WiFi** on a single chip.

**Why?**
- USB host requires significant hardware resources
- USB device needs separate controller
- WiFi adds more silicon complexity
- Manufacturing/market demand insufficient

### Closest Alternatives

#### 1. Arduino GIGA R1 WiFi

- **MCU**: STM32H747XI (dual-core Cortex-M7 + M4)
- **USB**: 1× USB-C (device) + 1× USB-A (host)
- **WiFi**: Murata 1DX module (separate chip)
- **Pros**: Official Arduino support, dual USB ports
- **Cons**:
  - STM32 ecosystem less mature than ESP-IDF
  - WiFi module separate (not integrated like ESP32)
  - Expensive (~$80-100)
  - Larger form factor
- **Verdict**: ⚠️ Possible but overcomplicated/expensive

#### 2. Renesas RA6M5

- **MCU**: 200MHz Cortex-M33 with TrustZone
- **USB**: Full-speed + High-speed controllers
- **WiFi**: Via external Pmod module (DA16600)
- **Pros**: Excellent security features, dual USB hardware
- **Cons**:
  - WiFi not integrated (external module)
  - Smaller community/ecosystem than ESP32/STM32
  - More expensive dev boards
  - Steeper learning curve
- **Verdict**: ⚠️ Not worth the complexity

#### 3. STM32H7 Series

- **MCU**: Dual-core Cortex-M7 + M4
- **USB**: 2× USB OTG (can run as host+device simultaneously)
- **WiFi**: External module required
- **Pros**: Very powerful, dual OTG support
- **Cons**:
  - No integrated WiFi (need ESP-01/similar)
  - Complex dual-USB software setup
  - STM32CubeMX has poor dual-role support
  - Overkill for HID proxy
- **Verdict**: ❌ Too complex, no WiFi advantage

### Comparison Matrix

| Solution | USB Host | USB Device | WiFi | Ecosystem | Cost | Ease |
|----------|----------|------------|------|-----------|------|------|
| **Dual ESP32-S3** | ✅ Native | ✅ Native | ✅ Integrated | ⭐⭐⭐⭐⭐ | $ | ⭐⭐⭐⭐ |
| Arduino GIGA | ✅ Native | ✅ Native | ⚠️ Module | ⭐⭐⭐ | $$$ | ⭐⭐ |
| Renesas RA6M5 | ✅ Native | ✅ Native | ⚠️ Module | ⭐⭐ | $$ | ⭐⭐ |
| STM32H7 | ✅ Native | ✅ Native | ❌ External | ⭐⭐⭐⭐ | $$ | ⭐ |

**Final Recommendation**: **Stick with dual ESP32-S3**. It's the simplest, cheapest, and best-supported solution.

---

## 5. Detailed Migration Plan

### Recommended Architecture ✅ **DECISION MADE**

**After analysis, logic placement decided: ESP32-S3 #2 (PC Side)**

**Device 1: USB Host (Simple Forwarding)**
- ESP32-S3 with native USB host
- Receives keyboard/mouse from physical device
- **Minimal processing: Just forwards raw HID to UART**
- No state machine, no storage, no WiFi

**Device 2: USB Device (All Logic)** ← **🧠 BRAIN HERE**
- ESP32-S3 with native USB device
- Acts as keyboard/mouse to host computer
- **Handles ALL logic**:
  - State machine (locked/normal/defining)
  - Macro expansion (in RAM, no UART buffer issues)
  - Key definitions storage (NVS)
  - Encryption
  - WiFi, HTTP server, MQTT client
- Receives raw HID reports from Device 1 via UART

### Communication Method: UART

**Why UART is Best:**

1. **Simplicity**
   - Hardware flow control (RTS/CTS) handles backpressure automatically
   - Built-in FIFOs in ESP32 hardware
   - No protocol overhead (unlike SPI which needs chip select, framing)

2. **Adequate Speed**
   - UART at 921600 baud = ~92KB/sec
   - Your traffic: <1KB/sec
   - **Overkill headroom**: 100x the bandwidth needed

3. **Low Latency**
   - Direct hardware interrupt on receive
   - No polling needed
   - <1ms latency typical

4. **Proven Reliability**
   - Hardware error detection (parity, framing)
   - Hardware flow control prevents data loss
   - ESP-IDF has mature UART drivers

5. **Pin Efficiency**
   - Only 4 pins with flow control: TX, RX, RTS, CTS
   - Or 2 pins without flow control (data rate is so low you don't need it)

### Phase 1: Hardware Setup (Week 1)

#### BOM

- 2× ESP32-S3-DevKitC-1 (or similar with exposed UART pins)
- Jumper wires (4× for UART)
- Optional: PN532 NFC module (if keeping NFC)

#### Wiring

```
ESP32-S3 #1 (Host)         ESP32-S3 #2 (Device)
=====================       =======================
GPIO17 (UART1 TX) ------→  GPIO18 (UART1 RX)
GPIO18 (UART1 RX) ←------  GPIO17 (UART1 TX)
GND ---------------------   GND
5V ----------------------   5V (if powering both from one USB)

Optional flow control:
GPIO1 (RTS) ------------→   GPIO2 (CTS)
GPIO2 (CTS) ←-----------    GPIO1 (RTS)
```

**Notes:**
- ESP32-S3 has 3 UART controllers (UART0, UART1, UART2)
- Use UART1 to avoid conflict with USB-Serial (UART0 often reserved for debug)
- GPIO numbers are suggestions; any free GPIOs work

### Phase 2: UART Protocol Implementation (Week 1-2)

#### Create Shared Protocol Header

`common/uart_protocol.h`:
```c
#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>
#include "esp_err.h"

#define UART_PACKET_START 0xAA
#define UART_PACKET_ESC   0xAB
#define UART_MAX_PAYLOAD  256

typedef enum {
    PKT_KEYBOARD_REPORT = 0x01,
    PKT_MOUSE_REPORT = 0x02,
    PKT_LED_UPDATE = 0x03,
    PKT_STATUS = 0x04,
    PKT_ACK = 0x05,
} packet_type_t;

typedef struct __attribute__((packed)) {
    uint8_t start;       // Always UART_PACKET_START
    uint8_t type;        // packet_type_t
    uint16_t length;     // Payload length
    uint8_t payload[UART_MAX_PAYLOAD];
    uint8_t checksum;    // Simple XOR checksum
} uart_packet_t;

// API
esp_err_t uart_protocol_init(int uart_num, int tx_pin, int rx_pin);
esp_err_t uart_send_packet(packet_type_t type, void *data, uint16_t len);
esp_err_t uart_recv_packet(uart_packet_t *packet, TickType_t timeout);

#endif // UART_PROTOCOL_H
```

#### Implementation

`common/uart_protocol.c`:
```c
#include "uart_protocol.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>

#define UART_BUF_SIZE 1024
static const char *TAG = "uart_protocol";
static int g_uart_num = -1;

esp_err_t uart_protocol_init(int uart_num, int tx_pin, int rx_pin) {
    g_uart_num = uart_num;

    uart_config_t uart_config = {
        .baud_rate = 921600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,  // Or UART_HW_FLOWCTRL_CTS_RTS
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(uart_num, UART_BUF_SIZE * 2,
                                        UART_BUF_SIZE * 2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART protocol initialized on UART%d (TX:%d, RX:%d, baud:%d)",
             uart_num, tx_pin, rx_pin, uart_config.baud_rate);
    return ESP_OK;
}

esp_err_t uart_send_packet(packet_type_t type, void *data, uint16_t len) {
    if (len > UART_MAX_PAYLOAD) {
        ESP_LOGE(TAG, "Payload too large: %d > %d", len, UART_MAX_PAYLOAD);
        return ESP_ERR_INVALID_SIZE;
    }

    uart_packet_t pkt = {
        .start = UART_PACKET_START,
        .type = type,
        .length = len,
    };
    memcpy(pkt.payload, data, len);

    // Calculate XOR checksum
    pkt.checksum = 0;
    uint8_t *bytes = (uint8_t*)&pkt;
    for (int i = 0; i < 4 + len; i++) {
        pkt.checksum ^= bytes[i];
    }

    int written = uart_write_bytes(g_uart_num, &pkt, 5 + len);
    if (written != 5 + len) {
        ESP_LOGE(TAG, "UART write failed: wrote %d/%d bytes", written, 5 + len);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t uart_recv_packet(uart_packet_t *packet, TickType_t timeout) {
    uint8_t start;

    // Wait for start byte
    while (1) {
        int len = uart_read_bytes(g_uart_num, &start, 1, timeout);
        if (len <= 0) return ESP_ERR_TIMEOUT;
        if (start == UART_PACKET_START) break;
        ESP_LOGW(TAG, "Skipping invalid start byte: 0x%02X", start);
    }

    packet->start = start;

    // Read header (type + length)
    int len = uart_read_bytes(g_uart_num, &packet->type, 3, timeout);
    if (len != 3) {
        ESP_LOGE(TAG, "Failed to read packet header");
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Read payload + checksum
    len = uart_read_bytes(g_uart_num, packet->payload,
                         packet->length + 1, timeout);
    if (len != packet->length + 1) {
        ESP_LOGE(TAG, "Failed to read payload: got %d, expected %d",
                 len, packet->length + 1);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Verify checksum
    uint8_t calc_checksum = 0;
    uint8_t *bytes = (uint8_t*)packet;
    for (int i = 0; i < 4 + packet->length; i++) {
        calc_checksum ^= bytes[i];
    }

    if (calc_checksum != packet->payload[packet->length]) {
        ESP_LOGE(TAG, "Checksum mismatch: calc=0x%02X, recv=0x%02X",
                 calc_checksum, packet->payload[packet->length]);
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}
```

### Phase 3: ESP32-S3 #1 (USB Host + Processing) (Week 2-3)

#### Directory Structure

```
esp32_host/
├── main/
│   ├── main.c              # Entry point
│   ├── usb_host.c          # TinyUSB host (port from your usb_host.c)
│   ├── key_defs.c          # State machine (reuse from Pico)
│   ├── keydef_store.c      # Storage API (port to NVS)
│   ├── macros.c            # Parser (reuse as-is)
│   ├── encryption.c        # Crypto (reuse mbedTLS parts)
│   └── nfc_tag.c           # Optional NFC (if keeping)
├── components/
│   └── uart_protocol/      # Shared UART library
│       ├── CMakeLists.txt
│       ├── uart_protocol.c
│       └── include/
│           └── uart_protocol.h
├── CMakeLists.txt
└── sdkconfig
```

#### Main Differences from Pico

**Old (Pico):**
```c
// hid_proxy.c - Core 0
int main() {
    multicore_launch_core1(core1_main);  // Launch USB host on Core 1
    queue_init(&keyboard_to_tud_queue, sizeof(hid_report_t), 32);
    // ...
}

// usb_host.c - Core 1
void core1_main() {
    tuh_init(1);
    while(1) tuh_task();
}
```

**New (ESP32-S3 #1):**
```c
// main.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "uart_protocol.h"
#include "tusb.h"

void app_main() {
    // Init UART to ESP32 #2
    uart_protocol_init(UART_NUM_1, GPIO_NUM_17, GPIO_NUM_18);

    // Init USB host
    usb_host_init();

    // Create FreeRTOS tasks
    xTaskCreate(usb_host_task, "usb_host", 4096, NULL, 5, NULL);
    xTaskCreate(state_machine_task, "state_machine", 8192, NULL, 5, NULL);
    xTaskCreate(uart_rx_task, "uart_rx", 2048, NULL, 4, NULL);
}

void usb_host_task(void *arg) {
    while(1) {
        tuh_task();  // TinyUSB host processing
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Replace queue_add() with uart_send_packet()
void send_to_device(hid_keyboard_report_t *report) {
    uart_send_packet(PKT_KEYBOARD_REPORT, report, sizeof(*report));
}
```

#### Storage Migration

**Old (kvstore):**
```c
kvstore_set_value("keydef.0x3A", data, len, true);
```

**New (NVS):**
```c
nvs_handle_t nvs_handle;
nvs_open("storage", NVS_READWRITE, &nvs_handle);
nvs_set_blob(nvs_handle, "kd_0x3A", data, len);  // "kd_" prefix (15 char limit)
nvs_commit(nvs_handle);
nvs_close(nvs_handle);
```

### Phase 4: ESP32-S3 #2 (USB Device + WiFi) (Week 3-4)

#### Directory Structure

```
esp32_device/
├── main/
│   ├── main.c              # Entry point
│   ├── usb_device.c        # TinyUSB device stack
│   ├── wifi_config.c       # WiFi (reuse/adapt from Pico W)
│   ├── http_server.c       # HTTP API (reuse lwIP code)
│   └── mqtt_client.c       # MQTT (reuse from Pico)
├── components/
│   └── uart_protocol/      # Shared UART library
├── CMakeLists.txt
└── sdkconfig
```

#### Main Differences

**Old (Pico W):**
```c
// hid_proxy.c - Core 0
while(1) {
    tud_task();  // Device stack

    hid_report_t report;
    if (queue_try_remove(&tud_to_physical_host_queue, &report)) {
        tud_hid_report(...);
    }
}
```

**New (ESP32-S3 #2):**
```c
// main.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "uart_protocol.h"
#include "tusb.h"

void app_main() {
    // Init UART from ESP32 #1
    uart_protocol_init(UART_NUM_1, GPIO_NUM_17, GPIO_NUM_18);

    // Init USB device
    tud_init(BOARD_TUD_RHPORT);

    // Init WiFi
    wifi_init_sta();

    // Create tasks
    xTaskCreate(usb_device_task, "usb_device", 4096, NULL, 5, NULL);
    xTaskCreate(uart_rx_task, "uart_rx", 2048, NULL, 4, NULL);
    xTaskCreate(wifi_task, "wifi", 4096, NULL, 3, NULL);
}

void uart_rx_task(void *arg) {
    uart_packet_t packet;
    while(1) {
        if (uart_recv_packet(&packet, portMAX_DELAY) == ESP_OK) {
            switch(packet.type) {
                case PKT_KEYBOARD_REPORT:
                    tud_hid_keyboard_report(0, packet.payload, packet.length);
                    break;
                case PKT_MOUSE_REPORT:
                    tud_hid_mouse_report(0, packet.payload, packet.length);
                    break;
            }
        }
    }
}
```

### Phase 5: Testing & Validation (Week 4-5)

#### Test Plan

1. **UART loopback test**
   - Send test packets between ESP32s
   - Verify checksum validation
   - Test error handling (corrupt packets)

2. **USB host test**
   - Connect keyboard to ESP32 #1
   - Verify HID reports received
   - Check UART transmission

3. **USB device test**
   - Connect ESP32 #2 to computer
   - Verify device enumeration
   - Test keystroke passthrough

4. **State machine test**
   - Test double-shift commands
   - Test password entry
   - Test macro definition/playback

5. **WiFi/HTTP test**
   - Configure WiFi credentials
   - Test HTTP endpoints (GET/POST /macros.txt)
   - Verify MQTT publishing

6. **End-to-end test**
   - Full keyboard → ESP32 #1 → UART → ESP32 #2 → PC
   - Test all macro features
   - Verify encryption/storage

### Phase 6: Optional Optimizations (Week 5-6)

1. **Flow control**: Add RTS/CTS if seeing data loss
2. **DMA**: Use UART DMA for zero-copy transfers
3. **Compression**: Add optional packet compression for large macros
4. **OTA updates**: Add ESP-IDF OTA for wireless firmware updates

---

## 6. Code Portability Summary

### Files Needing NO Changes (~40%)

- ✅ `macros.c` / `macros.h` - Pure C, no platform deps
- ✅ `usb_descriptors.c` / `usb_descriptors.h` - TinyUSB (cross-platform)
- ✅ Most of `key_defs.c` - State machine logic (portable)
- ✅ Crypto algorithms in `encryption.c` (mbedTLS is ESP-IDF built-in)

### Files Needing Minor Changes (~40%)

- ⚠️ `wifi_config.c` - Replace `cyw43_arch` → ESP-IDF WiFi API
- ⚠️ `http_server.c` - Already uses lwIP (ESP-IDF compatible)
- ⚠️ `mqtt_client.c` - Already portable (may need ESP-IDF adaptations)
- ⚠️ `keydef_store.c` - Replace kvstore calls → NVS calls

### Files Needing Major Rewrite (~20%)

- ❌ `hid_proxy.c` - Pico SDK → ESP-IDF (queues → UART, multicore → FreeRTOS)
- ❌ `usb_host.c` - PIO-USB config → Native USB OTG config
- ❌ `kvstore_init.c` - Replace with NVS (or port kvstore to ESP-IDF flash API)

---

## 7. Timeline & Effort Estimate

| Phase | Tasks | Duration | Skills Required |
|-------|-------|----------|-----------------|
| **1. Hardware** | Purchase, wire, test UART | 3 days | Basic electronics |
| **2. UART Protocol** | Write protocol layer | 3-5 days | C programming |
| **3. ESP32 #1** | Port host + state machine | 7-10 days | ESP-IDF, TinyUSB |
| **4. ESP32 #2** | Port device + WiFi | 5-7 days | ESP-IDF, lwIP |
| **5. Testing** | Integration tests | 5-7 days | Debugging |
| **6. Optimization** | Polish, OTA, etc. | 3-5 days | Optional |
| **TOTAL** | | **4-6 weeks** | Intermediate C |

**Assumptions:**
- You're comfortable with C programming
- You can dedicate 10-15 hours/week
- You have basic ESP-IDF knowledge (or 1 week learning curve)

---

## 8. Final Recommendations

### Decision Matrix

| Factor | Pico (Current) | Dual ESP32-S3 | Winner |
|--------|----------------|---------------|--------|
| **USB support** | ❌ PIO-USB (broken on RP2350) | ✅ Native OTG | ESP32 |
| **WiFi** | ✅ CYW43 (integrated) | ✅ Native WiFi | Tie |
| **Development** | ⚠️ Limited to RP2040 | ✅ Works on all ESP32-S3 | ESP32 |
| **Code portability** | - | ✅ 80% reusable | ESP32 |
| **Power** | ✅ Lower | ⚠️ Higher (but irrelevant) | Pico |
| **Cost** | ✅ $6 | ⚠️ $16-24 | Pico |
| **Complexity** | ✅ Single chip | ⚠️ Two chips + UART | Pico |
| **Future-proof** | ❌ PIO-USB uncertain | ✅ Native USB stable | ESP32 |

### Recommendation: YES, Migrate to Dual ESP32-S3

**Reasons:**
1. ✅ **Solves your RP2350 problem** - native USB works everywhere
2. ✅ **Better WiFi ecosystem** - ESP-IDF is more mature than CYW43
3. ✅ **80% code reuse** - not starting from scratch
4. ✅ **Better security** - NVS encryption is hardware-accelerated
5. ✅ **OTA updates** - can update firmware over WiFi
6. ✅ **Larger community** - ESP32 is hugely popular

**Trade-offs:**
- ⚠️ $10-18 more expensive
- ⚠️ Two boards instead of one (but still fits in small enclosure)
- ⚠️ 4-6 weeks migration effort

### Alternative: Wait for PIO-USB RP2350 Fix

If you don't want to migrate now:
- Monitor https://github.com/sekigon-gonnoc/Pico-PIO-USB/issues
- RP2350 silicon bugs may be addressed in future revisions
- But no timeline/guarantee this will happen

---

## Next Steps

1. **Order hardware**: 2× ESP32-S3-DevKitC-1
2. **Set up ESP-IDF**: Install toolchain and test "blink" example
3. **Prototype UART**: Implement simple loopback test
4. **Port incrementally**: Start with USB host on ESP32 #1
5. **Test frequently**: Validate each component before moving to next

---

## References

- ESP-IDF Documentation: https://docs.espressif.com/projects/esp-idf/
- ESP32-S3 Datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
- TinyUSB Documentation: https://docs.tinyusb.org/
- Current project README: `/home/paul/git/hid-proxy2/README.md`
- Current project architecture: `/home/paul/git/hid-proxy2/CLAUDE.md`
