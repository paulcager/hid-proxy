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
ENABLE_DIAGNOSTICS=false  # Diagnostic system (16KB RAM)
USB_HOST_DP_PIN=6  # GPIO pin for USB host D+ (D- is D++1), default 6 for new boards

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
    -b, --board BOARD   Target board: pico, pico_w, pico2, pico2_w, ws_2350 (default: pico_w)
    -s, --stdio         Enable USB CDC stdio for debugging (printf over USB)
    -n, --nfc           Enable NFC tag authentication support
    -D, --diagnostics   Enable diagnostic system (16KB RAM, Double-shift+D to dump)
    -u, --usb-pins PIN  USB host D+ GPIO pin (D- is automatically D++1, default: 6)

EXAMPLES:
    ./build.sh                      # Build for Pico W (RP2040 with WiFi)
    ./build.sh --board pico         # Build for regular Pico (RP2040, no WiFi)
    ./build.sh --board pico2_w      # Build for Pico2 W (RP2350 with WiFi)
    ./build.sh --board pico2        # Build for Pico2 (RP2350, no WiFi)
    ./build.sh --stdio              # Build with USB CDC debugging enabled
    ./build.sh --clean              # Clean build
    ./build.sh --interactive        # Open shell for debugging
    ./build.sh --local --board pico # Use local toolchain for regular Pico
    ./build.sh --usb-pins 2         # Build with USB host on GPIO2/3 (for old boards)

BOARD OPTIONS:
    pico_w      Raspberry Pi Pico W (RP2040 with WiFi/HTTP) - DEFAULT [WORKING]
    pico        Raspberry Pi Pico (RP2040, WiFi disabled, smaller binary) [WORKING]
    pico2_w     Raspberry Pi Pico2 W (RP2350 with WiFi/HTTP) [NOT WORKING]
    pico2       Raspberry Pi Pico2 (RP2350, WiFi disabled) [NOT WORKING]
    ws_2350     Waveshare RP2350-USB-A (RP2350, USB-A host, RGB LED) [NOT WORKING]

    WARNING: RP2350 boards are NOT CURRENTLY FUNCTIONAL. USB keyboards are
             not detected due to PIO-USB compatibility issues (possible RP2350B
             silicon bug or library incompatibility). Use RP2040 boards only.

DEBUG OPTIONS:
    --stdio     Enable USB CDC stdio (printf/scanf over USB serial)
                Access via /dev/ttyACM0 (or similar) at 115200 baud
                Note: Adds ~20KB to binary, disable for production builds

NFC OPTIONS:
    --nfc       Enable NFC tag authentication (PN532 via I2C)
                Requires PN532 module connected to GPIO 4/5 (I2C)
                Default: disabled (most users don't have NFC hardware)

DIAGNOSTIC OPTIONS:
    --diagnostics   Enable diagnostic system (Double-shift+D to dump keystroke history)
                    Allocates 16KB RAM for cyclic buffers tracking last 256 keystrokes
                    Useful for debugging keystroke drops/corruption issues
                    Default: disabled (saves RAM for production builds)

USB HOST PIN OPTIONS:
    --usb-pins PIN  Set GPIO pin for USB host D+ (D- is automatically D++1)
                    Default: 6 (GPIO6/7) for new boards
                    Use --usb-pins 2 for boards with sockets soldered to GPIO2/3
                    Recommended: GPIO6/7 (keeps both UARTs on default pins)
                    Also valid: GPIO8/9 (conflicts with UART1 default)

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
            if [[ "$BOARD" != "pico" && "$BOARD" != "pico_w" && "$BOARD" != "pico2" && "$BOARD" != "pico2_w" && "$BOARD" != "ws_2350" ]]; then
                echo -e "${RED}Error: Invalid board '$BOARD'. Must be 'pico', 'pico_w', 'pico2', 'pico2_w', or 'ws_2350'${NC}"
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
        -D|--diagnostics)
            ENABLE_DIAGNOSTICS=true
            shift
            ;;
        -u|--usb-pins)
            USB_HOST_DP_PIN="$2"
            if ! [[ "$USB_HOST_DP_PIN" =~ ^[0-9]+$ ]] || [ "$USB_HOST_DP_PIN" -lt 0 ] || [ "$USB_HOST_DP_PIN" -gt 28 ]; then
                echo -e "${RED}Error: Invalid USB host pin '$USB_HOST_DP_PIN'. Must be 0-28${NC}"
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

    # Warn if building for RP2350 (non-functional)
    if [[ "$BOARD" == "pico2" || "$BOARD" == "pico2_w" || "$BOARD" == "ws_2350" ]]; then
        echo -e "${YELLOW}════════════════════════════════════════════════════════════════${NC}"
        echo -e "${YELLOW}WARNING: Building for RP2350 board (NOT CURRENTLY FUNCTIONAL)${NC}"
        echo -e "${YELLOW}USB keyboards are not detected on RP2350 due to PIO-USB issues.${NC}"
        echo -e "${YELLOW}Use RP2040 boards (pico, pico_w) for working deployments.${NC}"
        echo -e "${YELLOW}════════════════════════════════════════════════════════════════${NC}"
        echo ""
    fi

    # Build the Docker image if it doesn't exist
    if [[ -z "$(docker images -q pico-bld 2> /dev/null)" ]]; then
        echo -e "${YELLOW}Building Docker image (this may take 5-10 minutes on first run)...${NC}"
        # Build from project root to include pico-kvstore submodule
        docker build -t pico-bld -f docker/Dockerfile .
    fi

    # Prepare CMake flags
    # Map ws_2350 to pico2 board for CMake (RP2350 without WiFi)
    if [[ "$BOARD" == "ws_2350" ]]; then
        CMAKE_FLAGS="-DPICO_BOARD=pico2 -DBOARD_WS_2350=ON"
    else
        CMAKE_FLAGS="-DPICO_BOARD=$BOARD"
    fi
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
    if [[ "$ENABLE_DIAGNOSTICS" == true ]]; then
        CMAKE_FLAGS="$CMAKE_FLAGS -DENABLE_DIAGNOSTICS=ON"
        echo -e "${YELLOW}Diagnostic system enabled (16KB RAM allocated)${NC}"
    fi
    CMAKE_FLAGS="$CMAKE_FLAGS -DUSB_HOST_DP_PIN=$USB_HOST_DP_PIN"
    if [[ "$USB_HOST_DP_PIN" != "6" ]]; then
        echo -e "${YELLOW}USB host on GPIO$USB_HOST_DP_PIN/$((USB_HOST_DP_PIN+1)) (non-default)${NC}"
    fi

    # Run the build with board selection
    docker run --rm -v "$(pwd):/home/builder/project" pico-bld \
        bash -c "mkdir -p build && cd build && cmake $CMAKE_FLAGS .. && make -j\$(nproc)"

else
    # Build locally
    check_local_toolchain

    echo -e "${GREEN}Building with local toolchain...${NC}"

    # Warn if building for RP2350 (non-functional)
    if [[ "$BOARD" == "pico2" || "$BOARD" == "pico2_w" || "$BOARD" == "ws_2350" ]]; then
        echo -e "${YELLOW}════════════════════════════════════════════════════════════════${NC}"
        echo -e "${YELLOW}WARNING: Building for RP2350 board (NOT CURRENTLY FUNCTIONAL)${NC}"
        echo -e "${YELLOW}USB keyboards are not detected on RP2350 due to PIO-USB issues.${NC}"
        echo -e "${YELLOW}Use RP2040 boards (pico, pico_w) for working deployments.${NC}"
        echo -e "${YELLOW}════════════════════════════════════════════════════════════════${NC}"
        echo ""
    fi

    # Prepare CMake flags
    # Map ws_2350 to pico2 board for CMake (RP2350 without WiFi)
    if [[ "$BOARD" == "ws_2350" ]]; then
        CMAKE_FLAGS="-DPICO_BOARD=pico2 -DBOARD_WS_2350=ON"
    else
        CMAKE_FLAGS="-DPICO_BOARD=$BOARD"
    fi
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
    if [[ "$ENABLE_DIAGNOSTICS" == true ]]; then
        CMAKE_FLAGS="$CMAKE_FLAGS -DENABLE_DIAGNOSTICS=ON"
        echo -e "${YELLOW}Diagnostic system enabled (16KB RAM allocated)${NC}"
    fi
    CMAKE_FLAGS="$CMAKE_FLAGS -DUSB_HOST_DP_PIN=$USB_HOST_DP_PIN"
    if [[ "$USB_HOST_DP_PIN" != "6" ]]; then
        echo -e "${YELLOW}USB host on GPIO$USB_HOST_DP_PIN/$((USB_HOST_DP_PIN+1)) (non-default)${NC}"
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
        echo "Features: WiFi/HTTP enabled"
        echo "Status: ⚠️  NOT WORKING - PIO-USB issues"
    elif [[ "$BOARD" == "pico2" ]]; then
        echo "Platform: RP2350"
        echo "Features: WiFi disabled"
        echo "Status: ⚠️  NOT WORKING - PIO-USB issues"
    elif [[ "$BOARD" == "ws_2350" ]]; then
        echo "Platform: RP2350 (Waveshare RP2350-USB-A)"
        echo "Features: WiFi disabled, USB-A host port (GPIO12/13), RGB LED (GPIO16)"
        echo "Status: ⚠️  NOT WORKING - PIO-USB issues"
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
