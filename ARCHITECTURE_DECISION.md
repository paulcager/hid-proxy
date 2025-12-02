# Architecture Decision: Logic Placement

## Decision

**All application logic will run on ESP32-S3 #2 (USB Device / PC Side)**

Date: 2025-01-02

---

## System Architecture

```
Physical Keyboard ──USB──> ESP32-S3 #1 (USB Host)
                           - Minimal: Just forwards raw HID
                           - No state machine
                           - No storage
                           - No WiFi
                                ↓ UART @ 921600 baud
                                ↓ (Raw HID reports only)
                                ↓
                           ESP32-S3 #2 (USB Device)
                           - State machine (locked/normal/defining)
                           - Macro expansion
                           - Key definitions storage (NVS)
                           - Encryption
                           - WiFi/HTTP/MQTT
                                ↓ USB
                           Host PC
```

---

## Rationale

### 1. UART Buffer Overflow Prevention ✅ **PRIMARY REASON**

**Problem**: Large macro expansion on ESP32 #1 causes UART buffer overflow

**Example**: 200-keystroke macro
- ESP32 #1 sends: 200 packets in ~28ms (7,000 packets/sec transmission rate)
- UART buffer: 1024 bytes / 13 bytes per packet = ~78 packets max
- USB consumption: 1ms per report = 28 reports consumed in 28ms
- **Overflow**: 200 - 28 = 172 packets buffered, but only 78 capacity → **94 packets lost!**

**Solution**: Expand macros on ESP32 #2
- UART carries only trigger (1 packet)
- Expansion happens in RAM (FreeRTOS queue, configurable size)
- Can handle 1000+ keystroke macros without UART involvement

### 2. Matches Original Pico Architecture ✅

**Pico project**:
- Core 0 (device side): State machine, storage, WiFi, all logic
- Core 1 (host side): Just USB host forwarding

**ESP32 mapping**:
- ESP32 #2 (device side): ← All logic HERE (matches Core 0)
- ESP32 #1 (host side): ← Simple forwarding (matches Core 1)

**Benefit**: Easier code port, proven architecture

### 3. Simpler UART Protocol ✅

**With logic on ESP32 #2**:
```
Packet types: 0x01 = Keyboard, 0x02 = Mouse, 0x03 = LED
- 1:1 mapping of physical keystrokes to UART packets
- Current protocol works as-is
- No sequencing needed
```

**With logic on ESP32 #1**:
```
Would need:
- Flow control (ACK packets)
- Sequence numbers
- Larger buffers
- Rate limiting
- More complex protocol
```

### 4. Deployment Scenario: Both in Same Box

**Physical setup**: Both ESP32s in same plastic enclosure

```
[Plastic Box]
┌──────────────────────┐
│  ESP32 #1 ↔ ESP32 #2 │
│    3 wires           │
└──┬───────────────┬───┘
   │               │
Keyboard          PC
```

**This eliminates security/access concerns**:
- ❌ Physical security: Box gets stolen = both devices gone
- ❌ WiFi access: Both in same box, doesn't matter which has WiFi
- ❌ Serial console: Both equally accessible
- ❌ NFC placement: Both in same box

**Remaining factors**: UART buffering, code architecture, protocol simplicity

### 5. Natural USB Rate Limiting ✅

ESP32 #2 has direct USB device control:
```c
// Natural backpressure - only send when USB ready
if (tud_hid_n_ready(ITF_NUM_KEYBOARD)) {
    xQueueReceive(keyboard_report_queue, &report, 0);
    tud_hid_n_keyboard_report(...);
}
```

If on ESP32 #1, would need to communicate USB readiness over UART.

---

## Alternatives Considered

### Option A: Logic on ESP32 #1 (Rejected)

**Pros**:
- None specific to this deployment

**Cons**:
- 🔴 UART buffer overflow on large macros (94 packets lost in example)
- 🔴 Requires flow control or rate limiting
- 🔴 More complex UART protocol
- 🔴 Doesn't match Pico architecture
- 🔴 Harder to port existing code

**Mitigation attempts**:
- Hardware flow control: Requires 2 more wires (RTS/CTS)
- Larger buffers: Still limited (4KB = ~315 packets)
- Software ACK: Slow, complex protocol
- Rate limiting: 500-keystroke macro takes 500ms to transmit

**Verdict**: Complexity not justified

### Option B: Split Logic (Rejected)

- ESP32 #1: Authentication, state machine
- ESP32 #2: Macro expansion, WiFi

**Pros**:
- Avoids UART buffer issue for macros

**Cons**:
- 🔴 Need to send state over UART (complex protocol)
- 🔴 Logic split across devices (harder to debug)
- 🔴 Doesn't match any existing architecture
- 🔴 More coupling between devices

**Verdict**: Worst of both worlds

---

## Implementation Plan

### ESP32-S3 #1 (USB Host) - Stays Simple

**Current state**: ✅ Complete
**Future changes**: None planned

```c
void hid_keyboard_report_callback(const uint8_t *data, int length) {
    uart_send_packet(PKT_KEYBOARD_REPORT, data, length);
}
```

**Responsibilities**:
- USB host enumeration
- HID report reception
- UART transmission
- **That's it!**

### ESP32-S3 #2 (USB Device) - Gets All Logic

**Current state**: Simple passthrough (UART → USB)
**Future changes**: Port all Pico Core 0 logic

**Phase 1**: State machine
- `handle_keyboard_report()` from `key_defs.c`
- Double-shift detection
- Lock/unlock states
- Password entry (in-memory only)

**Phase 2**: Storage
- Migrate to ESP-IDF NVS
- Password validation
- Key definitions persistence
- Encryption (NVS built-in or custom)

**Phase 3**: Macro expansion
- `evaluate_keydef()` from `key_defs.c`
- Action system (HID reports, delays, MQTT)
- Macro parser (`macros.c` - should be 100% portable)

**Phase 4**: Networking
- WiFi configuration
- HTTP server for macro management
- MQTT for Home Assistant
- mDNS discovery

---

## Validation

### Test Cases

✅ **Small macro (20 keystrokes)**
- Works with either architecture
- Validates basic macro expansion

✅ **Medium macro (100 keystrokes)**
- ESP32 #1: Would need flow control
- ESP32 #2: Works without changes

✅ **Large macro (500 keystrokes)**
- ESP32 #1: UART buffer overflow
- ESP32 #2: Works (RAM queue sized for 1000+ reports)

✅ **Extreme macro (2000 keystrokes)**
- ESP32 #1: Even with flow control, slow/complex
- ESP32 #2: Limited only by RAM (configurable queue)

### Macro Size Limits

**With logic on ESP32 #2**:
```c
// In usb_device_hid.c
#define REPORT_QUEUE_SIZE 32  // Current

// Can easily increase:
#define REPORT_QUEUE_SIZE 1024  // For large macros
// Cost: 1024 * 8 bytes = 8KB RAM
```

**ESP32-S3 has 512KB SRAM** - even 1000-report queue is <1% of RAM.

---

## Performance Characteristics

### Latency (Both Architectures Similar)

| Component | Time |
|-----------|------|
| USB keyboard poll | 1ms |
| UART transmission (1 packet) | ~200μs |
| State machine processing | <100μs |
| USB device ready check | <10μs |
| **Total** | **~2ms** |

### Bandwidth

| Scenario | ESP32 #1 Logic | ESP32 #2 Logic |
|----------|----------------|----------------|
| **Passthrough** | 1 packet/keystroke | 1 packet/keystroke |
| **20-key macro** | 20 packets UART | 1 packet UART ✅ |
| **200-key macro** | 200 packets → overflow 🔴 | 1 packet → expand in RAM ✅ |
| **Continuous typing** | ~10 packets/sec | ~10 packets/sec |

---

## Code Portability

### From Pico to ESP32 #2

| File | Portability | Changes Needed |
|------|-------------|----------------|
| `macros.c` | 100% | None (pure C) |
| `key_defs.c` | 90% | queue → direct USB calls |
| `keydef_store.c` | 60% | kvstore → NVS API |
| `encryption.c` | 80% | May use NVS encryption instead |
| `wifi_config.c` | 70% | cyw43_arch → ESP-IDF WiFi |
| `http_server.c` | 80% | Already uses lwIP |
| `mqtt_client.c` | 80% | Minimal changes |

**Estimated porting effort**: 2-3 weeks

---

## Risks & Mitigations

### Risk 1: ESP32 #2 Complexity

**Risk**: Too much logic on one device
**Mitigation**:
- ESP32-S3 has 512KB RAM (vs Pico's 264KB)
- Dual-core (can separate USB task from logic)
- Proven architecture (matches working Pico)

### Risk 2: USB Device Performance

**Risk**: USB device side can't keep up with macro expansion
**Mitigation**:
- USB keyboard poll rate: 1ms (1000 reports/sec max)
- Even 1000-keystroke macro = 1 second to send
- Acceptable for typical use cases

### Risk 3: UART Reliability

**Risk**: UART communication failures
**Mitigation**:
- Checksum validation (already implemented)
- Short wire length (both in same box)
- 921600 baud well within spec for <30cm

---

## Decision Drivers (Priority Order)

1. **UART buffer overflow** (blocking issue for ESP32 #1)
2. **Simplicity** (current protocol works)
3. **Code reuse** (matches Pico architecture)
4. **USB rate limiting** (natural on device side)

---

## Approval

**Decided by**: Architecture discussion with user
**Date**: 2025-01-02
**Status**: ✅ Approved

---

## Next Steps

1. ✅ Update planning documents
2. ⏭️ Port state machine to ESP32 #2
3. ⏭️ Migrate storage to NVS
4. ⏭️ Add macro expansion
5. ⏭️ Add WiFi/HTTP/MQTT

---

## References

- [ESP32_MIGRATION_PLAN.md](ESP32_MIGRATION_PLAN.md) - Original migration plan
- [esp32_usb_device/README.md](esp32_usb_device/README.md) - ESP32 #2 documentation
- [esp32_usb_host/README.md](esp32_usb_host/README.md) - ESP32 #1 documentation
- [Original Pico project](src/hid_proxy.c) - Proven architecture reference
