# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

JefeCheck is a professional C++ video frame processing and playback application for color correction, effects processing, and real-time playback of digital cinema content. It supports Windows, macOS, and Linux.

## Build System

**Windows (primary):** Open `win/jefecheck.sln` in Visual Studio 2010+. Supports Debug/Release and x64 configurations. Pre-built dependencies are in `win/buildDependencies/`.

**Linux:** Uses GNU Autotools.
```bash
cd linux/jefecheck
make -f Makefile.cvs        # Generate configure script
./configure
make
```
Packaging: `linux/createInstallPackage.sh`

**macOS:** See `mac/howToBuildJefeCheckOnAMac.txt`. Uses custom build scripts with bundle resources in `mac/MacBundleResources/`.

## Architecture

### Core Manager Pattern
The application uses a manager pattern where singleton-style manager classes coordinate subsystems:

- **gfcPlaybackManager** — Timeline, FPS, playback state control
- **gfcTrackManager** — Manages up to 4 video sequences/tracks for parallel playback
- **gfcPlateManager** — Manages 4 display quadrants (plates) with compositing
- **gfcPlaylistManager** — Sequential playlist of items
- **gfcSessionManager** — Save/load sessions with crash recovery via XML

### Rendering Pipeline
- **GlViewport** (Fl_Gl_Window subclass) — Main OpenGL rendering context
- **gfcPlate** — A display quadrant with its own color correction and FX stack
- **gfcFX / gfcFXStack** — Individual effects and their ordered stack per plate
- Fragment/vertex shaders in `common/FX/` (.frag/.vert files), with XML metadata in .jfx files

### Image I/O
Base class `gfcImageLoader` with format-specific subclasses:
- **gfcImageLoaderDPX** — DPX cinema format (custom parser via dpxslice)
- **gfcImageLoaderEXR** — OpenEXR high-bit-depth
- **gfcImageLoaderGFL** — GFL SDK for 100+ standard formats
- **gfcImageSaver** — Frame export

### Networking
RakNet-based client/server architecture for remote control and synchronization:
- **gfcNetworkManager** orchestrates server (`gfcNetworkServer`) and client (`gfcNetworkClient`)
- Packet definitions in `gfcNetworkStructures.h`

### UI
FLTK-based GUI. Window layouts defined in `.fl` files (FLUID designer), generating `.cxx`/`.h` pairs. Custom widgets prefixed `Fl_*_gfc`.

## Key Dependencies

FLTK (GUI), OpenGL/GLEW (rendering), Boost (filesystem, threading, program_options), OpenEXR, GFL SDK (image formats), RakNet (networking), Botan (license/crypto), libcurl (HTTP).

## Directory Layout

- `src/` — All C++ source (~416 files). Entry point: `main.cpp`
- `common/FX/` — Effect definitions: shaders (.frag/.vert), metadata (.jfx), LUTs (.lut/.cub)
- `win/` — VS solution, project files, build dependencies, installer (NSIS)
- `linux/` — Autotools build, packaging scripts
- `mac/` — macOS build scripts and bundle resources

## Notes

- No automated test suite exists.
- The `gfc` prefix on classes stands for the project's internal namespace convention.
- UI windows follow the pattern: `*Window.fl` → FLUID generates `*Window.cxx` + `*Window.h`.
- License/activation system uses RSA/DSA via Botan (see `activatorWindow`, `activatorCallbacks`).
