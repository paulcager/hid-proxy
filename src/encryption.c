//
// Encrypt / decrypt the storage.
//

#include "hid_proxy.h"
#include "encryption.h"

#define CTR 1
#define CBC 0
#define ECB 0
#include "tiny-AES-c/aes.h"

#include "pico/rand.h"
#include "pico/unique_id.h"
#include "tinycrypt/sha256.h"
#include "pbkdf-lite.h"

static uint8_t key[32];
static uint8_t password_buf[128];
static size_t password_len = 0;

void enc_add_password_byte(uint8_t b) {
    if (password_len < sizeof(password_buf)) {
        password_buf[password_len++] = b;
    }
}

void enc_clear_password() {
    memset(password_buf, 0, sizeof(password_buf));
    password_len = 0;
}

void enc_derive_key_from_password() {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);

    // Derive key from password buffer
    absolute_time_t start = get_absolute_time();
    derive_key(key, password_buf, password_len, id.id, PICO_UNIQUE_BOARD_ID_SIZE_BYTES);
    absolute_time_t end = get_absolute_time();
    LOG_INFO("derive_key took %lld μs (%ld millis)\n", to_us_since_boot(end) - to_us_since_boot(start), to_ms_since_boot(end) - to_ms_since_boot(start));

    // SECURITY: Immediately clear the plaintext password from memory after key derivation.
    // This must happen before any subsequent operations that might fail, to ensure the
    // password buffer doesn't persist in RAM if an error/panic occurs later in the caller.
    enc_clear_password();
}

void enc_set_key(uint8_t *data, size_t length) {
    assert(length <= sizeof(key));
    memset(key, 0, sizeof(key));
    memcpy(key, data, length);
}

void enc_get_key(uint8_t *data, size_t length) {
    assert(length <= sizeof(key));
    memcpy(data, key, length);
}

void enc_clear_key() {
    memset(key, 0, sizeof key);
    enc_clear_password();
}

bool store_encrypt(kb_t *kb) {
    store_t *s = kb->local_store;

    memcpy(s->magic, FLASH_STORE_MAGIC, sizeof(s->magic));
    memcpy(s->encrypted_magic, FLASH_STORE_MAGIC, sizeof(s->encrypted_magic));
    uint64_t rand = get_rand_64();
    memcpy(s->iv, (void*)&rand, 8);
    rand = get_rand_64();
    memcpy(s->iv + 8, (void*)&rand, 8);

    struct AES_ctx ctx;

    // TODO - we only need to encrypt *used* portion.
    AES_init_ctx_iv(&ctx, key, s->iv);
    AES_CTR_xcrypt_buffer(&ctx, (uint8_t *) s->encrypted_magic, FLASH_STORE_SIZE - offsetof(store_t, encrypted_magic));

    LOG_INFO("store_encrypt:\n");
    hex_dump(key, 32);

    return true;
}

bool store_decrypt(kb_t *kb) {
    store_t *s = kb->local_store;

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, s->iv);
    AES_CTR_xcrypt_buffer(&ctx, (uint8_t *) s->encrypted_magic, FLASH_STORE_SIZE - offsetof(store_t, encrypted_magic));

    bool ret = memcmp(s->magic, s->encrypted_magic, sizeof(s->magic)) == 0;
    LOG_INFO("After store_decrypt=%d\n", ret);
    assert_sane(kb);

    LOG_INFO("store_decrypt:\n");
    hex_dump(key, 32);


    return ret;
}