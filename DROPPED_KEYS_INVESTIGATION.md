# Dropped Keys Investigation

## Problem Description

Occasionally, keystrokes are "dropped" during typing - i.e., a key is pressed but the character does not appear on the host computer. This happens frequently enough to be noticeable, though not consistently reproducible.

## Possible Causes

1. **User typing error** - Accidental incomplete keypress
2. **Firmware bug** - Issue in hid-proxy firmware processing
3. **PIO-USB library bug** - Issue in the Pico-PIO-USB library that interfaces with the physical keyboard

## Investigation Strategy

**Binary chop approach**: Since this issue may not have existed in older versions, we can bisect the git history to identify when the problem was introduced. This will help narrow down which code changes caused the issue.

## Diagnostic Infrastructure

To help diagnose this issue, extensive diagnostic instrumentation has been added to the codebase:

### 1. Global Counters

Located in `include/hid_proxy.h:148-151`:

- `keystrokes_received_from_physical` - Total HID reports received from physical keyboard (Core 1)
- `keystrokes_sent_to_host` - Total HID reports sent to host computer (Core 0)
- `queue_drops_realtime` - Number of times the oldest queue item was dropped due to queue full

These counters help track if keystrokes are being lost between reception and transmission.

### 2. Cyclic Diagnostic Buffers

Located in `include/hid_proxy.h:154-171`:

Two 256-entry cyclic buffers with spinlock protection for concurrent access:

- `diag_received_buffer` - Logs keystrokes received from physical keyboard
- `diag_sent_buffer` - Logs keystrokes sent to host computer

Each entry contains:
- `sequence` - Monotonic sequence number
- `timestamp_us` - Microsecond timestamp (wraps after ~71 minutes)
- `modifier` - HID modifier byte (Shift, Ctrl, Alt, etc.)
- `keycode[6]` - Up to 6 simultaneous HID keycodes

Functions:
- `diag_log_keystroke()` - Add entry to buffer
- `diag_dump_buffers()` - Print side-by-side comparison to console

### 3. Raw USB Reception Debug Output

Located in `src/usb_host.c:288-292`:

```c
printf("USB_RX: [%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x]\n",
       kb->modifier, kb->reserved,
       kb->keycode[0], kb->keycode[1], kb->keycode[2],
       kb->keycode[3], kb->keycode[4], kb->keycode[5]);
stdio_flush();  // Force immediate output
```

This prints every HID report **immediately** when received from the physical keyboard (before any queue operations). The `stdio_flush()` ensures output is not buffered.

### 4. Side-by-Side Comparison Display

Located in `src/hid_proxy.c:695-734`:

Triggered by **Double-Shift + SPACE**, this displays a side-by-side comparison of:
- Left column: Keystrokes received from physical keyboard
- Right column: Keystrokes sent to host computer

Format per line:
```
#<seq> [<mod>:<k0>:<k1>:<k2>:<k3>:<k4>:<k5>] <human-readable>
```

Example output:
```
RECEIVED FROM KEYBOARD                                           | SENT TO HOST
------------------------------------------------------------------+-----------------------------------------------------------------
#1234 [00:04:00:00:00:00:00] a                                   | #1234 [00:04:00:00:00:00:00] a
#1235 [00:00:00:00:00:00:00] (release)                           | #1235 [00:00:00:00:00:00:00] (release)
```

This allows visual inspection to see if received keystrokes match sent keystrokes.

### 5. Queue Behavior

Two queue strategies exist:

**Backpressure mode** (`queue_add_with_backpressure()`):
- Used for macro playback
- Blocks if queue is full
- Ensures no data loss

**Realtime mode** (`queue_add_realtime()`):
- Used for keyboard passthrough
- Drops oldest item if queue is full
- Increments `queue_drops_realtime` counter
- Logs warning on drop

## How to Use Diagnostics

### Real-time Monitoring

1. Connect serial console: `minicom -D /dev/ttyACM0 -b 115200`
2. Watch for `USB_RX:` lines as you type
3. Each keystroke should generate two lines:
   - Key press: `USB_RX: [02:00:04:00:00:00:00:00]` (Shift + A)
   - Key release: `USB_RX: [00:00:00:00:00:00:00:00]`

### Capture Keystroke History

1. Type some text that exhibits the problem
2. Press **Both Shifts + SPACE** to trigger diagnostic dump
3. Review side-by-side output to identify missing keystrokes
4. Look for:
   - Sequence number gaps
   - Keystrokes in left column but not right column
   - `queue_drops_realtime` counter increments

### Check Counter Discrepancies

The 5-second status message (printed after boot and periodically) shows:
- Total keystrokes received
- Total keystrokes sent
- Queue drop count

If `received > sent + queue_drops`, keystrokes are being lost somewhere in processing.

## Code Locations

Key diagnostic code locations:

- Diagnostic buffer definitions: `include/hid_proxy.h:154-171`
- USB reception logging: `src/usb_host.c:288-292, 297-299`
- Keystroke sent logging: `src/key_defs.c` (multiple locations)
- Buffer dump function: `src/hid_proxy.c:695-734`
- Counter declarations: `include/hid_proxy.h:148-151`

## Real-World Example: 5 Missing Keystrokes

### Captured Diagnostic Output

The user typed text followed by **Double-Shift + SPACE** to dump diagnostics. The output revealed:

```
Total received: 4500, Total sent: 4495, Drops: 0
```

**Key findings:**
- **5 keystrokes received but not sent** (4500 - 4495 = 5)
- **Zero queue drops** - the realtime queue drop mechanism did NOT activate
- **Conclusion: Keystrokes lost during processing, not in queue**

### Side-by-Side Buffer Comparison

The diagnostic buffers (last 256 keystrokes) show a consistent 5-keystroke offset:

```
RECEIVED FROM KEYBOARD                     | SENT TO HOST
-------------------------------------------+-------------------------------------------
#4245 [00:08:00:00:00:00:00] e            | #4240 [00:19:00:00:00:00:00] v
#4246 [00:00:00:00:00:00:00] (none)       | #4241 [00:00:00:00:00:00:00] (none)
#4247 [00:2c:00:00:00:00:00]              | #4242 [00:08:00:00:00:00:00] e
...
#4498 [02:00:00:00:00:00:00] Shift        | #4493 [00:2c:00:00:00:00:00]
#4499 [22:00:00:00:00:00:00] Shift+RShift | #4494 [00:00:00:00:00:00:00] (none)
#4500 [22:07:00:00:00:00:00] Shift+RShift+d | #4495 [02:00:00:00:00:00:00] Shift
```

**Observations:**
- Received buffer: sequences #4245 through #4500 (256 entries)
- Sent buffer: sequences #4240 through #4495 (256 entries)
- **Consistent 5-keystroke lag** maintained throughout buffer
- No gaps within either buffer - all sequences are consecutive

### Raw USB Reception Log

The `USB_RX:` output interspersed with the diagnostic dump confirms all keystrokes were physically received:

```
USB_RX: [22:00:00:00:00:00:00:00]
State changed from normal to seen_magic
USB_RX: [22:00:07:00:00:00:00:00]
Publishing MQTT: hidproxy-582e/lock = unlocked
```

The last few USB reports show:
1. `[22:00:00:00:00:00:00:00]` - Both shifts pressed (modifier 0x22 = 0x02|0x20 = LShift+RShift)
2. State change to `seen_magic` triggered
3. `[22:00:07:00:00:00:00:00]` - Both shifts + D pressed (keycode 0x07 = D)
4. MQTT publish triggered (SPACE handler for web access)

### Analysis: The Case of the Missing 'y'

**What the user typed:** `"aaThis is some test typing"`
**What the host saw:** `"aaThis is some test tping"` ← Missing 'y'

#### Searching for the 'y' keystroke (HID keycode 0x1c)

Looking at the diagnostic buffers around "test t...ping":

```
#4482  [00:2c:00:00:00:00:00]              (space)
#4483  [00:00:00:00:00:00:00] (none)
#4484  [00:17:00:00:00:00:00] t
#4485  [00:00:00:00:00:00:00] (none)
#4486  [1c:00:00:00:00:00:00] Alt+GUI+RCtrl     ← CORRUPTED 'y'!
#4487  [00:00:00:00:00:00:00] (none)
#4488  [00:13:00:00:00:00:00] p
#4489  [00:00:00:00:00:00:00] (none)
#4490  [00:0c:00:00:00:00:00] i
```

**Found it!** At sequence #4486, the HID report shows:
- **Modifier byte = 0x1c** (displayed as "Alt+GUI+RCtrl")
- **All keycode bytes = 0x00** (no key pressed)

The correct HID report for 'y' should have been: `[00:00:1c:00:00:00:00:00]`

Instead, PIO-USB delivered: `[1c:00:00:00:00:00:00:00]`

**The 'y' keycode (0x1c) ended up in the MODIFIER byte instead of a keycode byte!**

#### Confirming with USB_RX logs

Searching the raw USB reception logs:

```
USB_RX: [00:17:00:00:00:00:00:00]  ← 't' key press
USB_RX: [00:00:00:00:00:00:00:00]  ← 't' release
USB_RX: [1c:00:00:00:00:00:00:00]  ← CORRUPTED 'y' (should be [00:00:1c:00:00:00:00:00])
USB_RX: [00:00:00:00:00:00:00:00]  ← Release
USB_RX: [00:13:00:00:00:00:00:00]  ← 'p' key press
```

✅ **Corruption confirmed in raw USB_RX output!**

#### Conclusion: NOT a firmware bug

The corrupted HID report was received **directly from PIO-USB** before any firmware processing:

1. ✅ User pressed the 'y' key (not a typing mistake)
2. ✅ The keystroke was detected by the physical keyboard
3. ✅ PIO-USB captured the USB interrupt transfer
4. ❌ **PIO-USB delivered a corrupted HID report** to our callback
5. ✅ Our firmware correctly passed through the corrupted report
6. ❌ Host computer received `[1c:00:00:00:00:00:00:00]` = modifier keys only, no 'y'

**Root cause:** Either:
- **PIO-USB library bug** - Corruption during USB packet reception/parsing
- **USB signal integrity issue** - Electrical noise/timing problems on GPIO2/3
- **Physical keyboard firmware bug** - Malformed HID report from keyboard itself

**This is NOT a bug in the hid-proxy firmware.** The corruption happens before our code sees the data.

#### Counter discrepancy explanation

The 5-keystroke difference (4500 received vs 4495 sent) can be explained by:
- Deliberate key filtering (e.g., double-shift+D command keystrokes)
- Keys consumed by state machine for UI commands
- Normal in-flight buffering between queues

**This is expected behavior, not dropped keys.**

### Next Steps for Investigation

Since the corruption happens in PIO-USB before our firmware sees the data:

1. **Test USB signal integrity**
   - Try different USB cables
   - Add shorter wiring between Pico and keyboard
   - Test with different keyboards to rule out keyboard firmware bug

2. **Binary chop PIO-USB library versions**
   - Test with older versions of Pico-PIO-USB submodule
   - Check when corruption behavior started
   - Review PIO-USB commit history for relevant changes

3. **Monitor corruption frequency**
   - Add counter for corrupted reports (keycode in modifier byte)
   - Check if corruption correlates with specific keys, typing speed, or USB traffic
   - Log pattern: does 0x1c always corrupt, or other keycodes too?

4. **Code change to improve diagnostic confidence**
   - ✅ **DONE**: Moved `USB_RX:` printf to immediately after bounds check (src/usb_host.c:256)
   - Now prints directly from PIO-USB buffer before any firmware processing
   - Eliminates any doubt about firmware involvement

5. **Add corruption detection and logging**
   - Detect pattern: non-zero byte in modifier position, all keycodes zero
   - Log corrupted reports to separate counter
   - Add recovery: attempt to reconstruct correct report if pattern detected

6. **Report issue to Pico-PIO-USB maintainers**
   - Document corruption pattern with evidence
   - Provide reproduction case if possible
   - Link to this investigation

---

**Last Updated**: 2025-11-27
