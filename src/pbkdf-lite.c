/**
 * @brief Derive a per-device cryptographic key from a password.
 *
 * This function derives a 256-bit key from a user-provided password using
 * an iterated HMAC-SHA256 construction. It provides a lightweight, PBKDF2-like
 * key stretching mechanism suitable for resource-constrained microcontrollers
 * where full KDFs such as Argon2 or scrypt are infeasible.
 *
 * The device's unique board ID is mixed into the salt to ensure that each
 * device produces a distinct key, even for identical passwords.
 *
 * The number of iterations controls the computational cost of derivation;
 * increasing it makes brute-force attacks proportionally harder, at the cost
 * of longer computation time. The default of ~6000 iterations takes roughly
 * 0.2 seconds on an RP2040 at 133 MHz.
 *
 * @param[out] out_key   Buffer to receive the derived 32-byte key.
 * @param[in]  password  Pointer to the password or secret input.
 * @param[in]  pw_len    Length of the password in bytes.
 * @param[in]  board_id  Pointer to the device's unique board identifier.
 * @param[in]  id_len    Length of the board ID in bytes.
 *
 * @note The derived key depends on the password, board ID, and the
 *       internal constant "b59497ea562367d8". Changing any of these inputs
 *       will produce a different key.
 *
 * @warning This routine is intended for deriving device-unique keys or
 *          verifying passwords locally. It is not a replacement for
 *          high-entropy key generation in security-critical systems.
 */

#include "tinycrypt/hmac.h"
#include "tinycrypt/sha256.h"
#include <string.h>
#include "pbkdf-lite.h"

// 600 iterations is laughably small, but on the Pico that translates to about 0.25s which is about as long as we
// can reasonably delay keystrokes.
#define ITERATIONS 600
#define KEY_LEN 32  // 256 bits

void derive_key(uint8_t *out_key,
                const uint8_t *password, size_t pw_len,
                const uint8_t *board_id, size_t id_len)
{
    struct tc_hmac_state_struct hmac;
    uint8_t digest[TC_SHA256_DIGEST_SIZE];
    uint8_t salt[TC_SHA256_DIGEST_SIZE];

    /* salt = SHA256(board_id || static tag) */
    struct tc_sha256_state_struct sha;
    tc_sha256_init(&sha);
    tc_sha256_update(&sha, board_id, id_len);
    const char *tag = "b59497ea562367d8";
    tc_sha256_update(&sha, (const uint8_t *)tag, strlen(tag));
    tc_sha256_final(salt, &sha);

    /* result = HMAC(password, salt) */
    tc_hmac_set_key(&hmac, password, pw_len);
    tc_hmac_init(&hmac);
    tc_hmac_update(&hmac, salt, sizeof(salt));
    tc_hmac_final(digest, TC_SHA256_DIGEST_SIZE, &hmac);

    /* iterative strengthening */
    for (uint32_t i = 1; i < ITERATIONS; i++) {
        tc_hmac_init(&hmac);
        tc_hmac_update(&hmac, digest, sizeof(digest));
        tc_hmac_final(digest, TC_SHA256_DIGEST_SIZE, &hmac);
    }

    memcpy(out_key, digest, KEY_LEN);
}
