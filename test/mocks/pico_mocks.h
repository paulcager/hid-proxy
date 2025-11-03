// Mock Pico SDK functions for host testing
#ifndef PICO_MOCKS_H
#define PICO_MOCKS_H

#include <stdint.h>
#include <stdarg.h>

// Mock panic function
void panic(const char *fmt, ...);

#endif // PICO_MOCKS_H
