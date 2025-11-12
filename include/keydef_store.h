/*
 * keydef_store.h
 *
 * Key definition storage using kvstore
 * Each keydef is stored as a separate key-value pair with key: keydef.0xHH
 * where HH is the HID keycode in hex (e.g., keydef.0x3A for F1)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hid_proxy.h"  // For keydef_t definition

/*! \brief Save a key definition to kvstore
 *
 * The keydef is stored with key "keydef.0xHH" where HH is the trigger HID code.
 * In Phase 3, all keydefs are encrypted (require_unlock is ignored).
 * In Phase 4, require_unlock determines if KVSTORE_REQUIRE_CONFIDENTIALITY_FLAG is used.
 *
 * \param keydef Pointer to keydef structure
 * \return true on success, false on failure
 */
bool keydef_save(const keydef_t *keydef);

/*! \brief Load a key definition from kvstore
 *
 * Allocates memory for the keydef and loads it from kvstore.
 * Caller must free() the returned pointer when done.
 *
 * \param trigger HID keycode (e.g., 0x3A for F1)
 * \return Pointer to allocated keydef, or NULL if not found or on error
 */
keydef_t *keydef_load(uint8_t trigger);

/*! \brief Delete a key definition from kvstore
 *
 * \param trigger HID keycode to delete
 * \return true on success, false on failure
 */
bool keydef_delete(uint8_t trigger);

/*! \brief List all available keydefs
 *
 * Returns an array of HID keycodes for which keydefs exist.
 * Respects device lock state - encrypted keydefs won't be listed if device is locked.
 *
 * \param triggers Buffer to receive HID keycodes
 * \param max_count Maximum number of entries in buffer
 * \return Number of keydefs found (may be less than max_count)
 */
size_t keydef_list(uint8_t *triggers, size_t max_count);

/*! \brief Get the size of a keydef in bytes
 *
 * Helper function to calculate total size including variable-length reports array.
 *
 * \param keydef Pointer to keydef structure
 * \return Total size in bytes
 */
static inline size_t keydef_size(const keydef_t *keydef) {
    return sizeof(keydef_t) + keydef->count * sizeof(hid_keyboard_report_t);
}

/*! \brief Allocate a new keydef with space for N reports
 *
 * Helper function to allocate a keydef structure with specified capacity.
 * Caller must free() the returned pointer when done.
 *
 * \param trigger HID keycode
 * \param count Number of HID reports to allocate space for
 * \return Pointer to allocated keydef, or NULL on allocation failure
 */
keydef_t *keydef_alloc(uint8_t trigger, uint16_t count);
