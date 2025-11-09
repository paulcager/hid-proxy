FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    cmake \
    git \
    build-essential \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    python3 \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt

# --- Core Pico SDK ---
# Pinned to 2.2.0 for RP2350 (Pico2) support
RUN git clone -b 2.2.0 --depth 1 https://github.com/raspberrypi/pico-sdk.git && \
    cd pico-sdk && git submodule update --init --recursive

# Environment vars for CMake
ENV PICO_SDK_PATH=/opt/pico-sdk

# Install libusb for picotool
RUN apt-get update && apt-get install -y \
    libusb-1.0-0-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Pre-download and pre-build SDK tools to speed up clean builds
# The Pico SDK uses ExternalProject_Add/FetchContent to download and build these tools
# on first use. By pre-building them here and setting up the shared cache directory,
# we eliminate the "Downloading Picotool" and "pioasmBuild" steps from project builds.

# Create shared tools directory
RUN mkdir -p /opt/pico-tools

# Pre-build pioasm from SDK sources
# This is the PIO assembler that gets built on first project build
WORKDIR /opt/pico-tools
RUN mkdir -p pioasm-build && \
    cd pioasm-build && \
    cmake -DPIOASM_VERSION_STRING=2.2.0 /opt/pico-sdk/tools/pioasm && \
    make -j$(nproc) && \
    cp pioasm /usr/local/bin/

# Pre-build picotool completely
# This tool is used for loading binaries to Pico devices
# Install it properly so subsequent builds don't try to reinstall
RUN git clone --depth 1 https://github.com/raspberrypi/picotool.git picotool-src && \
    mkdir -p picotool-build && \
    cd picotool-build && \
    cmake -DPICO_SDK_PATH=/opt/pico-sdk ../picotool-src && \
    make -j$(nproc) && \
    make install && \
    # Clean up the build directory to prevent reinstall attempts
    cd /opt/pico-tools && \
    rm -rf picotool-build picotool-src

# Pre-build pico-kvstore host tool (kvstore-util)
# This tool is used for creating and manipulating KVS image files on the host
# NOTE: We COPY the patched pico-kvstore submodule from the project rather than cloning upstream
# The patches include:
# 1. mbedtls 3.x API compatibility (gcm_starts, gcm_update, gcm_finish)
# 2. SDK 2.2.0 host library compatibility (pico_rand, pico_mbedtls_*, etc.)
WORKDIR /opt
COPY --chown=root:root pico-kvstore pico-kvstore-host
RUN cd pico-kvstore-host && \
    # Patch 1: CMakeLists.txt to fix conflict with Pico SDK 2.2.0 host libraries \
    # The SDK now provides these interface libraries, so only create if they don't exist \
    sed -i '/^add_library(pico_rand INTERFACE)/i if(NOT TARGET pico_rand)' host/CMakeLists.txt && \
    sed -i '/^add_library(pico_mbedtls_crypto INTERFACE)/i if(NOT TARGET pico_mbedtls_crypto)' host/CMakeLists.txt && \
    sed -i '/^add_library(pico_mbedtls_headers INTERFACE)/i if(NOT TARGET pico_mbedtls_headers)' host/CMakeLists.txt && \
    sed -i '/^add_library(pico_unique_id INTERFACE)/i if(NOT TARGET pico_unique_id)' host/CMakeLists.txt && \
    sed -i '/^add_library(pico_unique_id INTERFACE)/a endif()' host/CMakeLists.txt && \
    sed -i '/^add_library(pico_mbedtls_headers INTERFACE)/a endif()' host/CMakeLists.txt && \
    sed -i '/^add_library(pico_mbedtls_crypto INTERFACE)/a endif()' host/CMakeLists.txt && \
    sed -i '/^add_library(pico_rand INTERFACE)/a endif()' host/CMakeLists.txt && \
    # Patch 2: Use SDK's get_rand_128 instead of local implementation \
    # Keep typedef, remove function definition (lines 47-73) \
    sed -i '47,73d' src/kvstore_securekvs.c && \
    mkdir -p host/build && \
    cd host/build && \
    PICO_SDK_PATH=/opt/pico-sdk PICO_PLATFORM=host cmake .. && \
    make -j$(nproc) && \
    cp kvstore-util /usr/local/bin/ && \
    cd /opt && rm -rf pico-kvstore-host

# Create dev user and give ownership of pico-tools directory
RUN useradd -ms /bin/bash builder && \
    chown -R builder:builder /opt/pico-tools

USER builder

# Set environment variable so SDK finds pre-downloaded picotool sources
# (though we've already built it, this keeps the cache available if needed)
ENV PICOTOOL_FETCH_FROM_GIT_PATH=/opt/pico-tools

WORKDIR /home/builder/project

# Pre-built tools available in PATH:
#   - pioasm        : PIO assembler
#   - picotool      : Pico device loading and info tool
#   - kvstore-util  : KVStore image manipulation tool
#
# Run like:
#   docker run --rm -it -v $(pwd):/home/builder/project pico-bld bash
#
# Example kvstore-util usage:
#   kvstore-util create -f kvstore.bin -s 131072
#   kvstore-util set -f kvstore.bin -k wifi.ssid -v "MyNetwork"
#   kvstore-util get -f kvstore.bin -k wifi.ssid
#   picotool load -o 0x101e0000 kvstore.bin
