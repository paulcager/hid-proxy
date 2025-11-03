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
