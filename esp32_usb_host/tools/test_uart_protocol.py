#!/usr/bin/env python3
"""
Test UART protocol by sending test packets

Useful for verifying the UART receiver on ESP32-S3 #2 before USB device is ready
Usage: ./test_uart_protocol.py /dev/ttyUSB1 921600
"""

import sys
import serial
import struct
import time

# Packet types
PKT_KEYBOARD_REPORT = 0x01
PKT_MOUSE_REPORT = 0x02
PKT_LED_UPDATE = 0x03
PKT_STATUS = 0x04
PKT_ACK = 0x05

PACKET_START = 0xAA

def create_packet(pkt_type, payload):
    """Create a UART packet with framing and checksum"""
    if isinstance(payload, str):
        payload = payload.encode('utf-8')

    # Build packet
    packet = bytearray()
    packet.append(PACKET_START)
    packet.append(pkt_type)
    packet.extend(struct.pack('<H', len(payload)))  # Little-endian length
    packet.extend(payload)

    # Calculate XOR checksum
    checksum = 0
    for byte in packet:
        checksum ^= byte

    packet.append(checksum)
    return bytes(packet)

def send_keyboard_report(ser, modifier=0, keys=[0, 0, 0, 0, 0, 0]):
    """Send a keyboard HID report (8 bytes)"""
    payload = bytes([modifier, 0] + keys)  # modifier, reserved, key0-5
    packet = create_packet(PKT_KEYBOARD_REPORT, payload)
    ser.write(packet)
    print(f"Sent keyboard: mod=0x{modifier:02X}, keys={[f'0x{k:02X}' for k in keys]}")

def send_mouse_report(ser, buttons=0, x=0, y=0):
    """Send a mouse HID report (3 bytes minimum)"""
    payload = bytes([buttons, x & 0xFF, y & 0xFF])
    packet = create_packet(PKT_MOUSE_REPORT, payload)
    ser.write(packet)
    print(f"Sent mouse: buttons=0x{buttons:02X}, x={x:+4d}, y={y:+4d}")

def send_status(ser, message):
    """Send a status message"""
    packet = create_packet(PKT_STATUS, message)
    ser.write(packet)
    print(f"Sent status: {message}")

def main():
    if len(sys.argv) < 2:
        print("Usage: test_uart_protocol.py <serial_port> [baud_rate]")
        print("Example: test_uart_protocol.py /dev/ttyUSB1 921600")
        sys.exit(1)

    port = sys.argv[1]
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 921600

    print(f"Opening {port} at {baud} baud...")
    ser = serial.Serial(port, baud, timeout=1)
    time.sleep(0.5)  # Let port stabilize

    print("\nSending test packets...\n")

    # Test 1: Status message
    send_status(ser, "UART Protocol Test Started")
    time.sleep(0.5)

    # Test 2: Simulate typing 'a'
    print("\n--- Simulating 'a' key press ---")
    send_keyboard_report(ser, modifier=0, keys=[0x04, 0, 0, 0, 0, 0])  # 'a' pressed
    time.sleep(0.1)
    send_keyboard_report(ser, modifier=0, keys=[0, 0, 0, 0, 0, 0])     # All released
    time.sleep(0.5)

    # Test 3: Simulate Shift+A
    print("\n--- Simulating Shift+A ---")
    send_keyboard_report(ser, modifier=0x02, keys=[0x04, 0, 0, 0, 0, 0])  # Shift+'a'
    time.sleep(0.1)
    send_keyboard_report(ser, modifier=0, keys=[0, 0, 0, 0, 0, 0])        # All released
    time.sleep(0.5)

    # Test 4: Simulate mouse movement
    print("\n--- Simulating mouse movement ---")
    send_mouse_report(ser, buttons=0, x=10, y=-5)   # Move right/up
    time.sleep(0.1)
    send_mouse_report(ser, buttons=0x01, x=0, y=0)  # Left click
    time.sleep(0.1)
    send_mouse_report(ser, buttons=0, x=0, y=0)     # Release
    time.sleep(0.5)

    # Test 5: Rapid keystroke simulation
    print("\n--- Simulating rapid typing (10 keys/sec for 3 seconds) ---")
    for i in range(30):
        key = 0x04 + (i % 26)  # Cycle through a-z
        send_keyboard_report(ser, modifier=0, keys=[key, 0, 0, 0, 0, 0])
        time.sleep(0.05)
        send_keyboard_report(ser, modifier=0, keys=[0, 0, 0, 0, 0, 0])
        time.sleep(0.05)

    print("\n--- Test complete ---")
    send_status(ser, "UART Protocol Test Complete")

    ser.close()

if __name__ == "__main__":
    main()
