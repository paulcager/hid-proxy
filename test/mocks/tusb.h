// Mock tusb.h for host testing
#ifndef TUSB_H
#define TUSB_H

#include <stdint.h>
#include <string.h>

// HID keyboard report structure
typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keycode[6];
} hid_keyboard_report_t;

// HID mouse report structure (minimal mock)
typedef struct {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
    int8_t pan;
} hid_mouse_report_t;

// HID gamepad report structure (minimal mock)
typedef struct {
    int8_t x;
    int8_t y;
    int8_t z;
    int8_t rz;
    int8_t rx;
    int8_t ry;
    uint8_t hat;
    uint32_t buttons;
} hid_gamepad_report_t;

#endif // TUSB_H
