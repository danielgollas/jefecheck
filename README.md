# JefeCheck

Professional video frame processing and playback application for color correction, effects, and real-time review of digital cinema content.

This is **JefeCheck 2.0**. Its interface is built on **Qt 6** with a dark VFX theme.

The classic 1.7 release, built on FLTK, stays on the `main` branch and its 1.7.x
releases. The 1.7 line ends at 1.7.x; new work happens on 2.0.

## Features

- Real-time playback of image sequences (DPX, EXR, and common formats via OpenImageIO)
- Movies (`.mov` / `.mp4` / `.mxf`: ProRes 422/HQ/4444/XQ, H.264, H.265, DNxHR) load like a sequence, with sound — see [Movies](#movies)
- Up to 4 simultaneous video tracks/plates with multi-plate layouts (1×1, 2×1, 1×2, 2×2)
- Per-plate color correction (gamma/exposure/brightness/contrast/saturation, RGBA channel view)
- 1D and 3D LUT (Look-Up Table) support for color grading, with an interactive LUT inspector
- Render & export: image sequences (JPEG/PNG/TIFF/TGA/BMP/EXR, 8/16-bit, per-format quality) and **video** (H.264/H.265/ProRes via bundled FFmpeg), with output-resolution control and a live, cancellable progress bar
- RGB histogram overlay
- Shader-based FX pipeline (GLSL), with an FX panel for adding, reordering and adjusting effects per plate
- Review notes (freehand, arrow, box and text) per frame, review summaries (PDF/TXT/CSV) and `.jcreview` review packages
- Remote sessions: synchronized playback, chat and remote pointers between instances
- Playlist management (`.jpl`) for sequential review sessions
- Session save/restore with crash recovery

## Building from Source

C++20, CMake. Qt 6 (with Qt Multimedia), OpenImageIO, OpenEXR/Imath, FreeType, libcurl,
zlib and the FFmpeg libraries (libavformat, libavcodec, libavutil, libswresample,
libswscale; FFmpeg 6.1 or newer) are required. A static **FFmpeg** command-line tool for
video export is fetched and bundled by the build automatically (disable with
`-DJEFECHECK_BUNDLE_FFMPEG=OFF`).

### macOS

```bash
brew install qt openimageio openexr curl zlib cmake freetype ffmpeg
cmake -B build && cmake --build build
```

### Linux (Ubuntu 24.04)

```bash
sudo apt install cmake build-essential qt6-base-dev qt6-base-private-dev libqt6opengl6-dev \
  libopenimageio-dev openimageio-tools libopenexr-dev libimath-dev \
  libcurl4-openssl-dev zlib1g-dev libgl-dev libglu1-mesa-dev libfreetype6-dev \
  libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev qt6-multimedia-dev
cmake -B build && cmake --build build -j$(nproc)
```

### Windows (MSYS2 / MinGW)

Use MSYS2 (UCRT64 environment) with `mingw-w64-ucrt-x86_64-qt6-base`, `mingw-w64-ucrt-x86_64-qt6-tools`,
`mingw-w64-ucrt-x86_64-qt6-multimedia`, `mingw-w64-ucrt-x86_64-openimageio`,
`mingw-w64-ucrt-x86_64-ffmpeg`, and `mingw-w64-ucrt-x86_64-freetype` (see
`.github/workflows/build.yml`), then `cmake -B build && cmake --build build`.

## Usage

Most work happens in the app: **File → Quick Load…** (`Ctrl+O`), the **Load Sequence
Manager** (`Ctrl+L`), or drag and drop onto the viewport. See the
[user manual](docs/manual.md) and the [quick start](docs/quick-start.md). On macOS,
`Ctrl` in shortcuts means `Cmd`.

```bash
jefecheck [--open-file <path>]... [--config-dir <dir>] [--open-package <file.jcreview>]
```

### Options

| Flag | Description |
|------|-------------|
| `--open-file <path>` | Load a file or sequence into the next plate. Repeat for up to four plates. |
| `--config-dir <dir>` | Keep settings, sessions and caches in this folder instead of the default (also `JEFECHECK_CONFIG_DIR`). |
| `--open-package <file>` | Open a `.jcreview` review package at startup, without asking you to locate missing media. |

### Examples

```bash
# Play a DPX sequence
jefecheck --open-file /path/to/sequence.0001.dpx

# Load two sequences into plates 1 and 2
jefecheck --open-file /path/to/seq_a.0001.exr --open-file /path/to/seq_b.0001.exr
```

## Movies

Open a `.mov`, `.mp4` or `.mxf` (ProRes 422/HQ/4444/XQ, H.264, H.265, DNxHR) the same way as a
sequence: Quick Load (`Ctrl+O`), the Load Sequence Manager (`Ctrl+L`), drag and drop, or
`--open-file`. A movie is one track entry that behaves like a sequence (range, trims,
scale, bit depth, hold, offset, filmstrip, notes); the timeline counts its frames 1…n.
Frames are decoded into memory, never extracted to disk. Numbers in a movie's name are
never read as frame numbers: `shot_v003.mov` is one clip, and `a_001.mov` / `a_002.mov`
are two.

- **Colour:** YUV is converted with the file's own matrix (Rec.709/601/2020) and range;
  untagged files are treated as Rec.709 video range. 10/12-bit sources keep their
  precision on a 16-bit track.
- **Frame rate:** loading a movie sets the player's rate to the movie's exact rate
  (e.g. 23.976 = 24000/1001); the most recent load wins and a restored session keeps
  its saved rate. Other tracks play one frame per timeline frame at that rate. The
  status bar shows the movie's source timecode (drop-frame as `;`), and variable-rate
  files play at their average rate with a "VFR" note.
- **Sound:** every track with audio plays, mixed. Mute a track with the speaker on its
  timeline lane (or its right-click menu); mute is saved with the session. At the movie's
  own rate the sound leads and the picture keeps up; stepping, scrubbing and off-rate
  playback play each frame's own slice at normal pitch; reverse is silent.
- **Memory:** a movie does not have to fit in memory. Frames load ahead of the playhead
  and frames behind it are unloaded to make room. Heavy 4K ProRes 4444 (around 1 Gb/s)
  can load slower than real time; frames already in memory play at full rate.
- **Hardware decoding:** Preferences → General → Hardware decoding (Auto / Off). Auto
  uses the graphics hardware when it can decode the file and software otherwise.
- **Limits:** only one audio stream per file is read, so camera MXF that stores each
  channel as its own mono stream plays its first channel only. Exports carry no sound.

## Contributing

Contributions are welcome. Please review the [CLA](CLA.md) before submitting a pull request.

## License

This project is licensed under the [GNU General Public License, version 2 or (at your option) any later version](LICENSE) (GPL v2 or later). Because the bundled FFmpeg is a GPLv3 build, distributed binaries are effectively under GPLv3.
