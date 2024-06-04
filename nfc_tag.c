
#include "nfc_tag.h"
#include <pico/time.h>
#include <pico/printf.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <pico/binary_info/code.h>
#include <pico/rand.h>
#include "pn532-lib/pn532_rp2040.h"
#include "hid_proxy.h"
#include "logging.h"

#define I2C_SDA      4
#define I2C_SCL      5
#define INTERRUPT   15
#define PN532_ADDRESS           0x24
#define HOST_TO_PN532           0xD4
#define PN532_TO_HOST           0xD5
#define KEY_ADDRESS 0x3A


// See https://www.nxp.com/docs/en/user-guide/141520.pdf for details about frame format and use.
typedef struct {
    uint8_t preamble;
    uint8_t start_code_00;
    uint8_t start_code_FF;
    uint8_t command_length;
    uint8_t command_length_cksum;
    uint8_t host;       // Either HOST_TO_PN532 or PN532_TO_HOST
    uint8_t command;
} frame_header_t;

typedef struct {
    uint8_t cksum;
    uint8_t postamble;
} frame_trailer_t;

typedef struct {
    uint8_t preamble;
    uint8_t start_code_00;
    uint8_t start_code_FF;
    uint8_t ack_00;
    uint8_t ack_ff;
    uint8_t postamble;
} ack_t;

typedef struct {
    uint8_t num_targets;
    // Following is really an array, but we are only reading one.
    uint8_t target_num;
    uint8_t sens_res0;
    uint8_t sens_res1;
    uint8_t sel_res;
    uint8_t id_length;
    uint8_t id[7];
} read_passive_response_data_t;

struct {
    enum {
        starting = 0,
        getting_version,
        sending_config,
        waiting_for_tag,
        waiting_for_auth,
        waiting_for_data,
        waiting_for_write,
        idle,
    } status;
    bool write_requested;
    absolute_time_t write_timeout;
    absolute_time_t key_read_time;
    bool waiting_for_ack;
    bool unavailable;        // No PN32, or given up because of errors.
    uint8_t target_num;
    uint8_t id_length;
    uint8_t id[7];  //  Mifare classic is 4-byte, NTAG213 is 7-byte (we won't work with one of those).
    bool key_known;
    uint8_t key[16];
} state;

static char *nfc_status_string(int status) {
    switch (status) {
        case starting:
            return "starting";
        case getting_version:
            return "getting_version";
        case sending_config:
            return "sending_config";
        case waiting_for_tag:
            return "waiting_for_tag";
        case waiting_for_auth:
            return "waiting_for_auth";
        case waiting_for_data:
            return "waiting_for_data";
        case waiting_for_write:
            return "waiting_for_write";
        case idle:
            return "idle";
        default:
            return "Unknown";
    }
}


static uint8_t frame[sizeof(frame_header_t) + 0x00ff +  sizeof(frame_trailer_t)];
static uint8_t *frame_data = frame + sizeof(frame_header_t);
static uint frame_size;

static uint8_t response[1 + sizeof(frame_header_t) + 0x00ff +  sizeof(frame_trailer_t)];
static uint8_t *response_data;
static uint8_t response_data_size;

// These 2 should only be updated in the ISR or while interrupts are disabled.
static volatile uint32_t messages_pending = 0;
static volatile absolute_time_t interrupt_time;

static void gpio_callback(uint gpio, uint32_t events);
static void read_ack();
static int read_data(uint8_t *buff, uint8_t size);
static void send_frame(uint8_t command, uint8_t *data, uint8_t data_length);
static bool decode_frame(uint8_t* input_frame, uint8_t frame_length, uint8_t **data, uint8_t *data_length);
static uint8_t get_reader_status();
static bool send_write_request(uint8_t *data, size_t data_length);
static bool send_read_request();
static void scan_for_tag();
static void send_authentication();

static char *id_as_string() {
    static char buff[3 * sizeof(state.id)];
    memset(buff, '\0', sizeof(buff));
    char *ptr = buff;
    for (int i = 0; i < state.id_length; i++) {
        if (i != 0) {
            *ptr++ = ':';
        }
        snprintf(ptr, 3, "%02X", state.id[i]);
        ptr += 2;
    }
    return buff;
}

// ================================ Public functions ================================
void nfc_write_key(uint8_t *key, size_t key_length, unsigned long timeout_millis) {
    if (key_length != 16) {
        LOG_ERROR("Ignoring attempt to write %d byte key", key_length);
        return;
    }

    LOG_INFO("NFC write requested\n");

    state.write_requested = true;
    state.key_known = false;    // I guess it is known, but we'd like to clear it anyway.
    memcpy(state.key, key, key_length);
    state.write_timeout = make_timeout_time_ms(timeout_millis);

    switch (state.status) {
        case starting:
        case getting_version:
        case sending_config:
        case waiting_for_tag:
            // Wait for initialisation to be completed. It will then see the write_requested flag.
            return;

        default:
            scan_for_tag();
            state.status = waiting_for_tag;
    }
}

bool nfc_key_available() {
    return state.key_known;
}

bool nfc_get_key(uint8_t key[16]) {
    if (state.key_known) {
        memcpy(key, state.key, sizeof(state.key));
        memset(state.key, 0, sizeof(state.key));
        state.key_known = false;
        return true;
    }

    return false;
}

// ==================================================================================



static void create_frame(uint8_t command, uint8_t* data, uint8_t data_length) {
    frame_header_t *hdr = (frame_header_t *) frame;
    frame_trailer_t *trailer = (frame_trailer_t *) (frame + sizeof(frame_header_t) + data_length);
    hdr->preamble = 0;
    hdr->start_code_00 = 0x00;
    hdr->start_code_FF = 0xff;
    hdr->command_length = (2 + data_length) & 0xff;
    hdr->command_length_cksum = (~hdr->command_length + 1) & 0xff;
    hdr->host = HOST_TO_PN532;
    hdr->command = command;

    if (data_length > 0) {
        memcpy(frame_data, data, data_length);
    }

    uint8_t cksum = 0xff + PN532_HOSTTOPN532 + command;
    for (int i = 0; i < data_length; i++) {
        cksum += data[i];
    }
    trailer->cksum = ~cksum & 0xff;
    trailer->postamble = 0;

    frame_size = sizeof(frame_header_t) + data_length + sizeof(frame_trailer_t);
#ifdef DEBUG
    LOG_DEBUG("Frame @ %p, hdr @ %p, frame_data@ %p. frame_data[0]==0x%02x, data[0]=0x%02x\n", frame, hdr, frame_data, frame_data[0], data[0]);
    hex_dump(frame, frame_size);
#endif

}

static bool read_response(size_t max_data_len) {
    memset(response, 0, sizeof(response));
    const size_t size = 1 + sizeof(frame_header_t) + max_data_len +  sizeof(frame_trailer_t);
    int read = i2c_read_blocking(i2c0, PN532_ADDRESS, response, size, false);
    if (read <= 0) {
        return false;
    }

    if (response[0] == 0) {
        // PN532 says not ready.
        return false;
    }

    if ((unsigned)read != size) {
        LOG_ERROR("Short read. Wanted %d got %d\n", size, read);
        hex_dump(response, read);
    }

    return decode_frame(response + 1, read - 1, &response_data, &response_data_size);
}

static bool decode_frame(uint8_t* input_frame, uint8_t frame_length, uint8_t **data, uint8_t *data_length) {
    if (frame_length < (sizeof(frame_header_t) + sizeof(frame_trailer_t))) {
        LOG_ERROR("Short frame: ");
        hex_dump(input_frame, frame_length);
        return false;
    }

    frame_header_t *hdr = (frame_header_t *) input_frame;
    if (hdr->preamble != 0 || hdr->start_code_00 != 0x00 || hdr->start_code_FF != 0xff || hdr->host != PN532_TO_HOST) {
        LOG_ERROR("Invalid header: preamble: %02x, start_code=%02x%02x, host=%02x\n", hdr->preamble, hdr->start_code_00, hdr->start_code_FF, hdr->host);
        hex_dump(input_frame, frame_length);
        return false;
    }
    // TODO - also check response's command is request's command + 1.

    if (((~hdr->command_length + 1) & 0xff) != hdr->command_length_cksum) {
        LOG_ERROR("Invalid cksum: %0x != %0x\n", ((~hdr->command_length + 1) & 0xff), hdr->command_length_cksum);
        hex_dump(input_frame, frame_length);
        return false;
    }

    if (data_length != NULL) *data_length = hdr->command_length - 2;
    if (data != NULL) *data = input_frame + sizeof(frame_header_t);

    return true;
}

void try_nfc();



void nfc_setup() {
    i2c_deinit(i2c0);
    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Make the I2C pins available to picotool
    bi_decl(bi_2pins_with_func(I2C_SDA, I2C_SCL, GPIO_FUNC_I2C));

    gpio_set_irq_enabled_with_callback(INTERRUPT, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);

    state.waiting_for_ack = false;
    state.status = starting;
    state.unavailable = false;

    // Ping I2C to detect the board.
    int ret;
    uint8_t rxdata;
    ret = i2c_read_timeout_us(i2c_default, PN532_ADDRESS, &rxdata, 1, false, 50*1000);
    if (ret < 0) {
        LOG_INFO("Could not find device on I2C address %d (error code=%d). NFC will be disabled.\n", PN532_ADDRESS, ret);
        state.unavailable = true;
    }

    // Maybe we should send an ACK here in case the PN532 is busy.
}



void nfc_task(bool key_required) {
    static int previous_status = 0;
    static bool previous_ack = false;
    if (previous_status != state.status || previous_ack != state.waiting_for_ack) {
        LOG_INFO("NFC status changed from %s%s to %s%s\n",
                 nfc_status_string(previous_status),
                 previous_ack ? "[ACK pending]" : "",
                 nfc_status_string(state.status),
                 state.waiting_for_ack ? "[ACK pending]" : ""
                 );
        previous_status = state.status;
        previous_ack = state.waiting_for_ack;
    }

    if (state.unavailable) {
        return;
    }

    if (state.status == starting) {
        LOG_INFO("Sending GETFIRMWAREVERSION to start identification process\n");
        send_frame(PN532_COMMAND_GETFIRMWAREVERSION, NULL, 0);
        assert(state.waiting_for_ack);
        state.status = getting_version;

        // TODO - not sure why we have to do this, but otherwise we don't get an interrupt when
        // the ACK is available. I can't see this in the spec.
        get_reader_status();

        return;
    }

    if (state.status == idle) {
        if (key_required) {
            scan_for_tag();
            state.status = waiting_for_tag;
        } else {
            return;
        }
    }

#ifdef INTERRUPT
    uint32_t saved_status = save_and_disable_interrupts();
    uint32_t pending = messages_pending;
    if (pending > 0) {
        messages_pending--;
    }
    restore_interrupts(saved_status);

    bool card_ready = pending > 0;
#else
    bool card_ready = get_reader_status() & 0x01;
#endif

    if (state.waiting_for_ack) {
        if (card_ready) {
            LOG_DEBUG("Ready for ack\n");
            state.waiting_for_ack = false;
            read_ack();
        }

        return;
    }

    if ((get_reader_status() & 0x01) == 0) {
        return;
    }

    LOG_DEBUG("Popped interrupt %lu\n", pending);

    switch (state.status) {
        case getting_version:
            if (!read_response(4) || response_data_size != 4) {
                LOG_ERROR("Failed to decode version data\n");
                state.unavailable = true;
                return;
            }

            if (response_data[0] != 0x32) {
                // Not a PN532.
                LOG_INFO("Unexpected version string: ");
                hex_dump(response_data, response_data_size);
                // Carry on regardless; hopefully compatible.
            }

            LOG_INFO("NFC PN532 version is %d.%d\n", response_data[1], response_data[2]);

            uint8_t args[] = {0x01, 0x14, 0x01};
            send_frame(PN532_COMMAND_SAMCONFIGURATION, args, sizeof(args));
            state.status = sending_config;

            break;

        case sending_config:
            if (!read_response(4) || response_data_size != 0) {
                LOG_ERROR("Failed to set SAMConfig\n");
                state.unavailable = true;
                return;
            }

            scan_for_tag();
            state.status = waiting_for_tag;

            break;

        case waiting_for_tag:
            if (!read_response(sizeof(read_passive_response_data_t))) {
                LOG_ERROR("Failed to read tag ID\n");
                state.unavailable = true;
                return;
            }

            read_passive_response_data_t *reply = (read_passive_response_data_t *) response_data;

            state.target_num = reply->target_num;
            state.id_length = reply->id_length;
            memset(state.id, '\0', sizeof(state.id));
            memcpy(state.id, reply->id, reply->id_length);

            LOG_INFO("Card ID: %s, target %d\n", id_as_string(), state.target_num);

            send_authentication();
            state.status = waiting_for_auth;

            break;

        case waiting_for_auth:
            if (!read_response(12)) {
                LOG_ERROR("Failed to auth tag\n");
                state.unavailable = true;
                return;
            }

            // TODO - validate auth was successful.
            printf("Auth\n");
            hex_dump(response_data, response_data_size);

            if (state.write_requested) {
                if (time_reached(state.write_timeout)) {
                    LOG_INFO("Ignoring stale write request\n");
                    state.write_requested = false;
                } else {
                    send_write_request(state.key, sizeof(state.key));
                    state.status = waiting_for_write;
                }
            } else {
                send_read_request();
                state.status = waiting_for_data;
            }

            break;

        case waiting_for_data:
            if (!read_response(1 + 16)) {
                LOG_ERROR("Failed to read tag\n");
                // TODO - back to scan
                state.unavailable = true;
                return;
            }

#ifdef DEBUG
            printf("Data\n");
            hex_dump(response_data, response_data_size);
#endif
            // TODO
            printf("Data: ");
            hex_dump(response_data, response_data_size);

            if (response_data[0] != 0) {
                LOG_ERROR("read failed:\n");
                hex_dump(response_data, response_data_size);
                state.status = idle;
                state.key_known = false;
                return;
            }

            state.status = idle;
            memcpy(state.key, response_data+1, response_data_size-1);
            state.key_read_time = get_absolute_time();
            state.key_known = true;

            break;

        case waiting_for_write:
            if (!read_response(1 + 16)) {
                LOG_ERROR("Failed to write tag\n");
                state.unavailable = true;
                return;
            }

            LOG_INFO("Write reply\n");
            hex_dump(response_data, response_data_size);

            // TODO - verify response.

            state.status = idle;

            break;

        default:
            LOG_ERROR("State == %d??\n", state.status);
            state.unavailable = true;
    }
}

bool send_write_request(uint8_t *data, size_t data_length) {
    // Maximum we can write to a block is 16 bytes.
    if (data_length > 16) {
        return false;
    }

    static uint8_t buff[3 + 16];
    buff[0] = state.target_num;
    buff[1] = 0xa0;   // 16-byte write.
    buff[2] = KEY_ADDRESS;

    memcpy(buff + 3, data, data_length);
    send_frame(PN532_COMMAND_INDATAEXCHANGE, buff, 3 + data_length);

    return true;
}

bool send_read_request() {
    uint8_t args[] = {
            state.target_num,
            0x30,   // 16-byte read.
            KEY_ADDRESS,
    };

    send_frame(PN532_COMMAND_INDATAEXCHANGE, args, sizeof(args));
    return true;
}

void send_authentication() {
    uint8_t args[] = {
            state.target_num,
            0x60,   // Authentication A
            KEY_ADDRESS,
            0xff,   // [6] byte Auth key
            0xff,
            0xff,
            0xff,
            0xff,
            0xff,
            state.id[0],
            state.id[1],
            state.id[2],
            state.id[3],
    };
    send_frame(PN532_COMMAND_INDATAEXCHANGE, args, sizeof(args));
}

void scan_for_tag() {
    uint8_t args[] = {
            0x01,   // Max targets to read at once.
            0x00    // Baud Rate. 0 => 106 kbps type A (ISO/IEC14443 Type A).
    };
    send_frame(PN532_COMMAND_INLISTPASSIVETARGET, args, sizeof(args));
}


uint8_t get_reader_status() {
    uint8_t card_status = 0;
    int read = i2c_read_timeout_us(i2c0, PN532_ADDRESS, &card_status, 1, false, 10000);
    if (read != 1) {
        return 0;
    }
    return card_status;
}

static void read_ack() {
    struct {
        uint8_t status;
        ack_t ack;
    } packet;
    int read = i2c_read_blocking(i2c0, PN532_ADDRESS, (uint8_t *) &packet, sizeof(packet), false);

    // check it is an ACK (not NACK) etc.
    if (read <= 0 || packet.status != 1 || packet.ack.ack_ff != 0xff || packet.ack.ack_00 != 0x00) {
        printf("Unexpected ACK packet\n");
        hex_dump(&packet, sizeof(packet));
        // TODO - better recovery.
        state.unavailable = true;
        return;
    }

    LOG_DEBUG("Got PN532 ACK\n");
}

void gpio_callback(uint gpio, uint32_t events) {
    interrupt_time = get_absolute_time();
    messages_pending++;
    if (messages_pending != 1) {
        // Should never get this, unless we have failed to "pop" an interrupt.
        // Have we changed to NACK commands?
        LOG_INFO("NFC interrupt. Pending now=%lu\n", messages_pending);
    }
}

static void send_frame(uint8_t command, uint8_t *data, uint8_t data_length) {
    create_frame(command, data, data_length);
    state.waiting_for_ack = true;
    int written = i2c_write_blocking(i2c0, PN532_ADDRESS, frame, frame_size, false);
    if (written == (int)frame_size) {
        LOG_INFO("Sent frame (cmd=0x%02x, data_length=%d, total_length=%d)\n", command, data_length, frame_size);
#ifdef DEBUG
        hex_dump(frame, frame_size);
#endif
        return;
    }

    state.waiting_for_ack = false;
    state.unavailable = true;
    LOG_ERROR("i2c_write failed(%d) returned %d\n", data_length, written);
}
