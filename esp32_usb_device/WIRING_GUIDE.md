# Complete Wiring Guide - Dual ESP32-S3 Setup

## Full System Diagram

```
                    ┌─────────────────────┐
                    │  USB Keyboard       │
                    │  (or Mouse)         │
                    └──────────┬──────────┘
                               │ USB
                               ↓
┌──────────────────────────────────────────────────────────┐
│                    ESP32-S3 #1 (USB Host)                │
│  ┌────────────────────────────────────────────────────┐  │
│  │  • Receives HID reports from keyboard              │  │
│  │  • Forwards reports to UART                        │  │
│  │  • GPIO3 = TX, GPIO4 = RX                          │  │
│  └────────────────────────────────────────────────────┘  │
└─────────────────┬──────────────────────┬─────────────────┘
                  │                      │
            GPIO3 (TX)             GPIO4 (RX)
                  │                      │
                  │                      │
                  │   ┌──────────┐       │
                  └───┤   GND    ├───────┘
                      └──────────┘
                  │                      │
            GPIO4 (RX)             GPIO3 (TX)
                  │                      │
┌─────────────────┴──────────────────────┴─────────────────┐
│                   ESP32-S3 #2 (USB Device)               │
│  ┌────────────────────────────────────────────────────┐  │
│  │  • Receives HID reports from UART                  │  │
│  │  • Acts as USB keyboard/mouse to PC                │  │
│  │  • GPIO3 = TX, GPIO4 = RX                          │  │
│  └────────────────────────────────────────────────────┘  │
└───────────────────────────┬──────────────────────────────┘
                            │ USB
                            ↓
                   ┌────────────────┐
                   │   Host PC      │
                   │  (Linux/Win/   │
                   │   macOS)       │
                   └────────────────┘
```

## Physical Connections

### Option 1: Breadboard Setup (Recommended for Testing)

```
ESP32-S3 #1           Breadboard         ESP32-S3 #2
===========           ==========         ===========
GPIO3 (TX) ────────┬─────────────────┬───> GPIO4 (RX)
                   │                 │
GPIO4 (RX) <───────┼─────────────────┼──── GPIO3 (TX)
                   │                 │
GND ───────────────┴─────────────────┴──── GND
```

### Option 2: Direct Jumper Wires (Minimal Setup)

Only 3 wires needed:

1. **ESP32 #1 GPIO3** → **ESP32 #2 GPIO4** (TX → RX)
2. **ESP32 #1 GPIO4** → **ESP32 #2 GPIO3** (RX → TX)
3. **ESP32 #1 GND** → **ESP32 #2 GND** (Common ground)

**Color coding suggestion:**
- Red/Orange: TX lines
- Blue/Green: RX lines
- Black: GND

### Option 3: Permanent Soldered Connection

For permanent installation, use a 3-pin connector:

```
┌───────────────┐                ┌───────────────┐
│  ESP32-S3 #1  │                │  ESP32-S3 #2  │
│               │                │               │
│  [17][18][GND]├────┬───┬───────┤[18][17][GND]  │
└───────────────┘    │   │       └───────────────┘
                     │   │
            Use JST-XH 3-pin connector
            or similar keyed connector
```

## Pin Tables

### ESP32-S3 #1 (USB Host)

| Pin | Function | Direction | Connects To |
|-----|----------|-----------|-------------|
| GPIO19/20 | USB Host D+/D- | Varies | USB OTG cable (keyboard) |
| GPIO3 | UART TX | Output | ESP32 #2 GPIO4 |
| GPIO4 | UART RX | Input | ESP32 #2 GPIO3 |
| GND | Ground | - | ESP32 #2 GND |
| 5V/USB | Power | Input | USB cable (from PC or power supply) |

### ESP32-S3 #2 (USB Device)

| Pin | Function | Direction | Connects To |
|-----|----------|-----------|-------------|
| GPIO19/20 | USB Device D+/D- | Varies | USB cable (to PC) |
| GPIO3 | UART TX | Output | ESP32 #1 GPIO4 |
| GPIO4 | UART RX | Input | ESP32 #1 GPIO3 |
| GND | Ground | - | ESP32 #1 GND |
| 5V/USB | Power | Input | USB cable (to PC) |

## Power Configurations

### Setup A: Both Powered from PC USB (Recommended)

```
PC USB Port 1 ──USB──> ESP32-S3 #1 (5V power + serial console)
PC USB Port 2 ──USB──> ESP32-S3 #2 (5V power + USB device)
                       └── UART ──┘
```

**Pros**: Simple, both devices powered and debuggable
**Cons**: Uses 2 USB ports

### Setup B: ESP32 #1 from External Power

```
USB Power Adapter ──USB──> ESP32-S3 #1
PC USB Port ──────────USB──> ESP32-S3 #2 (must use PC for USB device)
                              └── UART ──┘
```

**Pros**: Only uses 1 PC USB port
**Cons**: Can't monitor ESP32 #1 serial without separate adapter

### Setup C: Shared 5V Rail (Advanced)

```
    ┌─── 5V Power Supply (5V/2A)
    │
    ├───> ESP32-S3 #1 (5V pin)
    │
    └───> ESP32-S3 #2 (5V pin)
          └── USB cable (D+/D- only) to PC
          └── UART between boards
```

**Pros**: Independent power
**Cons**: Requires careful wiring, USB to PC may not work without D+/D- connection

**⚠️ WARNING**: Do NOT connect 5V pins together if both boards are USB-powered. This can damage your boards or PC.

## Cable Lengths

For reliable operation at 921600 baud:

- **UART wires**: < 30cm (12 inches) recommended
- **Longer distances**: Use shielded twisted pair, or reduce baud rate to 115200

For testing on desk:
- 10-15cm jumper wires work perfectly
- Standard Dupont wires are fine

## Common Wiring Mistakes

### ❌ WRONG: TX → TX, RX → RX
```
ESP32 #1          ESP32 #2
GPIO3 (TX) ────> GPIO3 (TX)  ← NO! Both transmitting
GPIO4 (RX) <──── GPIO4 (RX)  ← NO! Both receiving
```

### ✅ CORRECT: TX → RX, RX → TX
```
ESP32 #1          ESP32 #2
GPIO3 (TX) ────> GPIO4 (RX)  ✓ Data flows
GPIO4 (RX) <──── GPIO3 (TX)  ✓ Data flows
```

### ❌ WRONG: Missing GND
```
ESP32 #1          ESP32 #2
GPIO3 ──────────> GPIO4
GPIO4 <────────── GPIO3
(No GND connection!)  ← Signal levels undefined!
```

### ✅ CORRECT: Common GND
```
ESP32 #1          ESP32 #2
GPIO3 ──────────> GPIO4
GPIO4 <────────── GPIO3
GND ──────────────> GND  ✓ Reference voltage
```

## Verification Steps

### 1. Visual Inspection

- [ ] TX from #1 goes to RX on #2
- [ ] RX from #1 comes from TX on #2
- [ ] GND is connected
- [ ] No short circuits between wires

### 2. Continuity Test (Multimeter)

With both boards UNPOWERED:

1. Set multimeter to continuity mode (beep)
2. Touch ESP32 #1 GPIO3 and ESP32 #2 GPIO4 → Should beep
3. Touch ESP32 #1 GPIO4 and ESP32 #2 GPIO3 → Should beep
4. Touch ESP32 #1 GND and ESP32 #2 GND → Should beep
5. Touch GPIO3 and GPIO4 on same board → Should NOT beep

### 3. Voltage Test (Multimeter)

With both boards POWERED:

1. Set multimeter to DC voltage
2. Black probe on GND, red probe on GPIO3 → Should read ~3.3V (idle high)
3. Black probe on GND, red probe on GPIO4 → Should read ~3.3V (idle high)

If you see 0V or 5V, something is wrong!

## USB Cable Types

### For ESP32-S3 #1 (Host)

Need: **USB OTG adapter/cable**

```
ESP32-S3 (Micro USB/USB-C) ──[OTG Cable]── USB-A Female ── Keyboard
```

Examples:
- USB-C to USB-A female adapter
- USB Micro to USB-A female OTG cable

### For ESP32-S3 #2 (Device)

Need: **Standard USB data cable**

```
ESP32-S3 (Micro USB/USB-C) ──[Data Cable]── PC USB Port
```

⚠️ **Must support data**, not just power (test cable with phone data transfer)

## Layout Suggestions

### Desktop Testing Setup

```
┌─────────────────────────────────────┐
│                                     │
│  [ESP32 #1]  ←─UART─→  [ESP32 #2]  │
│       ↑                      ↓      │
│     Keyboard                PC      │
│                                     │
└─────────────────────────────────────┘
     Both on breadboard or desk
```

### Production Enclosure

```
┌──────────────────────────┐
│  ┌────────┐  ┌────────┐  │
│  │ESP32 #1│──│ESP32 #2│  │
│  └───┬────┘  └────┬───┘  │
│      │            │      │
│   USB-A        USB-C     │
└──────┼────────────┼───────┘
   Keyboard        PC
```

## Debugging Wiring Issues

If UART communication fails:

1. **Check serial console output**:
   ```bash
   idf.py -p /dev/ttyUSB0 monitor  # ESP32 #1
   idf.py -p /dev/ttyUSB1 monitor  # ESP32 #2
   ```

2. **Look for these messages**:
   - ESP32 #1: "Sent keyboard report..."
   - ESP32 #2: "Checksum mismatch" or "Invalid packet" = wiring issue

3. **Try loopback test** on each board individually:
   - Wire GPIO3 → GPIO4 on same board
   - Should echo packets back

4. **Reduce baud rate** (edit both `uart_protocol.c` files):
   ```c
   .baud_rate = 115200,  // Was 921600
   ```

## Next Steps

Once wired correctly:
1. Follow [QUICKSTART.md](QUICKSTART.md) for testing
2. Verify full passthrough works
3. Add more features (macros, WiFi, etc.)
