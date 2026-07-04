---
layout: default
title: Building from Source
---

# Building JefeCheck from Source

JefeCheck uses **CMake** as its build system across all platforms, targeting **C++20**.

---

## macOS

### Prerequisites

Install dependencies via [Homebrew](https://brew.sh/):

```bash
brew install fltk openimageio openexr curl zlib cmake freetype
```

### Build

```bash
git clone https://github.com/danielgollas/jefecheck.git
cd jefecheck
cmake -B build
cmake --build build
```

### Notes

- Tested on macOS with Apple Silicon (ARM64).
- OpenGL runs via Apple's Metal translation layer (GL 2.1). OpenGL is deprecated by Apple but fully functional for JefeCheck.
- Retina displays are supported automatically.

---

## Linux (Ubuntu 24.04)

### Prerequisites

```bash
sudo apt install cmake build-essential \
  libfltk1.3-dev libopenimageio-dev openimageio-tools \
  libopenexr-dev libilmbase-dev libimath-dev libcurl4-openssl-dev zlib1g-dev \
  freeglut3-dev libgl-dev libglu1-mesa-dev \
  libx11-dev libxext-dev libxft-dev libxinerama-dev \
  libxcursor-dev libxrender-dev libxfixes-dev \
  libopencv-dev libfreetype6-dev
```

### Build

```bash
git clone https://github.com/danielgollas/jefecheck.git
cd jefecheck
cmake -B build
cmake --build build -j$(nproc)
```

Or use the convenience script:

```bash
bash build_linux.sh
```

---

## Windows (MSYS2 / MinGW)

### Prerequisites

Install [MSYS2](https://www.msys2.org/), then from the MSYS2 MinGW 64-bit shell:

```bash
pacman -S --noconfirm \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-fltk \
  mingw-w64-x86_64-openimageio \
  mingw-w64-x86_64-openexr \
  mingw-w64-x86_64-imath \
  mingw-w64-x86_64-curl \
  mingw-w64-x86_64-zlib \
  mingw-w64-x86_64-freeglut \
  mingw-w64-x86_64-freetype \
  mingw-w64-x86_64-fmt
```

### Build

```bash
git clone https://github.com/danielgollas/jefecheck.git
cd jefecheck
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

### Notes

- The Windows build uses `-fpermissive` for FLTK callback casts.
- CI builds are automated via GitHub Actions using MSYS2. See `.github/workflows/build.yml`.

---

## Runtime Resources

JefeCheck looks for its FX plug-ins and fonts relative to the binary. For development, create symlinks from the source tree:

```bash
ln -sf $(pwd)/src/FX FX
ln -sf $(pwd)/src/fonts fonts
```

Release builds package `src/FX/` and `src/fonts/` alongside the binary automatically.

---

## CI / Pre-built Binaries

GitHub Actions builds JefeCheck on all three platforms for every push to `main`. Tagged releases (`v*`) produce downloadable binaries on the [Releases page](https://github.com/danielgollas/jefecheck/releases).

Artifacts:
- `jefecheck-macos-arm64.dmg`
- `jefecheck-linux-x64.tar.gz`
- `jefecheck-windows-x64.zip`
