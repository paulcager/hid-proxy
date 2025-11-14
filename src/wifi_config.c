#include "wifi_config.h"
#include "kvstore.h"
#include "kvstore_init.h"
#include "hid_proxy.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "pico/unique_id.h"
#include "lwip/apps/mdns.h"
#include <string.h>

// Note: All WiFi config (including password) is treated as PUBLIC data.
// WiFi credentials are not considered sensitive in this application context.

// WiFi configuration keys
#define WIFI_SSID_KEY "wifi.ssid"
#define WIFI_PASSWORD_KEY "wifi.password"
#define WIFI_COUNTRY_KEY "wifi.country"
#define WIFI_ENABLED_KEY "wifi.enabled"

web_state_t web_state = {
    .web_access_enabled = false,
    .web_access_expires = 0
};

static wifi_config_t current_config;
static bool wifi_initialized = false;
static bool wifi_connected = false;
static bool wifi_suspended = false;

void wifi_config_init(void) {
    // Load WiFi config from kvstore
    wifi_config_load(&current_config);

#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
    // Credentials are baked into the firmware
    // If kvstore config is invalid or doesn't match, use build-time values
    if (!wifi_config_is_valid(&current_config) ||
        !current_config.enable_wifi ||
        strcmp(current_config.ssid, WIFI_SSID) != 0 ||
        strcmp(current_config.password, WIFI_PASSWORD) != 0) {

        LOG_INFO("Using WiFi config from build-time values\n");

        // Save build-time values to kvstore
        memset(&current_config, 0, sizeof(current_config));
        strncpy(current_config.ssid, WIFI_SSID, sizeof(current_config.ssid) - 1);
        strncpy(current_config.password, WIFI_PASSWORD, sizeof(current_config.password) - 1);
        #ifdef WIFI_COUNTRY_CODE
        strncpy(current_config.country, WIFI_COUNTRY_CODE, sizeof(current_config.country) - 1);
        #else
        strncpy(current_config.country, "UK", sizeof(current_config.country) - 1);
        #endif
        current_config.enable_wifi = true;

        // Note: We attempt to save to kvstore, but it's OK if it fails
        // (e.g., if device is locked). Build-time values will be used on next boot.
        wifi_config_save(&current_config);
    }
#endif

    if (!wifi_config_is_valid(&current_config)) {
        LOG_INFO("No valid WiFi config found, WiFi disabled\n");
        memset(&current_config, 0, sizeof(current_config));
        current_config.enable_wifi = false;
    }
}

void wifi_config_load(wifi_config_t *config) {
    size_t size;
    int ret;

    // Initialize config to defaults
    memset(config, 0, sizeof(wifi_config_t));
    config->enable_wifi = false;

    // Load WiFi enabled flag
    uint8_t enabled_byte = 0;
    ret = kvstore_get_value(WIFI_ENABLED_KEY, &enabled_byte, sizeof(enabled_byte), &size, NULL);
    if (ret == 0 && size == sizeof(enabled_byte)) {
        config->enable_wifi = (enabled_byte != 0);
    } else {
        // Default to enabled if not found
        config->enable_wifi = true;
    }

    // Load SSID
    ret = kvstore_get_value(WIFI_SSID_KEY, config->ssid, sizeof(config->ssid), &size, NULL);
    if (ret != 0) {
        LOG_DEBUG("wifi_config_load: SSID not found in kvstore\n");
        config->ssid[0] = '\0';
    } else {
        // Ensure null-terminated
        config->ssid[sizeof(config->ssid) - 1] = '\0';
    }

    // Load password (also public - WiFi creds not considered sensitive here)
    ret = kvstore_get_value(WIFI_PASSWORD_KEY, config->password, sizeof(config->password), &size, NULL);
    if (ret != 0) {
        LOG_DEBUG("wifi_config_load: Password not found in kvstore\n");
        config->password[0] = '\0';
    } else {
        // Ensure null-terminated
        config->password[sizeof(config->password) - 1] = '\0';
    }

    // Load country code
    ret = kvstore_get_value(WIFI_COUNTRY_KEY, config->country, sizeof(config->country), &size, NULL);
    if (ret != 0) {
        // Default to US
        strncpy(config->country, "UK", sizeof(config->country) - 1);
    } else {
        // Ensure null-terminated
        config->country[sizeof(config->country) - 1] = '\0';
    }

    LOG_DEBUG("wifi_config_load: ssid='%s', enabled=%d, country='%s'\n",
              config->ssid, config->enable_wifi, config->country);
}

void wifi_config_save(const wifi_config_t *config) {
    int ret;

    // Save SSID
    ret = kvstore_set_value(WIFI_SSID_KEY, config->ssid, strlen(config->ssid) + 1, false);
    if (ret != 0) {
        LOG_ERROR("wifi_config_save: Failed to save SSID: %s\n", kvs_strerror(ret));
        return;
    }

    // Save password (also public in this application)
    ret = kvstore_set_value(WIFI_PASSWORD_KEY, config->password, strlen(config->password) + 1, false);
    if (ret != 0) {
        LOG_ERROR("wifi_config_save: Failed to save password: %s\n", kvs_strerror(ret));
        return;
    }

    // Save country code
    ret = kvstore_set_value(WIFI_COUNTRY_KEY, config->country, strlen(config->country) + 1, false);
    if (ret != 0) {
        LOG_ERROR("wifi_config_save: Failed to save country: %s\n", kvs_strerror(ret));
        return;
    }

    // Save enabled flag
    uint8_t enabled_byte = config->enable_wifi ? 1 : 0;
    ret = kvstore_set_value(WIFI_ENABLED_KEY, &enabled_byte, sizeof(enabled_byte), false);
    if (ret != 0) {
        LOG_ERROR("wifi_config_save: Failed to save enabled flag: %s\n", kvs_strerror(ret));
        return;
    }

    LOG_INFO("WiFi config saved to kvstore.\n");
}

bool wifi_config_is_valid(const wifi_config_t *config) {
    // Config is valid if SSID is not empty
    return (config->ssid[0] != '\0');
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

        // Start mDNS responder with a unique name
        pico_unique_board_id_t board_id;
        pico_get_unique_board_id(&board_id);
        char mdns_name[32];
        snprintf(mdns_name, sizeof(mdns_name), "hidproxy-%02x%02x",
                 board_id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 2],
                 board_id.id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES - 1]);

        mdns_resp_init();
        mdns_resp_add_netif(netif_list, mdns_name);
        LOG_INFO("mDNS responder started: %s.local\n", mdns_name);
    } else if (!wifi_connected && was_connected) {
        LOG_INFO("WiFi disconnected\n");
    }

    // Check web access timeout
    if (web_state.web_access_enabled) {
        if (time_reached(web_state.web_access_expires)) {
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

void wifi_suspend(void) {
    if (!wifi_initialized || wifi_suspended) {
        return;
    }

    LOG_INFO("Suspending WiFi...\n");

    // Disconnect gracefully
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);

    // Put WiFi into maximum power save mode
    cyw43_wifi_pm(&cyw43_state, CYW43_PM2_POWERSAVE_MODE);

    wifi_suspended = true;
    wifi_connected = false;
    LOG_INFO("WiFi suspended\n");
}

void wifi_resume(void) {
    if (!wifi_initialized || !wifi_suspended) {
        return;
    }

    LOG_INFO("Resuming WiFi...\n");

    // Exit power save mode
    cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);

    // Reconnect to network
    int err = cyw43_arch_wifi_connect_async(
        current_config.ssid,
        current_config.password,
        CYW43_AUTH_WPA2_AES_PSK
    );

    if (err) {
        LOG_ERROR("Failed to reconnect WiFi: %d\n", err);
    }

    wifi_suspended = false;
    LOG_INFO("WiFi resume initiated\n");
}

bool wifi_is_initialized(void) {
    return wifi_initialized;
}

bool wifi_is_suspended(void) {
    return wifi_suspended;
}
