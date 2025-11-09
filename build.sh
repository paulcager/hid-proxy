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
BOARD="pico_w"  # Default to Pico W (RP2040)
ENABLE_USB_STDIO=false  # USB CDC stdio for debugging
ENABLE_NFC=false  # NFC tag authentication

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
    -b, --board BOARD   Target board: pico, pico_w, pico2, pico2_w (default: pico_w)
    -s, --stdio         Enable USB CDC stdio for debugging (printf over USB)
    -n, --nfc           Enable NFC tag authentication support

EXAMPLES:
    ./build.sh                      # Build for Pico W (RP2040 with WiFi)
    ./build.sh --board pico         # Build for regular Pico (RP2040, no WiFi)
    ./build.sh --board pico2_w      # Build for Pico2 W (RP2350 with WiFi)
    ./build.sh --board pico2        # Build for Pico2 (RP2350, no WiFi)
    ./build.sh --stdio              # Build with USB CDC debugging enabled
    ./build.sh --clean              # Clean build
    ./build.sh --interactive        # Open shell for debugging
    ./build.sh --local --board pico # Use local toolchain for regular Pico

BOARD OPTIONS:
    pico_w      Raspberry Pi Pico W (RP2040 with WiFi/HTTP) - DEFAULT
    pico        Raspberry Pi Pico (RP2040, WiFi disabled, smaller binary)
    pico2_w     Raspberry Pi Pico2 W (RP2350 with WiFi/HTTP)
    pico2       Raspberry Pi Pico2 (RP2350, WiFi disabled, smaller binary)

    NOTE: Pico2 (RP2350) support is experimental. PIO-USB compatibility
          depends on GPIO selection. Test thoroughly before production use.

DEBUG OPTIONS:
    --stdio     Enable USB CDC stdio (printf/scanf over USB serial)
                Access via /dev/ttyACM0 (or similar) at 115200 baud
                Note: Adds ~20KB to binary, disable for production builds

NFC OPTIONS:
    --nfc       Enable NFC tag authentication (PN532 via I2C)
                Requires PN532 module connected to GPIO 4/5 (I2C)
                Default: disabled (most users don't have NFC hardware)

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
            if [[ "$BOARD" != "pico" && "$BOARD" != "pico_w" && "$BOARD" != "pico2" && "$BOARD" != "pico2_w" ]]; then
                echo -e "${RED}Error: Invalid board '$BOARD'. Must be 'pico', 'pico_w', 'pico2', or 'pico2_w'${NC}"
                exit 1
            fi
            shift 2
            ;;
        -s|--stdio)
            ENABLE_USB_STDIO=true
            shift
            ;;
        -n|--nfc)
            ENABLE_NFC=true
            shift
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

        # Build the Docker image if it doesn't exist
        if [[ -z "$(docker images -q pico-bld 2> /dev/null)" ]]; then
            echo -e "${YELLOW}Building Docker image first...${NC}"
            docker build -t pico-bld -f docker/Dockerfile .
        fi

        docker run --rm -it -v "$(pwd):/home/builder/project" pico-bld bash
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
        # Build from project root to include pico-kvstore submodule
        docker build -t pico-bld -f docker/Dockerfile .
    fi

    # Prepare CMake flags
    CMAKE_FLAGS="-DPICO_BOARD=$BOARD"
    if [[ "$BUILD_TYPE" == "debug" ]]; then
        CMAKE_FLAGS="$CMAKE_FLAGS -DCMAKE_BUILD_TYPE=Debug"
        echo -e "${YELLOW}Debug build enabled (no optimization, debug symbols)${NC}"
    else
        CMAKE_FLAGS="$CMAKE_FLAGS -DCMAKE_BUILD_TYPE=Release"
    fi
    if [[ "$ENABLE_USB_STDIO" == true ]]; then
        CMAKE_FLAGS="$CMAKE_FLAGS -DENABLE_USB_STDIO=ON"
        echo -e "${YELLOW}USB CDC stdio debugging enabled${NC}"
    fi
    if [[ "$ENABLE_NFC" == true ]]; then
        CMAKE_FLAGS="$CMAKE_FLAGS -DENABLE_NFC=ON"
        echo -e "${YELLOW}NFC tag authentication enabled${NC}"
    fi

    # Run the build with board selection
    docker run --rm -v "$(pwd):/home/builder/project" pico-bld \
        bash -c "mkdir -p build && cd build && cmake $CMAKE_FLAGS .. && make -j\$(nproc)"

else
    # Build locally
    check_local_toolchain

    echo -e "${GREEN}Building with local toolchain...${NC}"

    # Prepare CMake flags
    CMAKE_FLAGS="-DPICO_BOARD=$BOARD"
    if [[ "$BUILD_TYPE" == "debug" ]]; then
        CMAKE_FLAGS="$CMAKE_FLAGS -DCMAKE_BUILD_TYPE=Debug"
        echo -e "${YELLOW}Debug build enabled (no optimization, debug symbols)${NC}"
    else
        CMAKE_FLAGS="$CMAKE_FLAGS -DCMAKE_BUILD_TYPE=Release"
    fi
    if [[ "$ENABLE_USB_STDIO" == true ]]; then
        CMAKE_FLAGS="$CMAKE_FLAGS -DENABLE_USB_STDIO=ON"
        echo -e "${YELLOW}USB CDC stdio debugging enabled${NC}"
    fi
    if [[ "$ENABLE_NFC" == true ]]; then
        CMAKE_FLAGS="$CMAKE_FLAGS -DENABLE_NFC=ON"
        echo -e "${YELLOW}NFC tag authentication enabled${NC}"
    fi

    mkdir -p build
    cd build
    cmake $CMAKE_FLAGS ..
    make -j$(nproc)
    cd ..
fi

# Check if build succeeded
if [[ -f "build/hid_proxy.uf2" ]]; then
    echo -e "${GREEN}✓ Build successful!${NC}"
    echo ""
    echo "Board: $BOARD"
    echo "Build type: $BUILD_TYPE"
    if [[ "$BOARD" == "pico_w" ]]; then
        echo "Platform: RP2040"
        echo "Features: WiFi/HTTP enabled"
    elif [[ "$BOARD" == "pico" ]]; then
        echo "Platform: RP2040"
        echo "Features: WiFi disabled (regular Pico)"
    elif [[ "$BOARD" == "pico2_w" ]]; then
        echo "Platform: RP2350"
        echo "Features: WiFi/HTTP enabled (EXPERIMENTAL - test PIO-USB)"
    elif [[ "$BOARD" == "pico2" ]]; then
        echo "Platform: RP2350"
        echo "Features: WiFi disabled (EXPERIMENTAL - test PIO-USB)"
    fi
    if [[ "$ENABLE_USB_STDIO" == true ]]; then
        echo "Debug: USB CDC stdio enabled (10s WiFi startup delay)"
    fi
    if [[ "$ENABLE_NFC" == true ]]; then
        echo "NFC: Tag authentication enabled (PN532 on GPIO 4/5)"
    fi
    echo ""
    echo "Firmware files:"
    ls -lh build/hid_proxy.{uf2,elf} 2>/dev/null || true
    echo ""
    echo "To flash:"
    echo "  1. Hold both Shift keys + HOME on running device (enters BOOTSEL mode)"
    echo "  2. Copy build/hid_proxy.uf2 to the RPI-RP2 drive"
    echo ""
    if [[ "$BOARD" == "pico_w" || "$BOARD" == "pico2_w" ]]; then
        echo "WiFi/HTTP configuration:"
        echo "  - Press both Shift keys + HOME to enable web access"
        echo "  - Access device at http://hidproxy.local"
        echo "  - See WIFI_SETUP.md for complete guide"
    fi
    if [[ "$ENABLE_USB_STDIO" == true ]]; then
        echo ""
        echo "USB CDC stdio debugging:"
        echo "  - Connect to /dev/ttyACM0 (or similar) at 115200 baud"
        echo "  - Example: minicom -D /dev/ttyACM0 -b 115200"
        echo "  - Example: screen /dev/ttyACM0 115200"
    fi
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi
