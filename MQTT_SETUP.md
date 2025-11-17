# MQTT Setup Guide

## Overview

The HID Proxy supports publishing MQTT messages for home automation integration. This includes automatic lock/unlock events and **custom MQTT publishes from keydefs** to trigger Home Assistant automations (lights, scenes, scripts, etc.).

## Features

- **Lock/Unlock Events**: Publishes device state changes to MQTT topics
- **Keydef MQTT Actions**: Trigger automations with `MQTT("topic", "message")` in macros
- **Status Reporting**: Online/offline status via Last Will and Testament (LWT)
- **Optional TLS**: Secure MQTT connections to local brokers
- **Unique Device IDs**: Uses last 2 hex digits of board ID for topic names
- **Non-blocking**: MQTT runs in background without affecting keyboard functionality

## Hardware Requirements

- **Raspberry Pi Pico W** (with CYW43 WiFi chip)
- **MQTT Broker** on local network (e.g., Mosquitto, Home Assistant built-in broker)

## Configuration

### Option 1: Build-Time Configuration via .env (Recommended)

Add MQTT settings to your `.env` file alongside WiFi credentials:

```bash
# WiFi Configuration
WIFI_SSID=Your-Network
WIFI_PASSWORD=Your-Password

# MQTT Configuration
MQTT_BROKER=192.168.1.100    # IP address or hostname of MQTT broker
MQTT_PORT=1883               # Port (1883 for plain, 8883 for TLS)
MQTT_USE_TLS=false           # Set to 'true' for TLS (optional)
MQTT_USERNAME=your_user      # Optional: MQTT username (if broker requires auth)
MQTT_PASSWORD=your_pass      # Optional: MQTT password (if broker requires auth)
```

**Note**:
- For local LAN brokers, TLS is optional. Set `MQTT_USE_TLS=true` only if your broker requires it.
- Username/password are optional - only needed if your broker has authentication enabled.

### Option 2: No MQTT (Optional)

If you don't want MQTT, simply omit the `MQTT_BROKER` setting from `.env`. The device will work normally with WiFi/HTTP only.

## MQTT Topics

The device publishes to topics with format: `hidproxy-XXXX/<topic>` where XXXX is the last 4 hex digits of your Pico's unique board ID (same as the mDNS hostname).

### Published Topics

| Topic | Values | QoS | Retained | Description |
|-------|--------|-----|----------|-------------|
| `hidproxy-XXXX/status` | `online`, `offline` | 1 | Yes | Device availability (LWT) |
| `hidproxy-XXXX/lock` | `locked`, `unlocked` | 1 | Yes | Device lock state |

**Finding your Board ID**: Check the serial console output when the device boots, or look at the mDNS hostname (e.g., `hidproxy-a3f4.local` → board ID is `a3f4`).

## Testing MQTT

### Using mosquitto_sub

```bash
# Subscribe to all topics for your device (replace XXXX with your board ID)
mosquitto_sub -h 192.168.1.100 -t "hidproxy-XXXX/#" -v

# You should see:
# hidproxy-a3f4/status online
# hidproxy-a3f4/lock locked
```

### Triggering Events

1. **Unlock the device** (double-shift + ENTER, type password, ENTER)
   ```
   hidproxy-a3f4/lock unlocked
   ```

2. **Lock the device** (double-shift + END)
   ```
   hidproxy-a3f4/lock locked
   ```

3. **Reboot the device** (will publish offline then online)
   ```
   hidproxy-a3f4/status offline
   hidproxy-a3f4/status online
   hidproxy-a3f4/lock locked
   ```

## Home Assistant Integration

### Manual MQTT Binary Sensor

Add to your Home Assistant `configuration.yaml`:

```yaml
mqtt:
  binary_sensor:
    - name: "HID Proxy Lock Status"
      unique_id: hidproxy_lock_a3f4  # Replace XXXX with your board ID
      state_topic: "hidproxy-a3f4/lock"  # Replace XXXX
      payload_on: "locked"
      payload_off: "unlocked"
      device_class: lock
      availability_topic: "hidproxy-a3f4/status"  # Replace XXXX
      payload_available: "online"
      payload_not_available: "offline"
```

After adding, reload the MQTT integration in Home Assistant.

### Automations

**Example: Notification when device unlocks**

```yaml
automation:
  - alias: "HID Proxy Unlocked Alert"
    trigger:
      - platform: state
        entity_id: binary_sensor.hid_proxy_lock_status
        to: "off"  # unlocked
    action:
      - service: notify.mobile_app
        data:
          message: "HID Proxy was unlocked"
```

**Example: Lock after midnight**

```yaml
automation:
  - alias: "Auto-lock HID Proxy at Midnight"
    trigger:
      - platform: time
        at: "00:00:00"
    condition:
      - condition: state
        entity_id: binary_sensor.hid_proxy_lock_status
        state: "off"  # unlocked
    action:
      - service: notify.persistent_notification
        data:
          message: "HID Proxy will lock in 5 minutes"
      # Note: Actual locking must be done physically or via HTTP POST /lock
```

## Using MQTT in Keydefs

You can trigger Home Assistant automations directly from keyboard macros using the `MQTT()` action.

### Syntax

```
[public|private] key { MQTT("topic", "message") }
```

### Examples

**Turn on bedroom light:**
```
[public] F5 { MQTT("hidproxy/light/bedroom", "ON") }
[public] F6 { MQTT("hidproxy/light/bedroom", "OFF") }
```

**Activate Home Assistant scene:**
```
[public] F7 { MQTT("homeassistant/scene/activate", "movie_time") }
[public] F8 { MQTT("homeassistant/scene/activate", "goodnight") }
```

**Combined keyboard + automation:**
```
[public] F9 { "Activating movie mode..." ENTER MQTT("homeassistant/scene", "movie") }
```

**Toggle switch:**
```
[public] F10 { MQTT("homeassistant/switch/living_room/set", "toggle") }
```

### Home Assistant Integration

To receive these custom MQTT messages in Home Assistant, create an automation:

```yaml
automation:
  - alias: "Bedroom Light via HID Proxy"
    trigger:
      - platform: mqtt
        topic: "hidproxy/light/bedroom"
    action:
      - service: light.turn_{{ trigger.payload | lower }}
        target:
          entity_id: light.bedroom
```

Or use MQTT switches/lights directly:

```yaml
light:
  - platform: mqtt
    name: "Bedroom Light"
    command_topic: "hidproxy/light/bedroom"
    payload_on: "ON"
    payload_off: "OFF"
```

### Notes

- MQTT actions require WiFi and MQTT broker to be configured
- If MQTT is not connected, the action is skipped with a warning log
- Topic and message strings are limited to 63 characters each
- MQTT actions work in both public and private keydefs
- Can mix MQTT with keyboard actions in the same macro

## Troubleshooting

### MQTT Not Connecting

Check the serial console for errors:

```bash
minicom -D /dev/ttyACM0 -b 115200
```

Look for:
```
WiFi connected! IP: 192.168.1.100
Initializing MQTT client for broker: 192.168.1.100
MQTT client ID: hidproxy-a3f4
Looking up MQTT broker: 192.168.1.100
MQTT broker resolved: 192.168.1.100
Connecting to MQTT broker (no TLS) on port 1883
MQTT connected to broker
```

### Common Issues

**1. "MQTT not configured (MQTT_BROKER not set)"**
- You didn't add `MQTT_BROKER` to `.env` file
- Solution: Add `MQTT_BROKER=<your-broker-ip>` and rebuild

**2. "MQTT DNS lookup failed"**
- Broker hostname doesn't resolve
- Solution: Use IP address instead of hostname in `MQTT_BROKER`

**3. "MQTT connection failed: X"**
- Broker not reachable or wrong port
- Solution: Check broker is running (`sudo systemctl status mosquitto`)
- Verify firewall allows port 1883

**4. No messages appearing**
- Check you're subscribing to the correct topic (with your board ID)
- Verify broker allows anonymous connections (or configure auth)

### TLS Issues

If using `MQTT_USE_TLS=true`:

**Certificate verification disabled** - The current implementation uses `altcp_tls_create_config_client(NULL, 0)` which doesn't verify the broker's certificate. This is acceptable for local LAN use but not recommended for internet-facing brokers.

For production use with certificate verification, you would need to:
1. Generate your broker's certificate
2. Add it to the code (similar to `https_cert.c`)
3. Modify `mqtt_client.c` to use `altcp_tls_create_config_client(cert, cert_len)`

## Security Considerations

### Local Network Only

- MQTT messages contain device state information (locked/unlocked)
- Use on trusted local network only
- Consider firewall rules to restrict broker access
- For internet access, use TLS with certificate verification

### Authentication

✅ **MQTT authentication is now supported!**

To use MQTT with username/password authentication, simply add to your `.env` file:

```bash
MQTT_USERNAME=your_username
MQTT_PASSWORD=your_password
```

The device will automatically use these credentials when connecting to the broker. If these are not defined, the device connects without authentication (anonymous mode).

### Passphrase Protection

The lock/unlock events don't expose your encryption passphrase or any macro contents - only the state changes.

## Advanced Configuration

### Custom MQTT Topics

To change the topic prefix, modify `mqtt_client.c`:

```c
// Current: hidproxy-XXXX
// Change to: mydevice-XXXX
snprintf(mqtt_state.topic_prefix, sizeof(mqtt_state.topic_prefix),
         "mydevice-%02x%02x", id.id[6], id.id[7]);
```

### QoS and Retention

Current settings (in `mqtt_client.c`):
- `MQTT_QOS = 1` (at least once delivery)
- `MQTT_RETAIN = 1` (retain last message)

Change these if needed for your broker setup.

## Performance

- **Non-blocking**: MQTT operates in lwIP background tasks
- **No keyboard latency**: Lock/unlock events published asynchronously
- **Automatic reconnect**: WiFi and MQTT reconnect automatically on failure
- **Minimal memory**: ~2KB RAM overhead for MQTT client state

## Future Enhancements

Possible future features (not yet implemented):

1. **MQTT Configuration**: Set macros via MQTT publish
2. **Keystroke Publishing**: Publish intercepted keystrokes to MQTT
3. **Remote Lock/Unlock**: Subscribe to commands from Home Assistant
4. **Certificate Support**: Full TLS with certificate verification
5. **MQTT Discovery**: Auto-configure Home Assistant via discovery protocol

## Example Workflow

```bash
# 1. Configure .env file
cat > .env << 'EOF'
WIFI_SSID=MyNetwork
WIFI_PASSWORD=MyPassword
MQTT_BROKER=192.168.1.100
MQTT_PORT=1883
EOF

# 2. Build and flash firmware
./build.sh --board pico_w

# 3. Flash to device (both-shifts+HOME to enter bootloader)
# Copy build/hid_proxy.uf2 to RPI-RP2 drive

# 4. Check serial console for board ID
minicom -D /dev/ttyACM0 -b 115200
# Look for: "mDNS responder started: hidproxy-a3f4.local"
# Your board ID is: a3f4

# 5. Test MQTT
mosquitto_sub -h 192.168.1.100 -t "hidproxy-a3f4/#" -v

# 6. Unlock device and watch MQTT messages
# (Press both shifts + ENTER, type password, ENTER on physical keyboard)
```

## See Also

- **WIFI_SETUP.md** - WiFi configuration and HTTP API
- **README.md** - General project documentation
- **CLAUDE.md** - Detailed architecture and code locations
