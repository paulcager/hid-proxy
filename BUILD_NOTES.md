# Build Notes

## Building for Different Hardware

This project supports both **Raspberry Pi Pico** (regular) and **Raspberry Pi Pico W** (with WiFi).

### Automatic Detection

The build system automatically detects which board you're targeting and includes/excludes WiFi features accordingly:

- **Pico W**: Full WiFi/HTTP support enabled
- **Regular Pico**: WiFi code excluded, all other features work

### Build Commands

**For Pico W:**
```bash
mkdir build && cd build
cmake -DPICO_BOARD=pico_w ..
make
```

**For Regular Pico:**
```bash
mkdir build && cd build
cmake -DPICO_BOARD=pico ..
make
```

**Auto-detect (uses Pico W if available):**
```bash
mkdir build && cd build
cmake ..
make
```

### What Gets Included

| Feature | Regular Pico | Pico W |
|---------|-------------|--------|
| USB HID proxy | ✅ | ✅ |
| Text expansion/macros | ✅ | ✅ |
| Encrypted flash storage | ✅ | ✅ |
| Passphrase unlock | ✅ | ✅ |
| NFC authentication | ✅ | ✅ |
| Interactive macro define | ✅ | ✅ |
| WiFi connectivity | ❌ | ✅ |
| HTTP API (GET/POST macros) | ❌ | ✅ |
| mDNS (hidproxy.local) | ❌ | ✅ |
| Physical web unlock | ❌ | ✅ |

### How It Works

The code uses `#ifdef PICO_CYW43_SUPPORTED` to conditionally compile WiFi features:

```c
#ifdef PICO_CYW43_SUPPORTED
    wifi_config_init();
    wifi_init();
#else
    LOG_INFO("WiFi not supported on this hardware\n");
#endif
```

When building for regular Pico:
- WiFi source files (`wifi_config.c`, `http_server.c`) are not compiled
- WiFi libraries (`pico_cyw43_arch`, `pico_lwip_http`) are not linked
- WiFi headers are not included
- Both-shifts+HOME does nothing (harmless no-op)

### Verification

Check the build output to confirm which board was detected:

```
-- PICO_BOARD: pico_w
-- PICO_CYW43_SUPPORTED: 1    <- WiFi enabled
```

or

```
-- PICO_BOARD: pico
-- PICO_CYW43_SUPPORTED: 0    <- WiFi disabled
```

### Switching Boards

To switch from one board to another, delete the build directory and reconfigure:

```bash
rm -rf build
mkdir build && cd build
cmake -DPICO_BOARD=pico_w ..  # or pico
make
```

### Binary Size

Approximate flash usage:
- **Regular Pico build**: ~95KB
- **Pico W build**: ~180KB (includes lwIP stack, HTTP server, WiFi drivers)

Both fit comfortably in the 2MB flash with plenty of room for macros.
