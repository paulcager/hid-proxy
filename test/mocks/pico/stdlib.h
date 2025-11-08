// Mock pico/stdlib.h for host testing
#ifndef PICO_STDLIB_H
#define PICO_STDLIB_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Panic function (defined in pico_mocks.c)
void panic(const char *fmt, ...) __attribute__((noreturn));

#endif // PICO_STDLIB_H
