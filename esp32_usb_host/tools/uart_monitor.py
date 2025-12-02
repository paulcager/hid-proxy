#!/usr/bin/env python3
"""
Simple UART monitor for ESP32 HID proxy protocol

Listens on UART and decodes packets, displaying keyboard/mouse reports
Usage: ./uart_monitor.py /dev/ttyUSB1 921600
"""

import sys
import serial
import struct

# Packet types
PKT_KEYBOARD_REPORT = 0x01
PKT_MOUSE_REPORT = 0x02
PKT_LED_UPDATE = 0x03
PKT_STATUS = 0x04
PKT_ACK = 0x05

PACKET_START = 0xAA

def verify_checksum(packet_bytes):
    """Verify XOR checksum"""
    calc_checksum = 0
    for byte in packet_bytes[:-1]:  # All except last byte
        calc_checksum ^= byte
    recv_checksum = packet_bytes[-1]
    return calc_checksum == recv_checksum

def decode_keyboard_report(payload):
    """Decode HID keyboard report (8 bytes)"""
    if len(payload) != 8:
        return f"Invalid keyboard report length: {len(payload)}"

    modifier = payload[0]
    # payload[1] is reserved
    keys = payload[2:8]

    mod_names = []
    if modifier & 0x01: mod_names.append("LCTRL")
    if modifier & 0x02: mod_names.append("LSHIFT")
    if modifier & 0x04: mod_names.append("LALT")
    if modifier & 0x08: mod_names.append("LGUI")
    if modifier & 0x10: mod_names.append("RCTRL")
    if modifier & 0x20: mod_names.append("RSHIFT")
    if modifier & 0x40: mod_names.append("RALT")
    if modifier & 0x80: mod_names.append("RGUI")

    mod_str = "+".join(mod_names) if mod_names else "none"
    key_codes = " ".join([f"0x{k:02X}" for k in keys if k != 0])

    return f"KEYBOARD: mod={mod_str}, keys=[{key_codes if key_codes else 'none'}]"

def decode_mouse_report(payload):
    """Decode HID mouse report (min 3 bytes)"""
    if len(payload) < 3:
        return f"Invalid mouse report length: {len(payload)}"

    buttons = payload[0]
    x = struct.unpack('b', bytes([payload[1]]))[0]  # Signed byte
    y = struct.unpack('b', bytes([payload[2]]))[0]

    btn_names = []
    if buttons & 0x01: btn_names.append("LEFT")
    if buttons & 0x02: btn_names.append("RIGHT")
    if buttons & 0x04: btn_names.append("MIDDLE")

    btn_str = "+".join(btn_names) if btn_names else "none"

    return f"MOUSE: buttons={btn_str}, x={x:+4d}, y={y:+4d}"

def decode_packet(pkt_type, payload):
    """Decode packet based on type"""
    if pkt_type == PKT_KEYBOARD_REPORT:
        return decode_keyboard_report(payload)
    elif pkt_type == PKT_MOUSE_REPORT:
        return decode_mouse_report(payload)
    elif pkt_STATUS:
        try:
            return f"STATUS: {payload.decode('utf-8')}"
        except:
            return f"STATUS: {payload.hex()}"
    elif pkt_type == PKT_ACK:
        return "ACK"
    else:
        return f"Unknown type 0x{pkt_type:02X}: {payload.hex()}"

def main():
    if len(sys.argv) < 2:
        print("Usage: uart_monitor.py <serial_port> [baud_rate]")
        print("Example: uart_monitor.py /dev/ttyUSB1 921600")
        sys.exit(1)

    port = sys.argv[1]
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 921600

    print(f"Opening {port} at {baud} baud...")
    ser = serial.Serial(port, baud, timeout=1)
    print("Listening for packets (Ctrl+C to exit)...")

    try:
        while True:
            # Wait for start byte
            while True:
                byte = ser.read(1)
                if not byte:
                    continue
                if byte[0] == PACKET_START:
                    break

            # Read header (type + length)
            header = ser.read(3)
            if len(header) != 3:
                print("ERROR: Failed to read header")
                continue

            pkt_type = header[0]
            pkt_len = struct.unpack('<H', header[1:3])[0]  # Little-endian uint16

            # Read payload + checksum
            data = ser.read(pkt_len + 1)
            if len(data) != pkt_len + 1:
                print(f"ERROR: Failed to read payload (expected {pkt_len+1}, got {len(data)})")
                continue

            payload = data[:-1]
            checksum = data[-1]

            # Verify checksum
            packet_bytes = bytes([PACKET_START, pkt_type]) + header[1:3] + data
            if not verify_checksum(packet_bytes):
                print("ERROR: Checksum mismatch")
                continue

            # Decode and print
            decoded = decode_packet(pkt_type, payload)
            print(f"{decoded}")

    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
