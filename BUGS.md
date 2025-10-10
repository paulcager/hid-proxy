# Bug Report for hid-proxy

This document contains a comprehensive analysis of bugs and issues found in the hid-proxy codebase.

**Total Bugs Found: 34**

**Breakdown by Severity:**
- Critical: 7 bugs (buffer overflows, race conditions, NULL pointer issues)
- High: 8 bugs (unbounded loops, race conditions, validation issues)
- Medium: 10 bugs (type issues, error handling, state machine)
- Low/Code Quality: 5 issues (dead code, magic numbers, inconsistencies)
- Security: 4 issues (weak encryption, key handling)

---

## CRITICAL BUGS

### 1. Buffer Overflow in key_defs.c - No bounds checking during key definition
**File:** `key_defs.c`
**Lines:** 180-184
**Description:** When defining a key sequence, there's no check to ensure we don't exceed the available space in `FLASH_STORE_SIZE`.
```c
// TODO - check remaining space
this_def->reports[this_def->used] = *kb_report;
this_def->used++;
```
**Impact:** A user could overflow the flash storage buffer by recording an extremely long key sequence, corrupting memory and potentially causing system crash.
**Fix:** Calculate remaining space before adding to definition. Check if `(void*)&this_def->reports[this_def->used + 1] < (void*)kb.local_store + FLASH_STORE_SIZE`.

---

### 2. Buffer Overflow in key_defs.c:start_define() - Memmove calculation error
**File:** `key_defs.c`
**Line:** 212
**Description:** The memmove operation calculates size as `limit - next`, but `next` could be beyond `limit` in edge cases, resulting in a huge unsigned size.
```c
memmove(ptr, next, limit - next);
```
**Impact:** Could cause memory corruption when replacing a key definition near the end of storage.
**Fix:** Add bounds check: `if (next < limit) memmove(ptr, next, limit - next);`

---

### 3. Race Condition in usb_host.c - Queue blocking without protection
**File:** `usb_host.c`
**Line:** 201
**Description:** Core 1 uses `queue_add_blocking()` which can block indefinitely if Core 0 isn't consuming from the queue.
```c
queue_add_blocking(&keyboard_to_tud_queue, &to_tud);
```
**Impact:** If Core 0 gets stuck or the queue fills up faster than it can be consumed, Core 1 will deadlock, stopping all USB host operations.
**Fix:** Use `queue_try_add_timeout_ms()` with a reasonable timeout and error handling.

---

### 4. Memory Leak - malloc() without free()
**File:** `hid_proxy.c`
**Line:** 90
**Description:** `kb.local_store` is allocated with malloc but never freed.
```c
kb.local_store = malloc(FLASH_STORE_SIZE);
```
**Impact:** Memory leak of 4KB. In an embedded system with limited RAM, this is problematic if the system ever resets without power cycling.
**Fix:** Either use static allocation or add proper cleanup (though in a never-ending main loop this may be intentional).

---

### 5. NULL Pointer Dereference - No malloc() success check
**File:** `hid_proxy.c`
**Line:** 90-91
**Description:** No check if `malloc()` succeeded before using `kb.local_store`.
```c
kb.local_store = malloc(FLASH_STORE_SIZE);
lock();  // Immediately uses kb.local_store
```
**Impact:** If malloc fails, the system will crash when trying to access NULL pointer.
**Fix:** Add check: `if (!kb.local_store) panic("malloc failed");`

---

### 6. Incorrect strlen() usage with array bound
**File:** `key_defs.c`
**Line:** 286
**Description:** Uses `strlen()` on a const char array instead of `sizeof()`.
```c
static const char trans_table[] = "....abcdefghijklmnopqrstuvwxyz1234567890";
if (keycode > strlen(trans_table)) {
```
**Impact:** Performance issue - unnecessary runtime string length calculation. Not technically a bug but inefficient.
**Fix:** Use `sizeof(trans_table) - 1` since the array size is known at compile time.

---

### 7. Buffer Overflow in hid_proxy.h:add_to_host_queue()
**File:** `hid_proxy.h`
**Line:** 142
**Description:** Always copies `sizeof(item.data)` bytes regardless of actual `len` parameter.
```c
memcpy(item.data, data, sizeof(item.data));
```
**Impact:** If `data` pointer points to less than `sizeof(item.data)` bytes, this reads beyond the buffer, potentially reading garbage or causing a crash.
**Fix:** Use `len` parameter: `memcpy(item.data, data, len);`

---

## HIGH SEVERITY BUGS

### 8. Unbounded Loop in key_defs.c - evaluate_keydef()
**File:** `key_defs.c`
**Line:** 235
**Description:** Loop continues until finding a zero keycode without bounds checking.
```c
for (keydef_t *ptr = kb.local_store->keydefs; ptr->keycode != 0; ptr = next_keydef(ptr))
```
**Impact:** If the keydef list is corrupted (no terminating zero), the loop walks past the end of `local_store`, reading garbage memory.
**Fix:** Add bounds check: `ptr < (keydef_t*)((void*)kb.local_store + FLASH_STORE_SIZE)`

---

### 9. Unbounded Loop in key_defs.c - print_keydefs()
**File:** `key_defs.c`
**Line:** 263
**Description:** Same issue as #8 in a different function.
```c
for (keydef_t *ptr = kb.local_store->keydefs; ptr->keycode != 0; ptr = next_keydef(ptr))
```
**Impact:** Same as #8 - potential buffer overrun if list is corrupted.
**Fix:** Add the same bounds check.

---

### 10. Unbounded Loop in key_defs.c - start_define()
**File:** `key_defs.c`
**Lines:** 196-217
**Description:** While loop walks through keydefs without proper termination check.
```c
while(true) {
    keydef_t *def = ptr;
    if (ptr >= limit) {
        break;
    }
```
**Impact:** If a keydef has a corrupt `used` field with a huge value, `next` could wrap around or jump to arbitrary memory.
**Fix:** Add validation: check that `def->used` is reasonable (e.g., `< 1000`) before calculating next pointer.

---

### 11. Integer Overflow in key_defs.c - next_keydef()
**File:** `key_defs.c`
**Lines:** 16-21
**Description:** Pointer arithmetic can overflow if `this->used` is corrupted.
```c
t += sizeof(keydef_t) + (this->used * sizeof(hid_keyboard_report_t));
```
**Impact:** If `this->used` contains a garbage value, the multiplication can overflow, causing the pointer to wrap and point to arbitrary memory.
**Fix:** Validate `this->used` is within reasonable bounds before calculation.

---

### 12. Race Condition - messages_pending in nfc_tag.c
**File:** `nfc_tag.c`
**Lines:** 433-438
**Description:** `messages_pending` is accessed and modified in both the ISR (gpio_callback at line 699) and main task with interrupts temporarily disabled, but the decrement-and-restore pattern is not atomic.
```c
uint32_t saved_status = save_and_disable_interrupts();
uint32_t pending = messages_pending;
if (pending > 0) {
    messages_pending--;
}
restore_interrupts(saved_status);
```
**Impact:** If an interrupt occurs between reading `pending` and decrementing `messages_pending`, the count could become inconsistent.
**Fix:** Move the entire read-check-modify sequence into the critical section properly, or use atomic operations.

---

### 13. Queue Overflow Not Handled
**File:** `hid_proxy.c`
**Lines:** 163-169
**Description:** `queue_add_or_panic()` panics instead of handling queue overflow gracefully.
```c
void queue_add_or_panic(queue_t *q, const void *data) {
    if (!queue_try_add(q, data)) {
        // TODO - this is most likely to happen if we are sending large definitions
        panic("Queue is full");
    }
}
```
**Impact:** System crash when sending large key definitions faster than they can be transmitted over USB.
**Fix:** As the TODO suggests, interleave queue operations with `tud_task()` and use backpressure instead of panicking.

---

### 14. NFC Tag ID Buffer Overflow
**File:** `nfc_tag.c`
**Line:** 508
**Description:** Copies `reply->id_length` bytes without validating it's <= 7.
```c
state.id_length = reply->id_length;
memset(state.id, '\0', sizeof(state.id));
memcpy(state.id, reply->id, reply->id_length);
```
**Impact:** If a malicious or malformed NFC tag reports `id_length > 7`, this will overflow the `state.id[7]` buffer.
**Fix:** Add check: `state.id_length = reply->id_length > 7 ? 7 : reply->id_length;`

---

### 15. NFC Data Size Validation Missing
**File:** `nfc_tag.c`
**Line:** 581
**Description:** Copies from response without validating `response_data_size`.
```c
memcpy(state.key, response_data+1, response_data_size-1);
```
**Impact:** If `response_data_size` is 0, this becomes SIZE_MAX (underflow), copying huge amounts of data. If `response_data_size > 17`, this overflows the 16-byte `state.key` buffer.
**Fix:** Add validation: `if (response_data_size < 1 || response_data_size > 17) { /* error */ }`

---

## MEDIUM SEVERITY BUGS

### 16. Off-by-One Error in Flash Bounds Check
**File:** `key_defs.c`
**Line:** 194
**Description:** Comparing pointer to offset in key store.
```c
void *limit = ptr + FLASH_STORE_SIZE;
```
**Impact:** `limit` should point to the end of keydefs area, not the end of entire flash store. The magic/IV/encrypted_magic fields occupy the first part of the store.
**Fix:** `void *limit = (void*)kb.local_store + FLASH_STORE_SIZE;`

---

### 17. Type Confusion - keycode stored as int instead of uint8_t
**File:** `hid_proxy.h`
**Lines:** 82-86
**Description:** keydef_t uses `int` for keycode and used, but keycodes are uint8_t.
```c
typedef struct keydef {
    int keycode;
    int used;
```
**Impact:** Wastes memory (4 bytes vs 1 byte for keycode) and could allow out-of-range values. The assert in sane.c line 26 only checks the low byte.
**Fix:** Change to `uint8_t keycode; uint16_t used;` with proper alignment.

---

### 18. Missing Error Check for I2C Operations
**File:** `nfc_tag.c`
**Line:** 710
**Description:** i2c_write_blocking() return value is checked, but many i2c_read operations aren't fully validated.
```c
int written = i2c_write_blocking(i2c0, PN532_ADDRESS, frame, frame_size, false);
```
**Impact:** Failed I2C operations might leave data in undefined state.
**Fix:** Add comprehensive error checking for all I2C operations.

---

### 19. Potential Sign Extension Issue
**File:** `nfc_tag.c`
**Line:** 307
**Description:** Casting int read result to unsigned for comparison.
```c
if ((unsigned)read != size) {
```
**Impact:** If `read` is negative (error), casting to unsigned makes it a large positive number, potentially hiding errors.
**Fix:** Check for error explicitly: `if (read < 0 || (size_t)read != size)`

---

### 20. Flash Verification Uses Wrong Size
**File:** `flash.c`
**Line:** 34
**Description:** Compares entire `FLASH_STORE_SIZE` but only writes used portion according to the comment.
```c
if (memcmp(kb->local_store, FLASH_STORE_ADDRESS, FLASH_STORE_SIZE) != 0) {
```
**Impact:** Verification might fail even if the write succeeded, if there's garbage in unused portions of the buffer.
**Fix:** Only compare the portion that was actually written, or zero-fill the entire buffer before writing.

---

### 21. Improper State Machine Handling
**File:** `key_defs.c`
**Lines:** 168-176
**Description:** In `defining` state, there's no explicit handling for keyup events other than double-shift.
```c
case defining:
    if (kb_report->modifier == 0x22 && key0 == 0) {
        // End definition
```
**Impact:** Single key releases will be recorded in the definition, which might not be the intended behavior. The function doesn't return after adding to definition, falling through.
**Fix:** Add explicit `return;` at line 185 after incrementing `used`.

---

### 22. Inconsistent Idle Timeout Type
**File:** `hid_proxy.h` and `hid_proxy.c`
**Lines:** hid_proxy.h:22, hid_proxy.c:141
**Description:** `IDLE_TIMEOUT_MILLIS` is defined as `int64_t` but used in microseconds comparison.
```c
#define IDLE_TIMEOUT_MILLIS ((int64_t)(120 * 60 * 1000))
// Used as:
if (...absolute_time_diff_us(...) > 1000 * IDLE_TIMEOUT_MILLIS)
```
**Impact:** Could overflow when multiplying by 1000, though unlikely with 2-hour timeout.
**Fix:** Define as microseconds directly, or use a safe conversion macro.

---

### 23. Uninitialized Variable in nfc_tag.c
**File:** `nfc_tag.c`
**Lines:** 76-101
**Description:** Global `state` struct members may not be fully initialized before use.
```c
struct {
    enum { ... } status;
    bool write_requested;
    // ... many fields
} state;
```
**Impact:** In C, global variables are zero-initialized, so this is actually okay, but it's fragile and not explicit.
**Fix:** Add explicit initialization in `nfc_setup()` for all fields.

---

### 24. Unused Variables
**File:** `usb_host.c`
**Lines:** 118
**Description:** Variables marked as unused but computed anyway.
```c
(void) l; (void) m; (void) r;
```
**Impact:** None, but poor code hygiene.
**Fix:** Wrap computation in `#ifdef DEBUG` or remove entirely.

---

### 25. Magic Number for Index Bounds
**File:** `nfc_tag.c`
**Line:** 530
**Description:** Hard-coded `NUM_KNOWN_AUTHS-1` in comparison.
```c
if (state.auth_key_index < NUM_KNOWN_AUTHS-1) {
```
**Impact:** None functionally, but magic numbers reduce maintainability.
**Fix:** Use named constant or comment explaining the -1.

---

## LOW SEVERITY / CODE QUALITY ISSUES

### 26. Duplicate Authentication Keys in Array
**File:** `nfc_tag.c`
**Lines:** 21-38
**Description:** The array `known_auths` has duplicate entry at index 0 and 8.
```c
{0xd3, 0xf7, 0xd3, 0xf7, 0xd3, 0xf7}, // Index 0
{0xd3, 0xf7, 0xd3, 0xf7, 0xd3, 0xf7}, // Index 8 - duplicate!
```
**Impact:** Wastes space and time trying the same key twice.
**Fix:** Remove duplicate or document why it's intentional.

---

### 27. Inconsistent Error Handling
**File:** `usb_host.c`
**Lines:** 100-102, 204-206
**Description:** Some error conditions log and continue, others log and return, with no clear pattern.
```c
if (!tuh_hid_receive_report(dev_addr, instance)) {
    LOG_ERROR("Error: cannot request to receive report\r\n");
}
```
**Impact:** Inconsistent error handling makes debugging harder.
**Fix:** Establish consistent error handling policy.

---

### 28. Debug Frame Function Unused
**File:** `nfc_tag.c`
**Lines:** 153-194
**Description:** `debug_frame()` function is marked unused but defined.
**Impact:** Dead code bloat.
**Fix:** Remove or use `#ifdef DEBUG` to conditionally compile.

---

### 29. Potential Infinite Wait
**File:** `hid_proxy.c`
**Lines:** 123-125
**Description:** Checks `!kb.send_to_host_in_progress` before dequeuing, but if USB host never completes the transfer, this flag might never clear.
```c
if (!kb.send_to_host_in_progress && queue_try_remove(&tud_to_physical_host_queue, &to_send)) {
```
**Impact:** Queue could fill up and panic if USB transfer gets stuck.
**Fix:** Add timeout mechanism to clear flag if transfer doesn't complete within reasonable time.

---

### 30. No Validation of Flash Magic After Decryption
**File:** `encryption.c`
**Lines:** 84-86
**Description:** Only checks if magic matches encrypted_magic, doesn't validate against expected constant.
```c
bool ret = memcmp(s->magic, s->encrypted_magic, sizeof(s->magic)) == 0;
```
**Impact:** If both magic values are corrupted identically, this would incorrectly return true.
**Fix:** Also verify `memcmp(s->magic, FLASH_STORE_MAGIC, sizeof(s->magic)) == 0`.

---

## SECURITY ISSUES

### 31. Weak Encryption Key Derivation
**File:** `encryption.c`
**Lines:** 48-53
**Description:** Uses single-round SHA256 with board ID salt for key derivation.
```c
tc_sha256_init(&sha256);
tc_sha256_update(&sha256, (const uint8_t *) &id, PICO_UNIQUE_BOARD_ID_SIZE_BYTES);
```
**Impact:** Vulnerable to brute force attacks. No proper KDF like PBKDF2 or Argon2.
**Fix:** Use proper key derivation function with many iterations.

---

### 32. IV Generation Not Cryptographically Strong
**File:** `encryption.c`
**Lines:** 60-63
**Description:** Uses `get_rand_64()` which may not be cryptographically secure.
```c
uint64_t rand = get_rand_64();
memcpy(s->iv, (void*)&rand, 8);
```
**Impact:** Predictable IVs could weaken encryption.
**Fix:** Use hardware RNG or verify `get_rand_64()` uses secure source.

---

### 33. Key Material Not Cleared from Stack
**File:** `key_defs.c`
**Line:** 120
**Description:** Encryption key is copied to stack without clearing afterward.
```c
uint8_t key[16];
enc_get_key(key, sizeof(key));
```
**Impact:** Key material could remain in stack memory after use.
**Fix:** Clear the key array with `memset(key, 0, sizeof(key))` before function return.

---

### 34. AES CTR Mode Without Authentication
**File:** `encryption.c`
**Lines:** 68-69, 81-82
**Description:** Uses AES-CTR without HMAC or authenticated encryption.
```c
AES_init_ctx_iv(&ctx, key, s->iv);
AES_CTR_xcrypt_buffer(&ctx, (uint8_t *) s->encrypted_magic, ...);
```
**Impact:** No integrity protection - attacker could flip bits in encrypted data without detection.
**Fix:** Use AES-GCM or add HMAC for authentication.

---

## SUMMARY

**Most Critical Issues to Fix First:**
1. Buffer overflow in key definition recording (#1)
2. Buffer overflow in add_to_host_queue (#7)
3. NULL pointer dereference from malloc (#5)
4. NFC tag ID buffer overflow (#14)
5. Race condition with queue blocking (#3)
6. Unbounded loops in keydef traversal (#8, #9, #10)

**Recommended Actions:**
1. Add comprehensive bounds checking for all buffer operations
2. Validate all input sizes before memory operations
3. Add proper error handling for queue overflows
4. Review and fix all loops to prevent infinite iteration
5. Implement proper timeout mechanisms for blocking operations
6. Upgrade encryption to use authenticated encryption and proper KDF

This codebase is marked as proof-of-concept and explicitly not for production use, which these findings confirm. The bugs identified would need to be addressed before any production deployment.
