#!/bin/bash
set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default values
BUILD_TYPE="release"
CLEAN=false
INTERACTIVE=false
USE_DOCKER=true
BOARD="pico_w"  # Default to Pico W

show_help() {
    cat << EOF
Usage: ./build.sh [OPTIONS]

Build the hid-proxy firmware using Docker or local toolchain.

OPTIONS:
    -h, --help          Show this help message
    -c, --clean         Clean build directory before building
    -i, --interactive   Open interactive shell in Docker container
    -l, --local         Use local toolchain instead of Docker
    -d, --debug         Build with debug symbols
    -b, --board BOARD   Target board: pico or pico_w (default: pico_w)

EXAMPLES:
    ./build.sh                      # Build for Pico W (with WiFi)
    ./build.sh --board pico         # Build for regular Pico (no WiFi)
    ./build.sh --clean              # Clean build
    ./build.sh --interactive        # Open shell for debugging
    ./build.sh --local --board pico # Use local toolchain for regular Pico

BOARD OPTIONS:
    pico_w      Raspberry Pi Pico W (WiFi/HTTP enabled) - DEFAULT
    pico        Raspberry Pi Pico (WiFi disabled, smaller binary)

OUTPUT:
    build/hid_proxy.uf2         # Firmware file ready to flash
    build/hid_proxy.elf         # ELF file for debugging

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
        -i|--interactive)
            INTERACTIVE=true
            shift
            ;;
        -l|--local)
            USE_DOCKER=false
            shift
            ;;
        -d|--debug)
            BUILD_TYPE="debug"
            shift
            ;;
        -b|--board)
            BOARD="$2"
            if [[ "$BOARD" != "pico" && "$BOARD" != "pico_w" ]]; then
                echo -e "${RED}Error: Invalid board '$BOARD'. Must be 'pico' or 'pico_w'${NC}"
                exit 1
            fi
            shift 2
            ;;
        *)
            echo -e "${RED}Error: Unknown option $1${NC}"
            show_help
            exit 1
            ;;
    esac
done

# Check if Docker is available
check_docker() {
    if ! command -v docker &> /dev/null; then
        echo -e "${RED}Error: Docker is not installed${NC}"
        echo "Please install Docker or use --local to build with local toolchain"
        exit 1
    fi
}

# Check if local toolchain is available
check_local_toolchain() {
    if ! command -v arm-none-eabi-gcc &> /dev/null; then
        echo -e "${RED}Error: ARM toolchain not found${NC}"
        echo "Please install gcc-arm-none-eabi or use Docker (remove --local flag)"
        exit 1
    fi

    if [[ ! -d "${PICO_SDK_PATH:-/home/paul/pico/pico-sdk}" ]]; then
        echo -e "${RED}Error: Pico SDK not found${NC}"
        echo "Please set PICO_SDK_PATH or use Docker (remove --local flag)"
        exit 1
    fi
}

# Clean build directory
if [[ "$CLEAN" == true ]]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf build
fi

# Initialize submodules if needed
if [[ ! -f "Pico-PIO-USB/CMakeLists.txt" ]]; then
    echo -e "${YELLOW}Initializing git submodules...${NC}"
    git submodule update --init --recursive
fi

# Interactive mode
if [[ "$INTERACTIVE" == true ]]; then
    if [[ "$USE_DOCKER" == true ]]; then
        check_docker
        echo -e "${GREEN}Opening interactive shell in Docker container...${NC}"
        docker compose -f docker/docker-compose.yml run --rm pico-build bash
    else
        echo -e "${YELLOW}Interactive mode only works with Docker${NC}"
        exit 1
    fi
    exit 0
fi

# Build using Docker
if [[ "$USE_DOCKER" == true ]]; then
    check_docker

    echo -e "${GREEN}Building with Docker...${NC}"

    # Build the Docker image if it doesn't exist
    if [[ -z "$(docker images -q pico-bld 2> /dev/null)" ]]; then
        echo -e "${YELLOW}Building Docker image (this may take 5-10 minutes on first run)...${NC}"
        docker build -t pico-bld docker/
    fi

    # Run the build with board selection
    docker run --rm -v "$(pwd):/home/builder/project" pico-bld \
        bash -c "mkdir -p build && cd build && cmake -DPICO_BOARD=$BOARD .. && make -j\$(nproc)"

else
    # Build locally
    check_local_toolchain

    echo -e "${GREEN}Building with local toolchain...${NC}"
    mkdir -p build
    cd build
    cmake -DPICO_BOARD=$BOARD ..
    make -j$(nproc)
    cd ..
fi

# Check if build succeeded
if [[ -f "build/hid_proxy.uf2" ]]; then
    echo -e "${GREEN}✓ Build successful!${NC}"
    echo ""
    echo "Board: $BOARD"
    if [[ "$BOARD" == "pico_w" ]]; then
        echo "Features: WiFi/HTTP enabled"
    else
        echo "Features: WiFi disabled (regular Pico)"
    fi
    echo ""
    echo "Firmware files:"
    ls -lh build/hid_proxy.{uf2,elf} 2>/dev/null || true
    echo ""
    echo "To flash:"
    echo "  1. Hold both Shift keys + PAUSE on running device (enters BOOTSEL mode)"
    echo "  2. Copy build/hid_proxy.uf2 to the RPI-RP2 drive"
    echo ""
    if [[ "$BOARD" == "pico_w" ]]; then
        echo "WiFi/HTTP configuration:"
        echo "  - Press both Shift keys + HOME to enable web access"
        echo "  - Access device at http://hidproxy.local"
        echo "  - See WIFI_SETUP.md for complete guide"
    fi
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi
