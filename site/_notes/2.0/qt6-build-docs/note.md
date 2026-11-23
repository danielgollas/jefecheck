---
title: Build instructions updated for Qt 6
summary: Build JefeCheck from source by following a README that now matches the Qt 6 toolchain on macOS, Linux and Windows.
release: "2.0"
areas: [build]
pr: 123
commit: dff545c6e65b1d6cdf32651e9a6fa965724faacd
date: 2026-06-27
---

## What's new

This is a documentation-only change. The README still described the old FLTK build, with package names such as `fltk`. Anyone following it would have installed the wrong toolkit. It now documents the Qt 6 build:

- **macOS**: Homebrew packages, including `qt` and `freetype`.
- **Linux (Ubuntu 24.04)**: the full `apt` package list for Qt 6, OpenImageIO, OpenEXR/Imath, FreeType and the GL libraries.
- **Windows**: MSYS2 / MinGW packages, replacing the earlier vcpkg instructions.

The feature list was also updated. It now covers multi-plate layouts, per-plate colour correction, the RGB histogram, and render and export: image sequences at 8 or 16 bits with per-format quality, plus H.264/H.265/ProRes video. At the time, the FX stack was marked as still in progress.

The contributor documentation was updated too. It covers how render and export work, how the 16-bit output path works, how in-viewport overlays are dragged, and how FFmpeg is bundled at build time.

## How to use it

Follow **Building from Source** in the README. You don't need to install FFmpeg yourself for video export: the build downloads a static FFmpeg and bundles it. To skip that step, pass `-DJEFECHECK_BUNDLE_FFMPEG=OFF`.

## Under the hood

The new build section lists the requirements once and then gives the commands for each platform:

{{quote README.md#L25-L49 "The Qt 6 build steps per platform"}}

The FFmpeg bundling switch mentioned there is a real CMake option at this commit:

{{quote CMakeLists.txt#L110-L111 "The JEFECHECK_BUNDLE_FFMPEG option"}}
