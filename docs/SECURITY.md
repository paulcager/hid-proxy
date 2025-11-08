# Security Review

This document provides a security review of the hid-proxy firmware. The review is divided into two main sections: core functionality and the optional WiFi/HTTP server.

## Core Functionality

The core functionality of the hid-proxy involves intercepting keyboard inputs, replaying macros, and storing macro definitions in flash.

### Data Encryption and Storage

* **Encryption Algorithm:** Data at rest (macro definitions) is encrypted using AES-128 in Counter (CTR) mode. AES-128 is a strong encryption standard. CTR mode is a stream cipher
  mode that is secure when a unique Initialization Vector (IV) is used for each encryption with the same key.
* **Initialization Vector (IV):** The IV is generated using `get_rand_64()`, which relies on the hardware random number generator of the RP2040. This is a good source of
  randomness, and using a unique IV for each encryption is a critical security measure that is correctly implemented.
* **In-place Encryption:** The firmware encrypts the data in-place before writing to flash and decrypts it after reading. This is an efficient use of memory.

### Key Derivation

* **Password-Based Key Derivation:** The encryption key is derived from a user-provided passphrase.
* **Salting:** The key derivation process uses the unique board ID of the Pico as a salt. This is an excellent security practice, as it ensures that the same passphrase will result
  in a different encryption key on different devices, preventing rainbow table attacks.
* **Hashing:** The salted passphrase is processed using SHA-256 to generate the final encryption key. SHA-256 is a secure hashing algorithm.

### State Management

* **Idle Timeout:** The device automatically locks after a period of inactivity (`IDLE_TIMEOUT_MILLIS`). This is a strong security feature that prevents unauthorized access to the
  macros if the device is left unattended.
* **Locking:** When locked, the in-memory macro definitions are cleared. This ensures that sensitive data is not kept in memory when the device is not in use.

### Macro Definition

* **Buffer Overflow Prevention:** The code for defining macros includes checks to prevent buffer overflows when adding new reports to a macro definition. If the buffer is full, the
  new report is ignored. This is a good security measure.
* **Parsing Macros:** The `parse_macros` function, which parses text-based macro definitions, uses `panic()` to handle buffer overflows. On a microcontroller, this is a safe
  approach as it prevents the device from entering an undefined state.

## WiFi/HTTP Server (Optional)

The optional WiFi/HTTP server provides a web interface for managing macros.

### Lack of Transport Layer Security

* **No TLS/SSL:** The HTTP server does not use TLS/SSL. This means that all communication, including the WiFi password (if configured via the web interface in the future) and macro
  data, is transmitted in cleartext. This is a significant vulnerability, as an attacker on the same network could intercept and read or modify the data. As noted, this is a known
  constraint of the hardware.

### Authentication and Authorization

* **Web Access Control:** The web interface is disabled by default and can only be enabled for a short period (5 minutes) by a physical key combination on the keyboard. This is an
  excellent security measure that significantly reduces the attack surface of the web interface.
* **Device Lock Check:** The web server checks if the device is unlocked before allowing access to macro data. This prevents an attacker from accessing the data even if they manage
  to access the web interface.

### Data Handling

* **POST Data Handling:** The HTTP server uses a fixed-size buffer for handling POST data and checks the size of the incoming data to prevent buffer overflows. This is a good
  security practice.

## Known Constraints and Recommendations

* **Physical Access:** An attacker with physical access to the device could potentially read the contents of the flash memory. However, the data is encrypted, so this would not
  reveal the macro definitions without the passphrase.
* **Side-Channel Attacks:** While unlikely, an attacker could potentially attempt side-channel attacks (e.g., power analysis) to extract the encryption key.
* **NFC Keys:** The `nfc_tag.c` file contains a list of known default keys for MIFARE Classic tags. These keys are public knowledge and should not be considered secure. The primary
  security of the NFC functionality relies on the fact that it's used to transfer the user's high-entropy encryption key, not on the security of the NFC tags themselves.

Overall, the firmware demonstrates a strong understanding of security principles, especially for a resource-constrained device. The use of strong encryption, proper key derivation,
and state management practices provides a solid security posture for the core functionality. The optional web interface, while lacking transport-layer security, has effective
authorization controls that limit its exposure.
