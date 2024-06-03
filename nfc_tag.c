
#include "nfc_tag.h"
//#include <Adafruit_PN532.h>
#include <pico/time.h>
#include <pico/printf.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <pico/binary_info/code.h>
#include "pn532-lib/pn532_rp2040.h"
#include "hid_proxy.h"

#define I2C_SDA      4
#define I2C_SCL      5
#define INTERRUPT   15
#define PN532_ADDRESS           0x24
#define HOST_TO_PN532           0xD4
#define PN532_TO_HOST           0xD5


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

volatile struct {
    enum {
        idle = 0,
        getting_version,
        sending_config,
        waiting_for_tag,
        waiting_for_auth,
        waiting_for_data,
    } status;
    bool waiting_for_ack;
    bool unavailable;        // No PN32, or given up because of errors.
    uint8_t target_num;
    uint8_t id_length;
    uint8_t id[8];
} state;


static uint8_t frame[sizeof(frame_header_t) + 0x00ff +  sizeof(frame_trailer_t)];
static uint frame_size;

static uint8_t response[1 + sizeof(frame_header_t) + 0x00ff +  sizeof(frame_trailer_t)];
static uint8_t *response_data;
static uint8_t response_data_size;

static volatile bool seen_interrupt;
static volatile absolute_time_t interrupt_time;

static void gpio_callback(uint gpio, uint32_t events);
static void read_ack();
static int read_data(uint8_t *buff, uint8_t size);
static void send_frame(uint8_t command, uint8_t *data, uint8_t data_length);
static bool decode_frame(uint8_t* input_frame, uint8_t frame_length, uint8_t **data, uint8_t *data_length);
static uint8_t get_reader_status();


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
        memcpy(frame + sizeof(frame_header_t), data, data_length);
    }

    uint8_t cksum = 0xff + PN532_HOSTTOPN532 + command;
    for (int i = 0; i < data_length; i++) {
        cksum += data[i];
    }
    trailer->cksum = ~cksum & 0xff;
    trailer->postamble = 0;

    frame_size = sizeof(frame_header_t) + data_length + sizeof(frame_trailer_t);
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
        printf("Short read. Wanted %d got %d\n", size, read);
        hex_dump(response, read);
    }

    return decode_frame(response + 1, read - 1, &response_data, &response_data_size);
}

static bool decode_frame(uint8_t* input_frame, uint8_t frame_length, uint8_t **data, uint8_t *data_length) {
    if (frame_length < (sizeof(frame_header_t) + sizeof(frame_trailer_t))) {
        hex_dump(input_frame, frame_length);
        return false;
    }

    frame_header_t *hdr = (frame_header_t *) input_frame;
    if (hdr->preamble != 0 || hdr->start_code_00 != 0x00 || hdr->start_code_FF != 0xff || hdr->host != PN532_TO_HOST) {
        printf("Invalid header: preamble: %02x, start_code=%02x%02x, host=%02x\n", hdr->preamble, hdr->start_code_00, hdr->start_code_FF, hdr->host);
        hex_dump(input_frame, frame_length);
        return false;
    }
    // TODO - also check response's command is request's command + 1.

    if (((~hdr->command_length + 1) & 0xff) != hdr->command_length_cksum) {
        printf("Invalid cksum: %0x != %0x\n", ((~hdr->command_length + 1) & 0xff), hdr->command_length_cksum);
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
    state.status = idle;
    state.unavailable = false;

    // Ping I2C to detect the board.
    int ret;
    uint8_t rxdata;
    ret = i2c_read_timeout_us(i2c_default, PN532_ADDRESS, &rxdata, 1, false, 50*1000);
    if (ret < 0) {
        printf("Could not find device on I2C address %d (error code=%d). NFC will be disabled.\n", PN532_ADDRESS, ret);
        state.unavailable = true;
    }

    // Maybe we should send an ACK here in case the PN532 is busy.
}



void nfc_task() {
    static int previous_status = -1;
    if (previous_status != state.status) {
        printf("Status changed from %d to %d\n", previous_status, state.status);
        previous_status = state.status;
    }

    if (state.unavailable) {
        return;
    }

    if (i2c_get_read_available(i2c0) != 0) {
        printf("state = %d, avail = %d\n", state.status, i2c_get_read_available(i2c0));
    }

    bool card_ready = get_reader_status() & 0x01;

    if (state.waiting_for_ack) {
        if (card_ready) {
            state.waiting_for_ack = false;
            seen_interrupt = false;
            read_ack();
        }

        return;
    }

    switch (state.status) {
        case idle:
            send_frame(PN532_COMMAND_GETFIRMWAREVERSION, NULL, 0);
            assert(state.waiting_for_ack);
            state.status = getting_version;
            break;
        case getting_version:
            if (card_ready) {
                if (!read_response(4) || response_data_size != 4) {
                    printf("Failed to decode version data\n");
                    state.unavailable = true;
                    return;
                }

                if (response_data[0] != 0x32) {
                    // Not a PN532.
                    printf("Unexpected version string: ");
                    hex_dump(response_data, response_data_size);
                    // Carry on regardless; hopefully compatible.
                }

                printf("Version is %d.%d\n", response_data[1], response_data[2]);

                uint8_t args[] = {0x01, 0x14, 0x01};
                send_frame(PN532_COMMAND_SAMCONFIGURATION, args, sizeof(args));
                state.status = sending_config;
            }
            break;

        case sending_config:
            if (card_ready) {
                if (!read_response(4) || response_data_size != 0) {
                    printf("Failed to set SAMConfig\n");
                    state.unavailable = true;
                    return;
                }

                uint8_t args[] = {
                        0x01,   // Max targets to read at once.
                        0x00    // Baud Rate. 0 => 106 kbps type A (ISO/IEC14443 Type A).
                };
                send_frame(PN532_COMMAND_INLISTPASSIVETARGET, args, sizeof(args));
                state.status = waiting_for_tag;
            }
            break;

        case waiting_for_tag:
            if (card_ready) {
                if (!read_response(sizeof(read_passive_response_data_t))) {
                    state.unavailable = true;
                    return;
                }

                read_passive_response_data_t *reply = (read_passive_response_data_t *) response_data;

                state.target_num = reply->target_num;
                state.id_length = reply->id_length;
                memcpy(state.id, reply->id, reply->id_length);

                printf("Card ID is\n");
                hex_dump(state.id, state.id_length);

                uint8_t args[] = {
                        state.target_num,
                        0x60,   // Authentication A
                        0x04,   // Sector
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
                state.status = waiting_for_auth;
            }
            break;

            case waiting_for_auth:
                if (card_ready) {
                    if (!read_response(12)) {
                        printf("Failed to auth tag\n");
                        state.unavailable = true;
                        return;
                    }

                    printf("Auth\n");
                    hex_dump(response_data, response_data_size);

                    uint8_t args[] = {
                            state.target_num,
                            0x60,   // Authentication A
                            0x04,   // Sector.
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
//                    send_frame(PN532_COMMAND_INDATAEXCHANGE, args, sizeof(args));
//                    state.status = waiting_for_data;
                    state.unavailable= true;
                }
            break;

        case waiting_for_data:
            break;

        default:
            printf("State == %d??\n", state.status);
            state.unavailable = true;
    }
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

    printf("Got ack\n");
}

void gpio_callback(uint gpio, uint32_t events) {
    interrupt_time = get_absolute_time();
    printf("Interrupt on GPIO %d [%04lx]\n", gpio, events);
    seen_interrupt = true;
}

void try_nfc() {
    sleep_ms(10);
    uint8_t command = PN532_COMMAND_GETFIRMWAREVERSION;
    uint8_t *data = NULL;
    uint8_t data_length = 0;

    send_frame(command, data, data_length);

    uint8_t reply[44];
    int reads[4];
    for (int i = 0; i < 4; i++) {
        reads[i] = i2c_read_blocking(i2c0, PN532_ADDRESS, reply, sizeof(reply), false);
        if (reads[i] > 0) {
            printf("Read %d returns %d (interrupt: %d)\n", i, reads[i], seen_interrupt);
            hex_dump(reply, reads[i]);
        }
//        reads[i] = to_us_since_boot(get_absolute_time());
    }
    for (int i = 0; i < 4; i++) {
        printf("%d: %d\n", i, reads[i]);
    }
    //printf("Interrupt arrived after %lld us\n", absolute_time_diff_us(write_timestamp, interrupt_time));

//    int read = i2c_read_blocking(i2c0, PN532_ADDRESS, reply, 1, false);
//    printf("First read returns %d (interrupt: %d)\n", read, seen_interrupt);
//    if (read > 0) {
//        hex_dump(reply, read);
//    }

    // Wait for ack.
    for (int i = 0; seen_interrupt == false && i < 10; i++) {
        // Wait for it.
        sleep_ms(1);
    }

    int read = i2c_read_blocking(i2c0, PN532_ADDRESS, reply, sizeof(reply), false);
//    int read = i2c_read_blocking(i2c0, PN532_ADDRESS, reply, 1, false);
    printf("Read returns %d (interrupt: %d)\n", read, seen_interrupt);
    if (read > 0) {
        hex_dump(reply, read);
    }

    PN532 dev;
    PN532_I2C_Init(&dev);
    uint8_t version = 255;
    int ret = PN532_GetFirmwareVersion(&dev, &version);
    printf("PN532_GetFirmwareVersion returns %d and version=%d\n", ret, version);

    PN532_SamConfiguration(&dev);


    while (1)
    {
        printf("Waiting for RFID/NFC card...\r\n");
        // Check if a card is available to read
        uint8_t uid[MIFARE_UID_MAX_LENGTH];
        int uid_len = PN532_ReadPassiveTarget(&dev, uid, PN532_MIFARE_ISO14443A, 1000000);
        printf("PN532_ReadPassiveTarget returned %d\n", uid_len);
        if (uid_len == PN532_STATUS_ERROR) {
        } else {
            printf("Found card with UID: ");
            for (uint8_t i = 0; i < uid_len; i++) {
                printf("%02x ", uid[i]);
            }
            printf("\r\n");
            break;
        }
        sleep_ms(200);
    }

    uint8_t block[16];
    memset(block, 0, 16);
    ret = PN532_MifareClassicReadBlock(&dev, block, 4);
    printf("PN532_MifareClassicReadBlock returns %d and block: ", ret);
    hex_dump(block, 16);
    sleep_ms(1000);
}

static void send_frame(uint8_t command, uint8_t *data, uint8_t data_length) {
    create_frame(command, data, data_length);
    seen_interrupt = false;

    state.waiting_for_ack = true;
    int written = i2c_write_blocking(i2c0, PN532_ADDRESS, frame, frame_size, false);
    if (written == (int)frame_size) {
        printf("Sent frame:\n");
        hex_dump(frame, frame_size);
        return;
    }

    state.waiting_for_ack = false;
    state.unavailable = true;
    printf("i2c_write failed(%d) returned %d\n", data_length, written);

    char card_status;
    int read = i2c_read_blocking(i2c0, PN532_ADDRESS, &card_status, 1, false);
}
