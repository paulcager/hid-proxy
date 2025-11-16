#ifdef PICO_CYW43_SUPPORTED

#include "mqtt_client.h"
#include "hid_proxy.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"
#include <string.h>
#include <stdio.h>

#ifdef MQTT_USE_TLS
#include "lwip/altcp_tls.h"
#include "lwip/apps/mqtt_priv.h"
#endif

// MQTT configuration from build-time defines
#ifndef MQTT_BROKER
#define MQTT_BROKER NULL
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif

#ifndef MQTT_TLS_PORT
#define MQTT_TLS_PORT 8883
#endif
#define MQTT_KEEP_ALIVE_S 60
#define MQTT_QOS 1
#define MQTT_RETAIN 1

// Topic buffer size
#define TOPIC_LEN 64

typedef struct {
    mqtt_client_t *client;
    struct mqtt_connect_client_info_t client_info;
    ip_addr_t broker_addr;
    bool connected;
    bool dns_done;
    char client_id[16];  // "hidproxy-XXXX"
    char topic_prefix[16];  // "hidproxy-XXXX"
} mqtt_state_t;

static mqtt_state_t mqtt_state = {0};

// MQTT callbacks
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    mqtt_state_t *state = (mqtt_state_t *)arg;

    if (status == MQTT_CONNECT_ACCEPTED) {
        LOG_INFO("MQTT connected to broker\n");
        state->connected = true;

        // Publish online status
        char topic[TOPIC_LEN];
        snprintf(topic, sizeof(topic), "%s/status", state->topic_prefix);
        const char *msg = "online";
        mqtt_publish(client, topic, msg, strlen(msg), MQTT_QOS, MQTT_RETAIN, NULL, NULL);
    } else {
        LOG_ERROR("MQTT connection failed: %d\n", status);
        state->connected = false;
    }
}

static void mqtt_pub_request_cb(void *arg, err_t err) {
    (void)arg;  // Unused
    if (err != ERR_OK) {
        LOG_ERROR("MQTT publish failed: %d\n", err);
    }
}

// DNS callback
static void dns_found_cb(const char *hostname, const ip_addr_t *ipaddr, void *arg) {
    (void)hostname;  // Unused
    mqtt_state_t *state = (mqtt_state_t *)arg;

    if (ipaddr) {
        LOG_INFO("MQTT broker resolved: %s\n", ipaddr_ntoa(ipaddr));
        state->broker_addr = *ipaddr;
        state->dns_done = true;

        // Connect to broker
#if defined(LWIP_ALTCP) && defined(LWIP_ALTCP_TLS) && defined(MQTT_USE_TLS)
        const int port = MQTT_TLS_PORT;
        LOG_INFO("Connecting to MQTT broker with TLS on port %d\n", port);
#else
        const int port = MQTT_PORT;
        LOG_INFO("Connecting to MQTT broker (no TLS) on port %d\n", port);
#endif

        cyw43_arch_lwip_begin();
        err_t err = mqtt_client_connect(state->client, &state->broker_addr, port,
                                        mqtt_connection_cb, state, &state->client_info);
        if (err != ERR_OK) {
            LOG_ERROR("MQTT connect failed: %d\n", err);
        }
#if defined(LWIP_ALTCP) && defined(LWIP_ALTCP_TLS) && defined(MQTT_USE_TLS)
        // Set hostname for SNI (Server Name Indication)
        if (state->client->conn) {
            mbedtls_ssl_set_hostname(altcp_tls_context(state->client->conn), MQTT_BROKER);
        }
#endif
        cyw43_arch_lwip_end();
    } else {
        LOG_ERROR("MQTT DNS lookup failed\n");
        state->dns_done = true;  // Mark as done even on failure
    }
}

bool mqtt_client_init(void) {
    // Check if MQTT is configured
    if (!MQTT_BROKER) {
        LOG_INFO("MQTT not configured (MQTT_BROKER not set)\n");
        return false;
    }
    if (strlen(MQTT_BROKER) == 0) {
        LOG_INFO("MQTT not configured (MQTT_BROKER empty)\n");
        return false;
    }

    LOG_INFO("Initializing MQTT client for broker: %s\n", (const char *)MQTT_BROKER);

    // Get last 4 hex digits of board ID for topic prefix
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    snprintf(mqtt_state.client_id, sizeof(mqtt_state.client_id),
             "hidproxy-%02x%02x", id.id[6], id.id[7]);
    snprintf(mqtt_state.topic_prefix, sizeof(mqtt_state.topic_prefix),
             "hidproxy-%02x%02x", id.id[6], id.id[7]);

    LOG_INFO("MQTT client ID: %s\n", mqtt_state.client_id);

    // Create MQTT client
    mqtt_state.client = mqtt_client_new();
    if (!mqtt_state.client) {
        LOG_ERROR("Failed to create MQTT client\n");
        return false;
    }

    // Setup client info
    mqtt_state.client_info.client_id = mqtt_state.client_id;
#if defined(MQTT_USERNAME) && defined(MQTT_PASSWORD)
    mqtt_state.client_info.client_user = MQTT_USERNAME;
    mqtt_state.client_info.client_pass = MQTT_PASSWORD;
    LOG_INFO("MQTT authentication enabled (user: %s)\n", MQTT_USERNAME);
#else
    mqtt_state.client_info.client_user = NULL;
    mqtt_state.client_info.client_pass = NULL;
    LOG_INFO("MQTT authentication disabled (no credentials)\n");
#endif
    mqtt_state.client_info.keep_alive = MQTT_KEEP_ALIVE_S;

    // Setup Last Will and Testament (LWT) - publish "offline" when disconnected
    static char will_topic[TOPIC_LEN];
    snprintf(will_topic, sizeof(will_topic), "%s/status", mqtt_state.topic_prefix);
    mqtt_state.client_info.will_topic = will_topic;
    mqtt_state.client_info.will_msg = "offline";
    mqtt_state.client_info.will_qos = MQTT_QOS;
    mqtt_state.client_info.will_retain = MQTT_RETAIN;

#if defined(LWIP_ALTCP) && defined(LWIP_ALTCP_TLS) && defined(MQTT_USE_TLS)
    // Setup TLS (no certificate verification for local broker)
    LOG_INFO("Configuring MQTT with TLS\n");
    mqtt_state.client_info.tls_config = altcp_tls_create_config_client(NULL, 0);
#endif

    // Start DNS lookup for broker
    LOG_INFO("Looking up MQTT broker: %s\n", (const char *)MQTT_BROKER);
    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(MQTT_BROKER, &mqtt_state.broker_addr, dns_found_cb, &mqtt_state);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) {
        // Address cached, connect immediately
        dns_found_cb(MQTT_BROKER, &mqtt_state.broker_addr, &mqtt_state);
    } else if (err != ERR_INPROGRESS) {
        LOG_ERROR("MQTT DNS lookup failed immediately: %d\n", err);
        return false;
    }
    // ERR_INPROGRESS means callback will be called later

    return true;
}

void mqtt_publish_lock_state(bool locked) {
    if (!mqtt_state.connected || !mqtt_state.client) {
        return;
    }

    char topic[TOPIC_LEN];
    snprintf(topic, sizeof(topic), "%s/lock", mqtt_state.topic_prefix);

    const char *msg = locked ? "locked" : "unlocked";

    LOG_INFO("Publishing MQTT: %s = %s\n", topic, msg);

    cyw43_arch_lwip_begin();
    err_t err = mqtt_publish(mqtt_state.client, topic, msg, strlen(msg),
                            MQTT_QOS, MQTT_RETAIN, mqtt_pub_request_cb, NULL);
    cyw43_arch_lwip_end();

    if (err != ERR_OK) {
        LOG_ERROR("MQTT publish failed: %d\n", err);
    }
}

void mqtt_client_task(void) {
    // Nothing to do here - MQTT runs in lwIP callbacks
    // This function exists for future expansion if needed
}

bool mqtt_is_connected(void) {
    return mqtt_state.connected && mqtt_state.client &&
           mqtt_client_is_connected(mqtt_state.client);
}

#endif // PICO_CYW43_SUPPORTED
