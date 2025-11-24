/*
 * wifi_console.c
 *
 * Serial console for WiFi configuration
 * Provides interactive setup of WiFi credentials via UART
 */

#include "wifi_config.h"
#include "hid_proxy.h"
#include "kvstore_init.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX_INPUT_LEN 64

static char input_buffer[MAX_INPUT_LEN];
static int input_pos = 0;

// Read a line from stdin (UART) with timeout
static bool read_line_with_timeout(char *buffer, size_t max_len, uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    int pos = 0;

    printf("> ");
    fflush(stdout);

    while (!time_reached(deadline)) {
        int c = getchar_timeout_us(1000);  // 1ms polling

        if (c == PICO_ERROR_TIMEOUT) {
            continue;
        }

        if (c == '\n' || c == '\r') {
            buffer[pos] = '\0';
            printf("\n");
            return (pos > 0);  // Return true if we got at least one character
        }

        if (c == '\b' || c == 127) {  // Backspace or DEL
            if (pos > 0) {
                pos--;
                printf("\b \b");  // Erase character on terminal
                fflush(stdout);
            }
            continue;
        }

        if (c >= 32 && c < 127 && pos < (int)max_len - 1) {  // Printable ASCII
            buffer[pos++] = (char)c;
            putchar(c);  // Echo character
            fflush(stdout);
        }
    }

    printf("\nTimeout\n");
    return false;
}

void wifi_console_setup(void) {
    printf("\n");
    printf("======================================\n");
    printf("  WiFi Configuration Console\n");
    printf("======================================\n");
    printf("\n");

    // Load current config
    wifi_config_t config;
    wifi_config_load(&config);

    if (wifi_config_is_valid(&config)) {
        printf("Current WiFi SSID: '%s'\n", config.ssid);
        printf("Current WiFi country: '%s'\n", config.country);
        printf("WiFi enabled: %s\n", config.enable_wifi ? "Yes" : "No");
        printf("\n");
    } else {
        printf("No WiFi configuration found.\n");
        printf("\n");
    }

    // Ask if user wants to configure WiFi
    printf("Configure WiFi? (y/n): ");
    fflush(stdout);

    char response[8];
    if (!read_line_with_timeout(response, sizeof(response), 30000)) {  // 30 second timeout
        printf("WiFi configuration cancelled\n");
        return;
    }

    if (response[0] != 'y' && response[0] != 'Y') {
        printf("WiFi configuration cancelled\n");
        return;
    }

    // Get SSID
    printf("\nEnter WiFi SSID (max 31 chars):\n");
    char ssid[32];
    if (!read_line_with_timeout(ssid, sizeof(ssid), 60000)) {  // 60 second timeout
        printf("WiFi configuration cancelled\n");
        return;
    }

    if (strlen(ssid) == 0) {
        printf("SSID cannot be empty. Configuration cancelled.\n");
        return;
    }

    // Get password
    printf("\nEnter WiFi password (max 63 chars):\n");
    printf("(Input will be visible on console)\n");
    char password[64];
    if (!read_line_with_timeout(password, sizeof(password), 60000)) {  // 60 second timeout
        printf("WiFi configuration cancelled\n");
        return;
    }

    // Get country code (optional, default to current or UK)
    printf("\nEnter country code (2 letters, e.g., US, UK, DE) [%s]: ", config.country);
    fflush(stdout);
    char country[8];
    if (!read_line_with_timeout(country, sizeof(country), 30000)) {
        printf("Using default country code: %s\n", config.country);
        strncpy(country, config.country, sizeof(country) - 1);
    } else if (strlen(country) == 0) {
        strncpy(country, config.country, sizeof(country) - 1);
    } else if (strlen(country) != 2) {
        printf("Invalid country code (must be 2 letters). Using %s\n", config.country);
        strncpy(country, config.country, sizeof(country) - 1);
    } else {
        // Convert to uppercase
        country[0] = (char)toupper((unsigned char)country[0]);
        country[1] = (char)toupper((unsigned char)country[1]);
        country[2] = '\0';
    }

    // Confirm before saving
    printf("\n");
    printf("--------------------------------------\n");
    printf("New WiFi configuration:\n");
    printf("  SSID: %s\n", ssid);
    printf("  Password: %s\n", password);
    printf("  Country: %s\n", country);
    printf("--------------------------------------\n");
    printf("Save this configuration? (y/n): ");
    fflush(stdout);

    if (!read_line_with_timeout(response, sizeof(response), 30000)) {
        printf("WiFi configuration cancelled\n");
        return;
    }

    if (response[0] != 'y' && response[0] != 'Y') {
        printf("WiFi configuration cancelled\n");
        return;
    }

    // Save configuration
    memset(&config, 0, sizeof(config));
    strncpy(config.ssid, ssid, sizeof(config.ssid) - 1);
    strncpy(config.password, password, sizeof(config.password) - 1);
    strncpy(config.country, country, sizeof(config.country) - 1);
    config.enable_wifi = true;

    wifi_config_save(&config);

    printf("\n");
    printf("WiFi configuration saved successfully!\n");
    printf("\n");
    printf("Please reboot the device for changes to take effect:\n");
    printf("  - Hold both shifts and press HOME to enter bootloader\n");
    printf("  - Or use the watchdog reset command if available\n");
    printf("\n");
    printf("======================================\n");
}
