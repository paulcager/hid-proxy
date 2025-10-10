# Unit Tests

This directory contains unit tests for the hid-proxy project using the [Unity](https://github.com/ThrowTheSwitch/Unity) testing framework.

## Running Tests

Tests run on your host machine (not on the Pico), so they're fast and don't require hardware:

```bash
# From project root
./test.sh

# Clean build
./test.sh --clean

# Verbose output
./test.sh --verbose
```

## Test Structure

```
test/
├── test_macros.c      # Tests for macro parser/serializer
├── mocks/             # Mock Pico SDK types for host testing
│   ├── pico_mocks.h
│   └── pico_mocks.c
├── CMakeLists.txt     # Test build configuration
└── README.md          # This file
```

## Current Tests

### test_macros.c

Comprehensive tests for the macro parser and serializer:

- **Parsing:**
  - Simple text macros (`a { "Hello" }`)
  - Mnemonic triggers (`F1`, `ENTER`, etc.)
  - Hex triggers (`0x04`)
  - Ctrl shorthand (`^C`, `^V`)
  - Explicit HID reports (`[01:06]`)
  - Mixed commands
  - Comments and whitespace
  - Escaped quotes
  - Multiple keydefs

- **Serialization:**
  - Text output
  - Ctrl shorthand generation
  - Round-trip (parse → serialize → parse)

Total: 13 tests

## Adding New Tests

1. Create a new test file in `test/` (e.g., `test_encryption.c`)
2. Include Unity header: `#include "unity.h"`
3. Write test functions:
   ```c
   void test_something(void) {
       TEST_ASSERT_EQUAL(expected, actual);
   }
   ```
4. Add to `test/CMakeLists.txt`:
   ```cmake
   add_executable(test_encryption
       test_encryption.c
       ${CMAKE_SOURCE_DIR}/src/encryption.c
   )
   target_link_libraries(test_encryption unity pico_mocks)
   add_test(NAME test_encryption COMMAND test_encryption)
   ```
5. Run with `./test.sh`

## Unity Assertions

Common assertions used in tests:

```c
TEST_ASSERT_TRUE(condition)
TEST_ASSERT_FALSE(condition)
TEST_ASSERT_EQUAL(expected, actual)
TEST_ASSERT_NOT_EQUAL(expected, actual)
TEST_ASSERT_NULL(pointer)
TEST_ASSERT_NOT_NULL(pointer)
TEST_ASSERT_EQUAL_STRING(expected, actual)
TEST_ASSERT_EQUAL_MEMORY(expected, actual, size)
```

See [Unity documentation](https://github.com/ThrowTheSwitch/Unity/blob/master/docs/UnityAssertionsReference.md) for full list.

## Mocking

The `mocks/` directory provides minimal mocks of Pico SDK types needed for testing:

- `hid_keyboard_report_t` - HID report structure
- `panic()` - Panic function (exits on error)
- Flash constants (`FLASH_STORE_SIZE`)

Add new mocks as needed when testing other modules.

## Benefits

- ✅ Fast feedback (no flashing to hardware)
- ✅ Test on any platform (Linux, Mac, Windows)
- ✅ Catch bugs early in development
- ✅ CI/CD integration ready
- ✅ Refactoring confidence
