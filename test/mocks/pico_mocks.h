// Mock Pico SDK functions for host testing
#ifndef PICO_MOCKS_H
#define PICO_MOCKS_H

#include <stdint.h>
#include <stdarg.h>

// Mock panic function
void panic(const char *fmt, ...);

// Mock flash storage symbols for testing
// In the real firmware these are defined by the linker script
#define FLASH_STORE_SIZE_TEST 4096

// These are pointers in the test environment (but arrays in firmware)
// Both forms work with pointer arithmetic in C
extern uint8_t *__flash_storage_start;
extern uint8_t *__flash_storage_end;

#endif // PICO_MOCKS_H
