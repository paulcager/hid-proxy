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
RUN git clone -b master https://github.com/raspberrypi/pico-sdk.git && \
    cd pico-sdk && git submodule update --init

# Environment vars for CMake
ENV PICO_SDK_PATH=/opt/pico-sdk

# Pre-compile SDK components by building a minimal example
# This caches TinyUSB and other SDK libraries to speed up first project build
WORKDIR /tmp/prebuild
RUN echo 'cmake_minimum_required(VERSION 3.13)\n\
set(PICO_SDK_PATH /opt/pico-sdk)\n\
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)\n\
project(prebuild C CXX ASM)\n\
pico_sdk_init()\n\
add_executable(prebuild main.c)\n\
target_link_libraries(prebuild pico_stdlib)\n\
pico_add_extra_outputs(prebuild)' > CMakeLists.txt && \
    echo '#include "pico/stdlib.h"\nint main() { return 0; }' > main.c && \
    mkdir build && cd build && \
    cmake .. && make -j$(nproc) && \
    cd / && rm -rf /tmp/prebuild

# Create dev user
RUN useradd -ms /bin/bash builder
USER builder
WORKDIR /home/builder/project

# Run like:
#   docker run --rm -it -v $(pwd):/home/builder/project pico-bld bash
