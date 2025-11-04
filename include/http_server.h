#ifndef HID_PROXY_HTTP_SERVER_H
#define HID_PROXY_HTTP_SERVER_H

#include <stdbool.h>

// Initialize HTTP server
void http_server_init(void);

// Process HTTP requests (call from main loop)
void http_server_task(void);

#endif // HID_PROXY_HTTP_SERVER_H
