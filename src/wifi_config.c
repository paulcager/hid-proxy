#include "wifi_config.h"
#include "hid_proxy.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/flash.h"
#include "lwip/apps/mdns.h"
#include <string.h>

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

void wifi_config_init(void) {
    wifi_config_load(&current_config);

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

void wifi_config_save(const wifi_config_t *config) {
    // TODO: Implement flash write for WiFi config
    // Similar to save_state() but for WiFi config sector
    LOG_INFO("WiFi config save not yet implemented\n");
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
