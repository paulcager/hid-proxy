#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdbool.h>

// Initialize MQTT client
// Returns true if MQTT is configured and client initialized successfully
bool mqtt_client_init(void);

// Publish seal state change event
// state: true = sealed, false = unsealed
void mqtt_publish_seal_state(bool sealed);

// Publish custom message to arbitrary topic (for keydef macros)
// topic: MQTT topic string (e.g., "hidproxy/light/bedroom")
// message: Message payload (e.g., "ON", "OFF", "toggle")
void mqtt_publish_custom(const char *topic, const char *message);

// Periodic MQTT task - call from main loop
void mqtt_client_task(void);

// Check if MQTT is connected
bool mqtt_is_connected(void);

#endif // MQTT_CLIENT_H
