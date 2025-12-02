/**
 * UART Protocol for HID Proxy
 *
 * Simple packet-based protocol for transmitting HID reports between ESP32s
 */

#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#define UART_PACKET_START 0xAA
#define UART_PACKET_ESC   0xAB
#define UART_MAX_PAYLOAD  256

/**
 * Packet types matching the original Pico queue messages
 */
typedef enum {
    PKT_KEYBOARD_REPORT = 0x01,  // HID keyboard report (8 bytes)
    PKT_MOUSE_REPORT = 0x02,     // HID mouse report (5 bytes)
    PKT_LED_UPDATE = 0x03,       // LED status from host (1 byte)
    PKT_STATUS = 0x04,           // Status/debug messages
    PKT_ACK = 0x05,              // Acknowledgement
} packet_type_t;

/**
 * Packet structure with framing and checksum
 */
typedef struct __attribute__((packed)) {
    uint8_t start;       // Always UART_PACKET_START (0xAA)
    uint8_t type;        // packet_type_t
    uint16_t length;     // Payload length (little-endian)
    uint8_t payload[UART_MAX_PAYLOAD];
    uint8_t checksum;    // Simple XOR checksum
} uart_packet_t;

/**
 * Initialize UART protocol
 *
 * @param uart_num UART peripheral number (0, 1, or 2)
 * @param tx_pin GPIO pin for TX
 * @param rx_pin GPIO pin for RX
 * @return ESP_OK on success
 */
esp_err_t uart_protocol_init(int uart_num, int tx_pin, int rx_pin);

/**
 * Send a packet over UART
 *
 * @param type Packet type
 * @param data Payload data
 * @param len Payload length (must be <= UART_MAX_PAYLOAD)
 * @return ESP_OK on success
 */
esp_err_t uart_send_packet(packet_type_t type, const void *data, uint16_t len);

/**
 * Receive a packet from UART (blocking)
 *
 * @param packet Buffer to store received packet
 * @param timeout Timeout in FreeRTOS ticks (portMAX_DELAY for infinite)
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout, ESP_ERR_INVALID_CRC on checksum failure
 */
esp_err_t uart_recv_packet(uart_packet_t *packet, TickType_t timeout);

#endif // UART_PROTOCOL_H
