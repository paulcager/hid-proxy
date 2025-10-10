#ifndef HID_PROXY_WIFI_CONFIG_H
#define HID_PROXY_WIFI_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/time.h"

// WiFi configuration stored in separate flash sector from encrypted keydefs
#define WIFI_CONFIG_MAGIC "hidwifi1"
#define WIFI_CONFIG_SIZE 4096  // One flash sector

typedef struct {
    char magic[8];
    char ssid[32];
    char password[64];
    bool enable_wifi;
    uint8_t reserved[4096 - sizeof(char[8]) - sizeof(char[32]) - sizeof(char[64]) - sizeof(bool)];  // Pad to full sector
} wifi_config_t;

// Web access state (in RAM, not persisted)
typedef struct {
    bool web_access_enabled;
    absolute_time_t web_access_expires;
} web_state_t;

extern web_state_t web_state;

// WiFi configuration functions
void wifi_config_init(void);
void wifi_config_load(wifi_config_t *config);
void wifi_config_save(const wifi_config_t *config);
bool wifi_config_is_valid(const wifi_config_t *config);

// WiFi connection functions
void wifi_init(void);
void wifi_task(void);
bool wifi_is_connected(void);

// Web access control
void web_access_enable(void);
void web_access_disable(void);
bool web_access_is_enabled(void);

#endif // HID_PROXY_WIFI_CONFIG_H
