#ifndef LWIP_HTTPD_OPTS_H
#define LWIP_HTTPD_OPTS_H

#include "lwip/opt.h"
#include "lwip/pbuf.h"

// Declare POST handler function prototypes
// These functions are defined in http_server.c
err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                       u16_t http_request_len, int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd);

err_t httpd_post_receive_data(void *connection, struct pbuf *p);

void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len);

#endif /* LWIP_HTTPD_OPTS_H */
