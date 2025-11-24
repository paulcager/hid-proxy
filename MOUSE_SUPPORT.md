# Mouse Support Documentation

## Current Implementation Status

### ✅ Implemented: Mouse Passthrough
- **Status**: Fully functional (as of commit 1aa3b25)
- **Capability**: Physical mouse reports are forwarded to host computer
- **Use Case**: Combined keyboard/mouse devices (e.g., keyboard with touchpad)
- **Technical Details**:
  - Supports up to 4 HID interfaces (`CFG_TUH_HID = 4`)
  - Each interface (keyboard, mouse) handled independently
  - Mouse uses realtime queue (non-blocking, never interferes with keyboard)
  - Works with multi-interface devices presenting separate keyboard + mouse endpoints

**Example Device Support:**
```
[VID:PID][dev_addr] HID Interface0, Protocol = Keyboard
[VID:PID][dev_addr] HID Interface1, Protocol = Mouse
```
Both interfaces forward correctly to host.

### ⏸️ Not Yet Implemented: Mouse Macros
- **Status**: Planned feature, not yet implemented
- **Capability**: Include mouse movements/clicks in key definitions
- **Blocked By**: Need to extend keydef storage format to support mixed HID report types

---

## USB HID Mouse Technical Reference

### Boot Protocol Report Format

USB HID mice using boot protocol send reports with this structure:

```c
typedef struct {
    uint8_t buttons;  // Bit 0=Left, 1=Right, 2=Middle, 3-7=Extra buttons
    int8_t x;         // Signed 8-bit: -128 to +127 (relative movement)
    int8_t y;         // Signed 8-bit: -128 to +127 (relative movement)
    int8_t wheel;     // Signed 8-bit: scroll wheel delta (optional)
} hid_mouse_report_t;
```

**Key Properties:**
- **Relative Movement**: X/Y are deltas from current position, NOT absolute coordinates
- **Range**: -128 to +127 per report
- **Units**: "Mickeys" (approximate mouse movement units, ~1/200th inch on mousepad)
- **Frequency**: Typically 125 Hz (8ms) or 1000 Hz (1ms) polling rate

### Button Bits
```
Bit 0: Left button   (0x01)
Bit 1: Right button  (0x02)
Bit 2: Middle button (0x04)
Bit 3-7: Extra buttons (device-specific)
```

---

## Mouse Macro Implementation Considerations

### Why Relative Movement is Challenging

**The Fundamental Problem:**
USB mice report **relative movement** (deltas), not absolute cursor position. The HID device has **no knowledge** of:
- Current cursor position on screen
- Screen resolution or DPI
- Multi-monitor setup
- OS mouse sensitivity settings
- Application-specific mouse capture

### Pixel-to-Mickey Conversion

The relationship between mouse movement and screen pixels is **non-deterministic**:

| Setting | Mickeys → Pixels |
|---------|------------------|
| Windows Default (6/11) | ~1 mickey = 1 pixel |
| Windows Fast (11/11) | ~1 mickey = 3.5 pixels |
| Windows Slow (1/11) | ~1 mickey = 0.25 pixels |
| Windows "Enhance Pointer Precision" | **Non-linear acceleration curve** |
| Linux (libinput) | Configurable, varies by DE |
| macOS | Different acceleration algorithm |

**Result:** A macro that moves "100 pixels right" on one system might move 25 or 350 pixels on another.

---

## Cursor Positioning Strategies

### Strategy 1: Corner Homing ✅ Feasible

**Concept:** Move to screen edge as reference point, then offset from there.

**Example:**
```
[private] m {
    # Home to top-left corner (overshoot guarantees arrival)
    MOUSE_MOVE(-127, -127)
    MOUSE_MOVE(-127, -127)
    MOUSE_MOVE(-127, -127)  # 3x = ~2400 virtual pixels

    # Now at (0, 0) regardless of starting position
    MOUSE_MOVE(50, 30)  # Offset to approximate menu position
    MOUSE_CLICK(LEFT)
}
```

**Pros:**
- Guarantees known starting position
- OS clamps cursor at screen edges
- Works across resolutions (relatively)

**Cons:**
- Still resolution-dependent for offsets
- DPI/sensitivity variations affect final position
- Multi-monitor setups complicate edge behavior
- Visually disruptive (cursor flies to corner)

### Strategy 2: Small Relative Movements ✅ Reliable

**Concept:** Use mouse for relative adjustments, not absolute positioning.

**Example:**
```
[private] scroll_up {
    MOUSE_WHEEL(5)  # Scroll up 5 notches
}

[private] nudge_right {
    MOUSE_MOVE(10, 0)  # Move slightly right
}

[private] context_menu {
    MOUSE_CLICK(RIGHT)  # Right-click at current position
}
```

**Pros:**
- Works reliably across all systems
- DPI-independent (relative is relative)
- Intuitive behavior
- No visual disruption

**Cons:**
- Can't position cursor precisely
- Limited to context-relative actions

### Strategy 3: Keyboard Navigation ✅ Recommended Alternative

For precise UI automation, **keyboard shortcuts are superior**:

```
# Instead of clicking File → Open
[private] open_file {
    ^O  # Ctrl+O (universal shortcut)
}

# Instead of navigating menus with mouse
[private] file_menu {
    ALT F  # Alt+F opens File menu
    DOWN DOWN  # Navigate down 2 items
    ENTER  # Activate
}
```

**Pros:**
- 100% reliable across systems
- Fast execution
- Accessible (works without mouse)
- No resolution/DPI dependencies

**Cons:**
- Requires knowing shortcuts
- Application-specific

---

## Mouse Acceleration: The Hidden Complexity

### What is Mouse Acceleration?

Most operating systems apply **non-linear scaling** based on movement speed:
- **Slow movement**: 1 mickey = 1 pixel (precision)
- **Fast movement**: 1 mickey = 3+ pixels (speed)

**Implications for Macros:**
```c
// These are NOT equivalent:
MOUSE_MOVE(100, 0)  // Fast movement → acceleration applied

vs.

MOUSE_MOVE(10, 0)   // Repeated 10 times
MOUSE_MOVE(10, 0)   // Slow movement → no acceleration
// ... (10 total)
```

The first might move 300 pixels, the second only 100 pixels!

### OS-Specific Acceleration

| OS | Acceleration Curve |
|----|-------------------|
| Windows | Configurable (off, linear, or "Enhance Pointer Precision") |
| Linux | libinput: flat, adaptive, or custom |
| macOS | Always enabled, non-configurable |

**Result:** Same macro behaves differently on each OS.

---

## Implementation Roadmap

### Phase 1: Extended Keydef Format ⏸️ Future

Modify `keydef_t` to support mixed action types:

```c
typedef enum {
    ACTION_KEYBOARD,
    ACTION_MOUSE_MOVE,
    ACTION_MOUSE_BUTTON,
    ACTION_MOUSE_WHEEL,
    ACTION_DELAY
} action_type_t;

typedef struct keydef {
    uint8_t trigger;
    uint16_t count;
    bool require_unlock;
    struct {
        action_type_t type;
        union {
            hid_keyboard_report_t keyboard;
            struct { int8_t x, y; } mouse_move;
            struct { uint8_t buttons; bool press; } mouse_button;
            int8_t wheel;
            uint16_t delay_ms;
        } data;
    } actions[0];
} keydef_t;
```

**Storage Impact:**
- Current: 8 bytes per keyboard report
- Proposed: ~12 bytes per action (includes type tag)
- Max macro: 64 actions = 768 bytes (fits in kvstore)

### Phase 2: Macro Parser Extensions ⏸️ Future

Add syntax for mouse actions:

```
[private] example {
    "Text typing"
    MOUSE_MOVE(10, -5)        # Move right 10, up 5
    MOUSE_CLICK(LEFT)          # Click left button
    MOUSE_CLICK(RIGHT)         # Click right button
    MOUSE_DOWN(LEFT)           # Press and hold
    DELAY(100)
    MOUSE_UP(LEFT)             # Release
    MOUSE_WHEEL(3)             # Scroll up 3 notches
    MOUSE_WHEEL(-3)            # Scroll down 3 notches
}
```

### Phase 3: Documentation & Examples ⏸️ Future

**User-facing documentation should include:**

1. **Clear limitations:**
   - "Mouse positioning is approximate and system-dependent"
   - "Calibration required for each system/resolution"
   - "Not suitable for precise UI automation"

2. **Recommended use cases:**
   - ✅ Scrolling (MOUSE_WHEEL)
   - ✅ Context menus (MOUSE_CLICK(RIGHT))
   - ✅ Small adjustments (MOUSE_MOVE with small values)
   - ❌ Clicking specific UI elements (use keyboard shortcuts instead)

3. **Corner homing template:**
   ```
   # Template for positioning macros
   [private] click_top_left_menu {
       # Home to corner
       MOUSE_MOVE(-127, -127)
       MOUSE_MOVE(-127, -127)
       MOUSE_MOVE(-127, -127)

       # Adjust offset (CALIBRATE FOR YOUR SYSTEM)
       MOUSE_MOVE(X, Y)  # Replace X, Y with your values
       MOUSE_CLICK(LEFT)
   }
   ```

---

## Alternative: Absolute Position HID Digitizer

For **true** absolute positioning, a different HID report descriptor is needed:

### Digitizer Report Format
```c
typedef struct {
    uint8_t tip_switch;   // Touch/pen contact
    uint16_t x;           // 0-65535 (maps to full screen width)
    uint16_t y;           // 0-65535 (maps to full screen height)
} hid_digitizer_report_t;
```

**Pros:**
- True absolute positioning
- Resolution-independent (0-65535 always spans full screen)
- No acceleration

**Cons:**
- Requires different USB descriptor
- OS treats as touchscreen/pen, not mouse
- Can't be sent from keyboard HID interface
- Would need separate USB composite device

**Verdict:** Not worth the complexity for this project.

---

## Recommendations

### For Current Users
1. **Passthrough works great** - combined keyboard/mouse devices fully supported
2. **Use keyboard shortcuts** for automation instead of mouse positioning
3. **Wait for mouse macro support** if you need mouse automation

### For Future Implementation
1. **Start with simple relative movements** (MOUSE_WHEEL, small MOUSE_MOVE)
2. **Document corner homing strategy** but warn about limitations
3. **Provide calibration tools** (helper to determine X/Y offsets)
4. **Focus on keyboard navigation** as primary automation method

### For Developers
If implementing mouse macros:
- Test across Windows/Linux/macOS with different DPI settings
- Measure actual pixel movement vs mickey values
- Provide calibration mode (shows current position, logs movements)
- Consider adding `MOUSE_HOME_CORNER()` helper command
- Document that macros are **not portable** across systems

---

## See Also
- **USB HID Specification 1.11**: https://www.usb.org/sites/default/files/documents/hid1_11.pdf
- **HID Usage Tables**: https://usb.org/sites/default/files/hut1_2.pdf
- **README.md**: Current command reference
- **CLAUDE.md**: Architecture and code locations

## Revision History
- 2025-01-17: Initial documentation (mouse passthrough working, macros planned)
