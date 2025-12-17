#include "http_server.h"
#include "wifi_config.h"
#include "hid_proxy.h"
#include "macros.h"
#include "keydef_store.h"
#include "encryption.h"
#include "lwip/tcp.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/fs.h"
#include <string.h>
#include <stdio.h>

// Buffer for serializing macros
#define HTTP_MACROS_BUFFER_SIZE (23 * 1024)
static char http_macros_buffer[HTTP_MACROS_BUFFER_SIZE];

// Buffer for POST data
static char http_post_buffer[HTTP_MACROS_BUFFER_SIZE];
static size_t http_post_offset = 0;

// Buffer for unseal password
#define HTTP_PASSWORD_BUFFER_SIZE 256
static char http_password_buffer[HTTP_PASSWORD_BUFFER_SIZE];
static bool is_unseal_request = false;

// Forward declarations of CGI handlers
static const char *status_cgi_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]);

// CGI handler for /status endpoint
static const char *status_cgi_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[]) {
    (void)iIndex;
    (void)iNumParams;
    (void)pcParam;
    (void)pcValue;

    // Count keydefs from kvstore
    uint8_t triggers[256];
    int num_macros = keydef_list(triggers, 256);

    uint64_t uptime_ms = to_ms_since_boot(get_absolute_time());
    int expires_in = 0;
    if (web_state.web_access_enabled) {
        int64_t diff = absolute_time_diff_us(get_absolute_time(), web_state.web_access_expires);
        expires_in = (int)(diff / 1000);  // Convert to milliseconds
    }

    snprintf(http_macros_buffer, sizeof(http_macros_buffer),
             "{\"sealed\":%s,\"web_enabled\":%s,\"expires_in\":%d,\"macros\":%d,\"uptime\":%llu,\"wifi\":%s,\"firmware\":\"%s\"}",
             kb.status == sealed ? "true" : "false",
             web_state.web_access_enabled ? "true" : "false",
             expires_in,
             num_macros,
             uptime_ms,
             wifi_is_connected() ? "true" : "false",
             GIT_COMMIT_HASH
    );

    return "/status.json";
}

// SSI handler for dynamic content
static u16_t ssi_handler(int iIndex, char *pcInsert, int iInsertLen) {
    (void)iIndex;
    (void)pcInsert;
    (void)iInsertLen;
    return 0;
}

// POST handler for /macros.txt
err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd) {
    (void)connection;
    (void)http_request;
    (void)http_request_len;
    (void)post_auto_wnd;

    LOG_INFO("POST begin: %s (len=%d)\n", uri, content_len);

    if (strcmp(uri, "/macros.txt") == 0) {
        // Check web access
        if (!web_access_is_enabled()) {
            snprintf(response_uri, response_uri_len, "/403.html");
            return ERR_OK;
        }

        // Check if device is unsealed
        if (kb.status == sealed) {
            snprintf(response_uri, response_uri_len, "/sealed.html");
            return ERR_OK;
        }

        // Reset buffer
        is_unseal_request = false;
        http_post_offset = 0;
        memset(http_post_buffer, 0, sizeof(http_post_buffer));

        return ERR_OK;
    }

    if (strcmp(uri, "/unseal") == 0) {
        // /unseal endpoint is always available (no web access check required)
        // This allows unsealing the device remotely without physical access
        // Security: Password must be correct, and WiFi must be configured

        // Reset password buffer
        is_unseal_request = true;
        http_post_offset = 0;
        memset(http_password_buffer, 0, sizeof(http_password_buffer));

        return ERR_OK;
    }

    snprintf(response_uri, response_uri_len, "/404.html");
    return ERR_OK;
}

err_t httpd_post_receive_data(void *connection, struct pbuf *p) {
    (void)connection;

    if (is_unseal_request) {
        // Copy password data into password buffer
        if (http_post_offset + p->tot_len < HTTP_PASSWORD_BUFFER_SIZE) {
            pbuf_copy_partial(p, http_password_buffer + http_post_offset, p->tot_len, 0);
            http_post_offset += p->tot_len;
            http_password_buffer[http_post_offset] = '\0';
        } else {
            LOG_ERROR("Password data too large\n");
            return ERR_MEM;
        }
    } else {
        // Copy data into macros buffer
        if (http_post_offset + p->tot_len < HTTP_MACROS_BUFFER_SIZE) {
            pbuf_copy_partial(p, http_post_buffer + http_post_offset, p->tot_len, 0);
            http_post_offset += p->tot_len;
            http_post_buffer[http_post_offset] = '\0';
        } else {
            LOG_ERROR("POST data too large\n");
            return ERR_MEM;
        }
    }

    return ERR_OK;
}

void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len) {
    (void)connection;

    LOG_INFO("POST finished, processing %zu bytes\n", http_post_offset);

    if (is_unseal_request) {
        // Handle unseal request
        LOG_INFO("Processing unseal request\n");

        // Attempt to unseal with provided password
        bool success = enc_unseal_with_password(http_password_buffer);

        // Clear password buffer immediately
        memset(http_password_buffer, 0, sizeof(http_password_buffer));

        if (success) {
            // Update device status
            unseal();
            LOG_INFO("Device unsealed via HTTP\n");
            snprintf(response_uri, response_uri_len, "/unseal_success.json");
        } else {
            LOG_INFO("Unseal failed - incorrect password\n");
            snprintf(response_uri, response_uri_len, "/unseal_failed.json");
        }

        is_unseal_request = false;
    } else {
        // Handle macros POST
        // Parse and save directly to kvstore
        if (parse_macros_to_kvstore(http_post_buffer)) {
            LOG_INFO("Macros updated successfully in kvstore\n");
            snprintf(response_uri, response_uri_len, "/success.html");
        } else {
            LOG_ERROR("Failed to parse macros\n");
            snprintf(response_uri, response_uri_len, "/error.html");
        }
    }
}

// Custom FS callback for /macros.txt
int fs_open_custom(struct fs_file *file, const char *name) {
    if (strcmp(name, "/macros.txt") == 0) {
        // Check web access
        if (!web_access_is_enabled()) {
            LOG_INFO("GET /macros.txt denied - web access disabled\n");
            return 0;  // Not found
        }

        // Check if device is unsealed
        if (kb.status == sealed) {
            LOG_INFO("GET /macros.txt denied - device sealed\n");
            return 0;  // Not found
        }

        // Serialize macros from kvstore
        memset(http_macros_buffer, 0, sizeof(http_macros_buffer));
        if (!serialize_macros_from_kvstore(http_macros_buffer, sizeof(http_macros_buffer))) {
            LOG_ERROR("Failed to serialize macros from kvstore\n");
            return 0;
        }

        // Fill fs_file structure
        memset(file, 0, sizeof(struct fs_file));
        file->data = http_macros_buffer;
        file->len = strlen(http_macros_buffer);
        file->index = file->len;
        // Let lwIP generate the headers. The .txt extension should produce the correct Content-Type.
        // file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;

        LOG_INFO("GET /macros.txt - %d bytes\n", file->len);
        return 1;  // File opened
    }

    if (strcmp(name, "/status.json") == 0) {
        // Already populated by CGI handler
        memset(file, 0, sizeof(struct fs_file));
        file->data = http_macros_buffer;
        file->len = strlen(http_macros_buffer);
        file->index = file->len;
        // Let lwIP generate the headers. The .json extension should produce the correct Content-Type.
        // file->flags = FS_FILE_FLAGS_HEADER_INCLUDED;
        return 1;
    }

    if (strcmp(name, "/unseal_success.json") == 0) {
        snprintf(http_macros_buffer, sizeof(http_macros_buffer),
                 "{\"success\":true,\"message\":\"Device unsealed successfully\"}");
        memset(file, 0, sizeof(struct fs_file));
        file->data = http_macros_buffer;
        file->len = strlen(http_macros_buffer);
        file->index = file->len;
        return 1;
    }

    if (strcmp(name, "/unseal_failed.json") == 0) {
        snprintf(http_macros_buffer, sizeof(http_macros_buffer),
                 "{\"success\":false,\"message\":\"Incorrect password\"}");
        memset(file, 0, sizeof(struct fs_file));
        file->data = http_macros_buffer;
        file->len = strlen(http_macros_buffer);
        file->index = file->len;
        return 1;
    }

    if (strcmp(name, "/success.html") == 0) {
        snprintf(http_macros_buffer, sizeof(http_macros_buffer),
                 "{\"success\":true,\"message\":\"Macros updated successfully\"}");
        memset(file, 0, sizeof(struct fs_file));
        file->data = http_macros_buffer;
        file->len = strlen(http_macros_buffer);
        file->index = file->len;
        return 1;
    }

    if (strcmp(name, "/error.html") == 0) {
        snprintf(http_macros_buffer, sizeof(http_macros_buffer),
                 "{\"success\":false,\"message\":\"Failed to parse macros\"}");
        memset(file, 0, sizeof(struct fs_file));
        file->data = http_macros_buffer;
        file->len = strlen(http_macros_buffer);
        file->index = file->len;
        return 1;
    }

    return 0;  // Not a custom file
}

void fs_close_custom(struct fs_file *file) {
    (void)file;
    // Nothing to clean up
}

static const tCGI cgi_table[] = {
    {"/status", status_cgi_handler}
};

// Global CGI handler required by newer lwIP HTTP server
// Signature: void httpd_cgi_handler(struct fs_file *file, const char* uri, int iNumParams, char *pcParam[], char *pcValue[])
void httpd_cgi_handler(struct fs_file *file, const char* uri, int iNumParams, char *pcParam[], char *pcValue[]) {
    (void)file;
    (void)uri;
    (void)iNumParams;
    (void)pcParam;
    (void)pcValue;
    // This function is required by lwIP but we use http_set_cgi_handlers instead
    // So this is just a stub to satisfy the linker
}

static const char *ssi_tags[] = {
    "status"
};

void http_server_init(void) {
    if (!wifi_is_connected()) {
        LOG_INFO("HTTP server not started - WiFi not connected\n");
        return;
    }

    LOG_INFO("Starting HTTP server...\n");

    // Initialize HTTP server
    httpd_init();

    // Register CGI handlers
    http_set_cgi_handlers(cgi_table, sizeof(cgi_table) / sizeof(cgi_table[0]));

    // Register SSI handlers
    http_set_ssi_handler(ssi_handler, ssi_tags, sizeof(ssi_tags) / sizeof(ssi_tags[0]));

    // Note: POST handlers are registered via compile-time hooks in httpd_opts.h
    // The functions http_post_begin, http_post_receive_data, and http_post_finished
    // are already defined and will be called by the httpd implementation

    LOG_INFO("HTTP server started\n");
}

void http_server_task(void) {
    // HTTP server runs in lwIP callbacks, nothing to do here
}
