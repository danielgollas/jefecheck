# JefeCheck

Professional video frame processing and playback application for color correction, effects, and real-time review of digital cinema content.

## Features

- Real-time playback of image sequences (DPX, EXR, and common formats via OpenImageIO)
- Up to 4 simultaneous video tracks with independent controls
- GPU-accelerated effects pipeline with GLSL shader support
- 1D and 3D LUT (Look-Up Table) support for color grading
- Network-based remote control and synchronization between instances
- Playlist management for sequential review sessions
- Session save/restore with crash recovery

## Building from Source

### macOS

```bash
brew install fltk openimageio openexr curl zlib
cmake -B build
cmake --build build
```

### Linux

```bash
sudo apt install libfltk1.3-dev libopenimageio-dev libopenexr-dev libcurl4-openssl-dev zlib1g-dev
cmake -B build
cmake --build build
```

### Windows

```powershell
vcpkg install fltk openimageio openexr curl zlib
cmake -B build -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

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
