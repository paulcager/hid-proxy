/**
 * UART Protocol Implementation
 */

#include "uart_protocol.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>

#define UART_BUF_SIZE 1024
static const char *TAG = "uart_protocol";
static int g_uart_num = -1;

esp_err_t uart_protocol_init(int uart_num, int tx_pin, int rx_pin) {
    g_uart_num = uart_num;

    uart_config_t uart_config = {
        .baud_rate = 921600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(uart_num, UART_BUF_SIZE * 2,
                                        UART_BUF_SIZE * 2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART protocol initialized on UART%d (TX:%d, RX:%d, baud:%d)",
             uart_num, tx_pin, rx_pin, uart_config.baud_rate);
    return ESP_OK;
}

esp_err_t uart_send_packet(packet_type_t type, const void *data, uint16_t len) {
    if (len > UART_MAX_PAYLOAD) {
        ESP_LOGE(TAG, "Payload too large: %d > %d", len, UART_MAX_PAYLOAD);
        return ESP_ERR_INVALID_SIZE;
    }

    // Build packet in local buffer
    uint8_t buffer[5 + UART_MAX_PAYLOAD];
    buffer[0] = UART_PACKET_START;
    buffer[1] = type;
    buffer[2] = len & 0xFF;         // Little-endian length
    buffer[3] = (len >> 8) & 0xFF;

    if (len > 0 && data != NULL) {
        memcpy(&buffer[4], data, len);
    }

    // Calculate XOR checksum over header + payload
    uint8_t checksum = 0;
    for (int i = 0; i < 4 + len; i++) {
        checksum ^= buffer[i];
    }
    buffer[4 + len] = checksum;

    // Send entire packet
    int written = uart_write_bytes(g_uart_num, buffer, 5 + len);
    if (written != 5 + len) {
        ESP_LOGE(TAG, "UART write failed: wrote %d/%d bytes", written, 5 + len);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t uart_recv_packet(uart_packet_t *packet, TickType_t timeout) {
    uint8_t start;

    // Wait for start byte (with sync recovery)
    while (1) {
        int len = uart_read_bytes(g_uart_num, &start, 1, timeout);
        if (len <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        if (start == UART_PACKET_START) {
            break;
        }
        ESP_LOGW(TAG, "Skipping invalid start byte: 0x%02X (expected 0x%02X)",
                 start, UART_PACKET_START);
    }

    packet->start = start;

    // Read header (type + length, 3 bytes)
    int len = uart_read_bytes(g_uart_num, &packet->type, 3, timeout);
    if (len != 3) {
        ESP_LOGE(TAG, "Failed to read packet header (got %d bytes)", len);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Reconstruct length from little-endian
    uint16_t payload_len = packet->length;  // Already in correct byte order from struct

    if (payload_len > UART_MAX_PAYLOAD) {
        ESP_LOGE(TAG, "Payload length too large: %d > %d", payload_len, UART_MAX_PAYLOAD);
        return ESP_ERR_INVALID_SIZE;
    }

    // Read payload + checksum
    len = uart_read_bytes(g_uart_num, packet->payload, payload_len + 1, timeout);
    if (len != payload_len + 1) {
        ESP_LOGE(TAG, "Failed to read payload: got %d, expected %d",
                 len, payload_len + 1);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Verify checksum
    uint8_t calc_checksum = 0;
    uint8_t *bytes = (uint8_t*)packet;
    for (int i = 0; i < 4 + payload_len; i++) {
        calc_checksum ^= bytes[i];
    }

    uint8_t recv_checksum = packet->payload[payload_len];
    if (calc_checksum != recv_checksum) {
        ESP_LOGE(TAG, "Checksum mismatch: calc=0x%02X, recv=0x%02X",
                 calc_checksum, recv_checksum);
        return ESP_ERR_INVALID_CRC;
    }

    return ESP_OK;
}
