#!/bin/bash
set -e

echo "=== JefeCheck Linux Build Script ==="

# Install dependencies
echo "Installing dependencies..."
sudo apt-get update
sudo apt-get install -y \
    cmake \
    build-essential \
    libfltk1.3-dev \
    libopenimageio-dev \
    openimageio-tools \
    libopenexr-dev \
    libilmbase-dev \
    libimath-dev \
    libcurl4-openssl-dev \
    zlib1g-dev \
    freeglut3-dev \
    libgl-dev \
    libglu1-mesa-dev \
    libx11-dev \
    libxext-dev \
    libxft-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxrender-dev \
    libxfixes-dev \
    libopencv-dev

# Symlink FX resources if not already done
if [ ! -L "Resources/FX" ] && [ ! -d "Resources/FX" ]; then
    echo "Creating Resources/FX symlink..."
    mkdir -p Resources
    ln -sf "$(pwd)/common/FX" Resources/FX
fi

# Build
echo "Configuring..."
cmake -B build

echo "Building..."
cmake --build build -j$(nproc)

echo "=== Build complete: ./build/jefecheck ==="
