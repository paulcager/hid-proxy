#!/bin/bash
set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

BUILD_DIR="build-test"
CLEAN=false
VERBOSE=false

show_help() {
    cat << EOF
Usage: ./test.sh [OPTIONS]

Run unit tests on host machine (no Pico hardware needed).

OPTIONS:
    -h, --help      Show this help message
    -c, --clean     Clean build before testing
    -v, --verbose   Show detailed test output

EXAMPLES:
    ./test.sh           # Run tests
    ./test.sh --clean   # Clean build and run tests
    ./test.sh --verbose # Show detailed output

EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        *)
            echo -e "${RED}Error: Unknown option $1${NC}"
            show_help
            exit 1
            ;;
    esac
done

# Clean if requested
if [[ "$CLEAN" == true ]]; then
    echo -e "${YELLOW}Cleaning test build directory...${NC}"
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with tests enabled
echo -e "${GREEN}Configuring tests...${NC}"
cmake -DBUILD_TESTS=ON ..

# Build
echo -e "${GREEN}Building tests...${NC}"
make

# Run tests
echo -e "${GREEN}Running tests...${NC}"
echo ""

# Run the test executable directly (CTest integration has issues with our setup)
if [[ "$VERBOSE" == true ]]; then
    ./test/test_macros -v
else
    ./test/test_macros
fi

TEST_RESULT=$?

echo ""
if [[ $TEST_RESULT -eq 0 ]]; then
    echo -e "${GREEN}✓ All tests passed!${NC}"
else
    echo -e "${RED}✗ Some tests failed${NC}"
    exit 1
fi
