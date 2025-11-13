# Test CMakeLists.txt - Builds tests to run on host machine

cmake_minimum_required(VERSION 3.13)
project(hid_proxy_tests C)

# Use native compiler for host tests (not ARM cross-compiler)
# This is already the default when not using pico_sdk_init()

# Add Unity testing framework
add_library(unity STATIC
    ${CMAKE_SOURCE_DIR}/unity/src/unity.c
)

target_include_directories(unity PUBLIC
    ${CMAKE_SOURCE_DIR}/unity/src
)

# Common includes for all tests
set(TEST_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks  # Mock SDK headers come first
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/tinycrypt/lib/include
)

# Mock pico SDK types for host testing
add_library(pico_mocks STATIC
    mocks/pico_mocks.c
    mocks/keydef_store_mocks.c
)

target_include_directories(pico_mocks PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    mocks
)

# Enable testing first
enable_testing()

# Test: Macros parser and serializer
add_executable(test_macros
    test_macros.c
    ${CMAKE_SOURCE_DIR}/src/macros.c
)

target_include_directories(test_macros PRIVATE
    ${TEST_INCLUDES}
    mocks
)

target_link_libraries(test_macros
    unity
    pico_mocks
)

# Add test to CTest
add_test(NAME test_macros COMMAND test_macros)

# Test: Macros helper functions
add_executable(test_macros_helpers
    test_macros_helpers.c
)

target_include_directories(test_macros_helpers PRIVATE
    ${TEST_INCLUDES}
    mocks
)

target_link_libraries(test_macros_helpers
    unity
    pico_mocks
)

# Add test to CTest
add_test(NAME test_macros_helpers COMMAND test_macros_helpers)

# Make sure test runs from the correct directory
set_tests_properties(test_macros PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Print instructions
message(STATUS "To run tests: cd build && make && ctest --output-on-failure")
