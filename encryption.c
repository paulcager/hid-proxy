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

static struct tc_sha256_state_struct sha256;

static uint8_t key[32];

void enc_clear_key() {
    memset(key, 0, sizeof key);
    tc_sha256_init(&sha256);
}

void enc_end_key_derivation() {
    tc_sha256_final(key, &sha256);
    hex_dump(key, 32);
}

void enc_add_key_derivation_byte(uint8_t b) {
    tc_sha256_update(&sha256, &b, 1);
}

void enc_start_key_derivation() {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    tc_sha256_init(&sha256);
    tc_sha256_update(&sha256, &id, PICO_UNIQUE_BOARD_ID_SIZE_BYTES);
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
    hex_dump(key, 16);

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
    hex_dump(key, 16);


    return ret;
}