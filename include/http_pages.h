#ifndef HID_PROXY_HTTP_PAGES_H
#define HID_PROXY_HTTP_PAGES_H

// Simple HTML response pages

static const char http_403_page[] =
    "HTTP/1.1 403 Forbidden\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><title>403 Forbidden</title></head>"
    "<body><h1>403 Forbidden</h1>"
    "<p>Web access not enabled. Press both shifts + HOME on the keyboard to enable access for 5 minutes.</p>"
    "</body></html>";

static const char http_locked_page[] =
    "HTTP/1.1 423 Locked\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><title>Device Locked</title></head>"
    "<body><h1>Device Locked</h1>"
    "<p>The device is currently locked. Unlock it using your passphrase or NFC tag first.</p>"
    "</body></html>";

static const char http_success_page[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><title>Success</title></head>"
    "<body><h1>Success</h1>"
    "<p>Macros updated successfully. Changes have been written to flash.</p>"
    "</body></html>";

static const char http_error_page[] =
    "HTTP/1.1 400 Bad Request\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><title>Error</title></head>"
    "<body><h1>Error</h1>"
    "<p>Failed to parse macro data. Please check the syntax and try again.</p>"
    "</body></html>";

static const char http_404_page[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><title>404 Not Found</title></head>"
    "<body><h1>404 Not Found</h1>"
    "<p>The requested page was not found.</p>"
    "</body></html>";

#endif // HID_PROXY_HTTP_PAGES_H
