#ifndef HTTPS_CERT_H
#define HTTPS_CERT_H

#include <stddef.h>

// Self-signed certificate for HTTPS server
// Generated with generate_cert.sh script
// Note: Browser will show security warning for self-signed certs

extern const unsigned char server_cert_pem[];
extern const size_t server_cert_pem_len;

extern const unsigned char server_key_pem[];
extern const size_t server_key_pem_len;

#endif // HTTPS_CERT_H
