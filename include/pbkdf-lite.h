#ifndef PBKDF_LITE_H
#define PBKDF_LITE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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
void derive_key(uint8_t *out_key,
                const uint8_t *password, size_t pw_len,
                const uint8_t *board_id, size_t id_len);

#ifdef __cplusplus
}
#endif

#endif // PBKDF_LITE_H
