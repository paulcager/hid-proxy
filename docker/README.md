# Docker Development Environment

This directory contains a Docker-based development environment for building the hid-proxy project. It provides a self-contained, reproducible build environment with all necessary tools and dependencies.

## Prerequisites

- Docker installed on your system

## What's Included

The Docker image includes:
- Ubuntu 22.04 LTS base
- ARM cross-compilation toolchain (gcc-arm-none-eabi)
- CMake and Ninja build systems
- Raspberry Pi Pico SDK 2.2.0 with submodules pre-compiled for faster builds
- Pre-built development tools (available in PATH):
  - **pioasm**: PIO assembler (v2.2.0)
  - **picotool**: Device loading and info tool (v2.2.0-a4)
  - **kvstore-util**: KVStore image manipulation tool (from pico-kvstore)

**Note**: Project dependencies (tiny-AES-c, Pico-PIO-USB, tinycrypt, pico-kvstore) are used as git submodules from your project directory, not embedded in the image. The pico-kvstore copy in the image is only used for building the kvstore-util host tool.

**See DOCKER_TOOLS.md** for detailed documentation on using pioasm, picotool, and kvstore-util.

## Quick Start (Recommended)

From the project root directory, use the build script:

```bash
# Simple build
./build.sh

# Clean build
./build.sh --clean

# Interactive shell for debugging
./build.sh --interactive
```

The build script automatically:
- Initializes git submodules if needed
- Builds the Docker image on first run (cached afterward)
- Compiles the project
- Shows the output file locations

## Manual Docker Usage

If you prefer to use Docker directly:

### Build and run:

```bash
# Build the Docker image (first time only)
# IMPORTANT: Must run from project root, not from docker/ directory
docker build -t pico-bld -f docker/Dockerfile .

# Run the build
docker run --rm -v $(pwd):/home/builder/project pico-bld \
    bash -c "mkdir -p build && cd build && cmake .. && make -j$(nproc)"

# Outputs in ./build/
ls build/*.uf2
```

**Note**: The Docker build must be run from the project root with `-f docker/Dockerfile .` because it needs to copy the `pico-kvstore` submodule into the image. Running `docker build` from inside the `docker/` directory will fail.

### Interactive shell:

```bash
docker run --rm -it -v $(pwd):/home/builder/project pico-bld bash
```

Inside the container:
```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Outputs available at /home/builder/project/build/
ls *.uf2 *.elf
```

## How It Works

1. **Volume mounting**: Your project directory is mounted at `/home/builder/project` inside the container
2. **Submodules from host**: Dependencies (Pico-PIO-USB, tiny-AES-c, tinycrypt) are used from your git submodules, not from the image
3. **Pre-compiled SDK**: The Pico SDK is pre-compiled in the image, making first builds much faster
4. **Path resolution**: CMakeLists.txt automatically detects Docker environment and uses `/opt/pico-sdk`
5. **Build artifacts**: All build outputs are written to the mounted `./build/` directory and persist after the container exits

## Performance Optimization

The Docker image includes a pre-compiled Pico SDK, which significantly reduces build times:

- **First build with pre-compiled SDK**: ~2-3 minutes
- **First build without pre-compilation**: ~8-10 minutes
- **Incremental builds**: ~10-30 seconds

## Rebuilding the Docker Image

If you need to rebuild the image (e.g., to update the Pico SDK):

```bash
# Remove old image
docker rmi pico-bld

# Rebuild (from project root)
./build.sh
# or
docker build -t pico-bld -f docker/Dockerfile .
```

## Flashing the Firmware

The built `.uf2` files can be flashed to your Pico:

1. **From the Docker build output**: Copy `build/hid_proxy.uf2` to your Pico in BOOTSEL mode
2. **Using the bootloader trigger**: Hold both shift keys + HOME on the running device, then copy the .uf2 file

## Docker vs Local Build

Both Docker and local builds produce identical outputs:

| Aspect | Docker | Local |
|--------|--------|-------|
| **PICO_SDK_PATH** | `/opt/pico-sdk` (in image) | `/home/paul/pico/pico-sdk` or custom |
| **Dependencies** | Git submodules (from host) | Git submodules |
| **Build location** | `./build/` (on host) | `./build/` |
| **Toolchain** | gcc-arm-none-eabi (in image) | System-installed |

The CMakeLists.txt automatically detects the environment and uses the correct SDK path.

## Troubleshooting

### Build fails with "Could not find PICO_SDK_PATH"
- Ensure you're running from the project root directory
- Check that the volume mount is working: `docker run --rm -v $(pwd):/home/builder/project pico-bld ls -la`

### Permission denied on build outputs
- The container runs as user `builder` (UID 1000 by default)
- If your local user has a different UID, you may see permission issues
- Solution: `sudo chown -R $USER:$USER build/`

### Submodules not initialized
- If you see "CMake Error: Could not find Pico-PIO-USB", run:
  ```bash
  git submodule update --init --recursive
  ```
- Or use `./build.sh` which does this automatically

### Docker image build is slow
- The image includes a full Pico SDK compilation
- First build takes 5-10 minutes but is cached
- The image is ~1.5-2GB

## Notes

- Build artifacts are stored in `./build/` which is git-ignored
- Dependencies are mounted from your git submodules, not embedded in the image
- The Pico SDK is pre-compiled in the image for faster builds
- Use `./build.sh` for the simplest workflow
