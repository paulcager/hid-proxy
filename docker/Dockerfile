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

# --- Pico Examples ---
RUN git clone -b master https://github.com/raspberrypi/pico-examples.git && \
    cd pico-sdk && git submodule update --init

# --- Common libs directory ---
RUN mkdir -p /opt/pico-libs
WORKDIR /opt/pico-libs

# tiny-AES-c
RUN git clone https://github.com/kokke/tiny-AES-c.git

# Adafruit-PN532 (use HTTPS instead of SSH)
RUN git clone https://github.com/adafruit/Adafruit-PN532.git

# pn532-lib (convert SSH → HTTPS)
RUN git clone https://github.com/paulcager/pn532-lib.git

# tinycrypt (convert SSH → HTTPS)
RUN git clone https://github.com/intel/tinycrypt.git

# Pico-PIO-USB (original sekigon-gonnoc repo)
RUN git clone https://github.com/sekigon-gonnoc/Pico-PIO-USB.git && \
    cd Pico-PIO-USB && git submodule update --init

# Environment vars for CMake
ENV PICO_SDK_PATH=/opt/pico-sdk
ENV PICO_LIBS_PATH=/opt/pico-libs
ENV PICO_PIO_USB_PATH=/opt/pico-libs/Pico-PIO-USB

# Optional: add extras & tinyusb if you want later
# ENV PICO_EXTRAS_PATH=/opt/pico-extras
# ENV PICO_TINYUSB_PATH=/opt/tinyusb

# Create dev user
RUN useradd -ms /bin/bash builder
USER builder
WORKDIR /home/builder/project

# Run like:
#   docker run --rm -it -v $(pwd):/home/builder/project pico-bld bash
