#include "wifi_config.h"
#include "hid_proxy.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"  // For watchdog_reboot
#include "hardware/flash.h"
#include "lwip/apps/mdns.h"
#include <string.h>

#include "pico/sync.h"

// WiFi config stored at second flash sector after keydefs
extern uint8_t __flash_storage_start[];
extern uint8_t __flash_storage_end[];

#define WIFI_CONFIG_OFFSET (FLASH_STORE_OFFSET + FLASH_STORE_SIZE)
#define WIFI_CONFIG_ADDRESS ((wifi_config_t*)(__flash_storage_start + FLASH_STORE_SIZE))

web_state_t web_state = {
    .web_access_enabled = false,
    .web_access_expires = 0
};

static wifi_config_t current_config;
static bool wifi_initialized = false;
static bool wifi_connected = false;

// Forward declaration
void wifi_config_save(const wifi_config_t *config);

void wifi_config_init(void) {
    wifi_config_load(&current_config);

#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
    // Credentials are baked into the firmware.
    // If flash config is invalid or doesn't match, use build-time values
    if (!wifi_config_is_valid(&current_config) ||
        current_config.enable_wifi == false ||
        strcmp(current_config.ssid, WIFI_SSID) != 0 ||
        strcmp(current_config.password, WIFI_PASSWORD) != 0) {

        LOG_INFO("Using WiFi config from build-time values\n");

        memset(&current_config, 0, sizeof(current_config));
        memcpy(current_config.magic, WIFI_CONFIG_MAGIC, 8);
        strncpy(current_config.ssid, WIFI_SSID, sizeof(current_config.ssid) - 1);
        strncpy(current_config.password, WIFI_PASSWORD, sizeof(current_config.password) - 1);
        current_config.enable_wifi = true;

        // Note: We don't save to flash here because core1 isn't running yet
        // The config will be used from build-time values on every boot
    }
#endif

    if (!wifi_config_is_valid(&current_config)) {
        LOG_INFO("No valid WiFi config found, WiFi disabled\n");
        memset(&current_config, 0, sizeof(current_config));
        memcpy(current_config.magic, WIFI_CONFIG_MAGIC, 8);
        current_config.enable_wifi = false;
    }
}

void wifi_config_load(wifi_config_t *config) {
    memcpy(config, WIFI_CONFIG_ADDRESS, sizeof(wifi_config_t));
}

static void internal_wifi_config_save(void *data) {
    flash_range_erase(WIFI_CONFIG_OFFSET, WIFI_CONFIG_SIZE);
    flash_range_program(WIFI_CONFIG_OFFSET, (const uint8_t *)data, WIFI_CONFIG_SIZE);
}

void wifi_config_save(const wifi_config_t *config) {
    static uint8_t wifi_config_buffer[WIFI_CONFIG_SIZE];
    memset(wifi_config_buffer, 0xFF, WIFI_CONFIG_SIZE); // Erased flash is 0xFF
    memcpy(wifi_config_buffer, config, sizeof(wifi_config_t));

    int ret = flash_safe_execute(internal_wifi_config_save, wifi_config_buffer, 100);
    if (ret != PICO_OK) {
        LOG_ERROR("wifi_config_save failed with %d", ret);
    } else {
        LOG_INFO("WiFi config saved to flash.");
    }
}

bool wifi_config_is_valid(const wifi_config_t *config) {
    return memcmp(config->magic, WIFI_CONFIG_MAGIC, 8) == 0;
}

void wifi_init(void) {
    if (wifi_initialized) {
        return;
    }

    if (!current_config.enable_wifi) {
        LOG_INFO("WiFi disabled in config\n");
        return;
    }

    if (strlen(current_config.ssid) == 0) {
        LOG_INFO("No WiFi SSID configured\n");
        return;
    }

    LOG_INFO("Initializing WiFi (CYW43)...\n");

    // Initialize CYW43 driver in station mode
    if (cyw43_arch_init()) {
        LOG_ERROR("Failed to initialize CYW43\n");
        return;
    }

    cyw43_arch_enable_sta_mode();
    wifi_initialized = true;

    LOG_INFO("WiFi initialized, connecting to '%s'...\n", current_config.ssid);

    // Start connection (non-blocking)
    int err = cyw43_arch_wifi_connect_async(
        current_config.ssid,
        current_config.password,
        CYW43_AUTH_WPA2_AES_PSK
    );

    if (err) {
        LOG_ERROR("Failed to start WiFi connection: %d\n", err);
    }
}

void wifi_task(void) {
    if (!wifi_initialized) {
        return;
    }

    // Check connection status
    int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    bool was_connected = wifi_connected;
    wifi_connected = (status == CYW43_LINK_UP);

    if (wifi_connected && !was_connected) {
        LOG_INFO("WiFi connected! IP: %s\n",
                 ip4addr_ntoa(netif_ip4_addr(netif_list)));

        // Start mDNS responder
        mdns_resp_init();
        mdns_resp_add_netif(netif_list, "hidproxy");
        LOG_INFO("mDNS responder started: hidproxy.local\n");
    } else if (!wifi_connected && was_connected) {
        LOG_INFO("WiFi disconnected\n");
    }

    // Check web access timeout
    if (web_state.web_access_enabled) {
        if (absolute_time_diff_us(get_absolute_time(), web_state.web_access_expires) > 0) {
            LOG_INFO("Web access timed out\n");
            web_state.web_access_enabled = false;
        }
    }
}

bool wifi_is_connected(void) {
    return wifi_connected;
}

void web_access_enable(void) {
    web_state.web_access_enabled = true;
    web_state.web_access_expires = make_timeout_time_ms(5 * 60 * 1000);  // 5 minutes
    LOG_INFO("Web access enabled for 5 minutes\n");
}

void web_access_disable(void) {
    web_state.web_access_enabled = false;
    LOG_INFO("Web access disabled\n");
}

bool web_access_is_enabled(void) {
    return web_state.web_access_enabled;
}
