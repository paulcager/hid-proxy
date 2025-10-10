#include "pico_mocks.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

// Mock panic function that exits for testing
void panic(const char *fmt, ...) __attribute__((noreturn));

void panic(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "PANIC: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

// Mock flash storage - allocate 4KB for testing
// This simulates the flash sector size used in the real firmware
// Use a struct to ensure the symbols are contiguous in memory
struct {
    uint8_t storage[FLASH_STORE_SIZE_TEST];
    uint8_t end_marker;
} __attribute__((packed)) mock_flash;

// Alias the symbols to the struct members
uint8_t *__flash_storage_start = mock_flash.storage;
uint8_t *__flash_storage_end = &mock_flash.end_marker;
