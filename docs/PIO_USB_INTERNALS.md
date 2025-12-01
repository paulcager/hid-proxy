# PIO-USB Internals

This document describes how the Pico-PIO-USB library implements USB 1.1 (Full/Low speed) using the RP2040's PIO state machines.

## Overview

The library uses **3 PIO state machines** to bit-bang USB signaling on GPIO pins:

| State Machine | Purpose | Clock Rate (FS) | Clock Rate (LS) |
|---------------|---------|-----------------|-----------------|
| TX | NRZI transmitter | 48 MHz (4x) | 6 MHz (4x) |
| RX (decoder) | NRZI decoder | Max speed | Max speed |
| EOP (edge detector) | Edge detection & sync | 96 MHz (8x) | 12 MHz (8x) |

## TX State Machine

**Source:** `Pico-PIO-USB/src/usb_tx.pio` (programs: `usb_tx_dpdm`, `usb_tx_dmdp`)

### How It Works

1. **Pre-encoding:** The CPU pre-encodes USB data into 2-bit values representing PIO jump targets:
   - `0b00` = SE0 (both lines low, used for End-of-Packet)
   - `0b01` = J state (D+ high, D- low for FS; opposite for LS)
   - `0b10` = K state (D+ low, D- high for FS; opposite for LS)
   - `0b11` = Complete (packet finished)

2. **Transmission:** The PIO program uses `out pc, 2` to read 2 bits from the TX FIFO and jump to different addresses. This effectively creates a lookup table where each 2-bit value selects a different output pattern.

3. **Sideset pins** directly drive D+/D- differential signaling.

4. **Timing:** Runs at 4x the bit rate (48 MHz for 12 Mbps full-speed, 6 MHz for 1.5 Mbps low-speed).

### Key Instructions

```asm
out pc, 2           side FJ_LK [3]   ; Jump based on 2 bits, output J/K
set pindirs, 0b00   side FJ_LK [3]   ; Release bus (tri-state)
set pindirs, 0b11   side FJ_LK       ; Drive bus
irq IRQ_TX_EOP      side SE0 [7]     ; Signal EOP, output SE0
```

## RX State Machines

**Source:** `Pico-PIO-USB/src/usb_rx.pio`

### Edge Detector (`usb_edge_detector`)

Runs at 8x the bit rate to precisely detect signal transitions.

**Purpose:**
- Synchronize to incoming USB signal edges
- Fire `IRQ DECODER_TRIGGER` on each transition to wake the decoder
- Detect SE0 condition (both D+ and D- low) for End-of-Packet

**Key Logic:**
```asm
; Wait for falling edge to detect packet start
start:
    jmp pin start               ; Wait for fall edge
    irq IRQ_RX_START [1]        ; Signal packet start

; Main loop: detect edges and trigger decoder
pin_still_low:
    irq DECODER_TRIGGER [1]     ; Trigger NRZI decoder

; Resync on rising edge
pin_low:
    jmp pin pin_went_high       ; Check for transition
    ...

; Check for EOP (SE0 condition)
pin_still_high:
    mov x, isr [1]
    jmp x-- eop                 ; Jump to eop if both pins high (inverted = SE0)
```

### NRZI Decoder (`usb_nrzi_decoder`)

Runs at maximum CPU speed (no clock divider) for lowest latency.

**Purpose:**
- Decode NRZI encoding: transition = 0, no transition = 1
- Handle bit stuffing (after 6 consecutive 1s, a stuff bit is inserted)
- Shift decoded bits into ISR, autopush to RX FIFO at 8 bits

**NRZI Encoding:**
- USB uses NRZI (Non-Return-to-Zero Inverted)
- A `0` bit causes a transition (J→K or K→J)
- A `1` bit maintains the current state (no transition)

**Key Logic:**
```asm
set_y:
    set y, BIT_REPEAT_COUNT     ; y = 6 (for bit stuffing)
irq_wait:
    wait 1 irq DECODER_TRIGGER  ; Wait for edge detector signal
    jmp !y flip                 ; If y==0, this is a stuff bit - ignore it
    jmp PIN pin_high            ; Check current pin state
pin_low:
    jmp !x K1                   ; x tracks previous state
    ; ... decode based on transition
flip:
    mov x, ~x                   ; Toggle state tracker
```

**Bit Stuffing:**
- After 6 consecutive 1-bits, the transmitter inserts a 0-bit
- The decoder counts consecutive 1s in register Y
- When Y reaches 0, the next bit is discarded (stuff bit)

## Data Flow

```
Transmit Path:
┌─────────┐    ┌──────────────┐    ┌────────┐    ┌─────────┐
│   CPU   │───▶│ Pre-encode   │───▶│ TX SM  │───▶│ D+/D-   │
│         │    │ (2-bit vals) │    │ (PIO)  │    │ pins    │
└─────────┘    └──────────────┘    └────────┘    └─────────┘

Receive Path:
┌─────────┐    ┌────────────┐    ┌──────────┐    ┌─────────┐
│ D+/D-   │───▶│ Edge Det.  │───▶│ NRZI     │───▶│   CPU   │
│ pins    │    │ SM (sync)  │    │ Decoder  │    │ (bytes) │
└─────────┘    └────────────┘    └──────────┘    └─────────┘
                    │ IRQ              ▲
                    └──────────────────┘
                    (DECODER_TRIGGER)
```

## Timing Considerations

### Full-Speed (12 Mbps)
- Bit time: 83.3 ns
- TX runs at 48 MHz (20.8 ns per cycle, 4 cycles per bit)
- Edge detector runs at 96 MHz (10.4 ns per cycle, 8 samples per bit)

### Low-Speed (1.5 Mbps)
- Bit time: 666.7 ns
- TX runs at 6 MHz (166.7 ns per cycle, 4 cycles per bit)
- Edge detector runs at 12 MHz (83.3 ns per cycle, 8 samples per bit)

### Critical Timing

The edge detector and NRZI decoder communicate via PIO IRQ. Any delay in responding to `DECODER_TRIGGER` could cause:
- Missed bit transitions
- Incorrect bit decoding
- Data corruption (e.g., keycode appearing in modifier byte)

## Pin Configuration

The library supports two pinout options:
- `PIO_USB_PINOUT_DPDM`: D- = D+ + 1 (default)
- `PIO_USB_PINOUT_DMDP`: D- = D+ - 1

Both pins are configured with:
- Pull-down resistors
- Input inversion (simplifies edge detection logic)
- 12mA drive strength
- Fast slew rate

## References

- [Pico-PIO-USB GitHub](https://github.com/sekigon-gonnoc/Pico-PIO-USB)
- [USB 1.1 Specification](https://www.usb.org/document-library/usb-11-specification)
- [RP2040 Datasheet - PIO](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
