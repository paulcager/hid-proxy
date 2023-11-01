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
    AES_init_ctx_iv(&ctx, kb->encryption_key, s->iv);
    AES_CTR_xcrypt_buffer(&ctx, (uint8_t *) s->encrypted_magic, FLASH_STORE_SIZE - offsetof(store_t, encrypted_magic));

    return true;
}

bool store_decrypt(kb_t *kb) {
    store_t *s = kb->local_store;

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, kb->encryption_key, s->iv);
    AES_CTR_xcrypt_buffer(&ctx, (uint8_t *) s->encrypted_magic, FLASH_STORE_SIZE - offsetof(store_t, encrypted_magic));

    bool ret = memcmp(s->magic, s->encrypted_magic, sizeof(s->magic)) == 0;
    LOG_INFO("After store_decrypt=%d\n", ret);
    assert_sane(kb);

    return ret;
}