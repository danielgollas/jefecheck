# JefeCheck

Professional video frame processing and playback application for color correction, effects, and real-time review of digital cinema content.

> **GUI:** JefeCheck's interface is built on **Qt 6** (dark VFX theme). The Qt
> rewrite lives on the `qt-experimental` branch; `main` remains on the older
> FLTK build until the rewrite is promoted. Build from `qt-experimental` for
> the current app.

## Features

- Real-time playback of image sequences (DPX, EXR, and common formats via OpenImageIO)
- Up to 4 simultaneous video tracks/plates with multi-plate layouts (1×1, 2×1, 1×2, 2×2)
- Per-plate color correction (gamma/exposure/brightness/contrast/saturation, RGBA masks)
- 1D and 3D LUT (Look-Up Table) support for color grading, with an interactive LUT inspector
- Render & export: image sequences (JPEG/PNG/TIFF/TGA/BMP/EXR, 8/16-bit, per-format quality) and **video** (H.264/H.265/ProRes via bundled FFmpeg), with output-resolution control and a live, cancellable progress bar
- RGB histogram overlay
- Network-based remote control and synchronization between instances
- Playlist management (`.jpl`) for sequential review sessions
- Session save/restore with crash recovery
- Shader-based FX pipeline (GLSL) — *wiring the FX stack into the Qt build is in progress*

## Building from Source

C++20, CMake. Qt 6, OpenImageIO, OpenEXR/Imath, FreeType, libcurl, zlib are required.
A static **FFmpeg** for video export is fetched and bundled by the build automatically
(disable with `-DJEFECHECK_BUNDLE_FFMPEG=OFF`).

### macOS

```bash
brew install qt openimageio openexr curl zlib cmake freetype
cmake -B build && cmake --build build
```

### Linux (Ubuntu 24.04)

```bash
sudo apt install cmake build-essential qt6-base-dev qt6-base-private-dev libqt6opengl6-dev \
  libopenimageio-dev openimageio-tools libopenexr-dev libimath-dev \
  libcurl4-openssl-dev zlib1g-dev libgl-dev libglu1-mesa-dev libfreetype6-dev
cmake -B build && cmake --build build -j$(nproc)
```

### Windows (MSYS2 / MinGW)

Use MSYS2 with `mingw-w64-x86_64-qt6-base`, `mingw-w64-x86_64-qt6-tools`,
`mingw-w64-x86_64-openimageio`, and `mingw-w64-x86_64-freetype` (see
`.github/workflows/build.yml`), then `cmake -B build && cmake --build build`.

## Usage

```bash
jefecheck [options] <files...>
```

### Options

| Flag | Description |
|------|-------------|
| `-h, --help` | Show help message |
| `-f, --from <frame>` | Start loading from this frame (per track) |
| `-t, --to <frame>` | Stop loading at this frame (per track) |
| `-s, --scale <percent>` | Scale percentage for loading (per track) |
| `-r, --frameRate <fps>` | Set playback frame rate |
| `-x, --fx <file>` | Load FX stack file (per track) |
| `-l, --lut <file>` | Load LUT file (per plate) |

### Examples

```bash
# Play a DPX sequence
jefecheck /path/to/sequence.0001.dpx

# Play two sequences side by side at 24fps
jefecheck -r 24 /path/to/seq_a.0001.exr /path/to/seq_b.0001.exr

# Load with a LUT applied
jefecheck -l grade.cube /path/to/sequence.0001.dpx
```

## Contributing

Contributions are welcome. Please review the [CLA](CLA.md) before submitting a pull request.

## License

This project is licensed under the [GNU General Public License v2.0](LICENSE).
