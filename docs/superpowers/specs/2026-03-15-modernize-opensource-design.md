# JefeCheck Open-Source Modernization Design

## Overview

Modernize JefeCheck for open-source release under GPL v2. Replace proprietary and outdated dependencies, unify the build system under CMake, target C++20, and achieve a working MVP (sequence playback) on macOS first, then Linux and Windows.

## License Strategy

- **License:** GPL v2
- **CLA:** Contributor License Agreement from day one, allowing future relicensing (e.g., to Apache 2.0 once RakNet is eventually replaced)
- **Rationale:** GPL v2 is compatible with RakNet's GPL v2 licensing. CLA preserves the option to relicense once all GPL dependencies are removed.

## Dependency Decisions

### Keep As-Is
| Library | License | Purpose |
|---------|---------|---------|
| RakNet | GPL v2 | Networking, RPC, reliable UDP — core feature |
| FLTK | LGPL v2 | GUI framework |
| OpenEXR / IlmBase | BSD | EXR image format support |
| libcurl | MIT | HTTP communication |
| zlib | zlib | Compression |
| xmlParser | BSD | XML parsing (vendored in-tree) |

### Replace
| Old | New | License | Reason |
|-----|-----|---------|--------|
| GFL SDK | OpenImageIO (OIIO) | BSD | GFL is proprietary, incompatible with open source |
| FLU | Native FLTK widgets | LGPL v2 | FLU is proprietary (Ohio State) |
| Boost | C++20 STL + CLI11 | BSL / BSD | All Boost usage has STL equivalents in C++20 |
| GLEW | GLAD | Public domain | GLEW is vendored and outdated (~2008) |

### Remove Entirely
| Component | Reason |
|-----------|--------|
| Botan | Only used for license activation, which is removed |
| Activation system | Not needed for open-source release |

### Deferred
| Topic | Decision |
|-------|----------|
| Metal support | Stay with OpenGL; Metal is a future project if Apple removes OpenGL |
| RakNet replacement | Future initiative; networking works and is GPL-compatible |

## Dependency Replacement Details

### GFL SDK to OpenImageIO
- Replace `gfcimageloadergfl.cpp` with new `gfcimageloaderoiio.cpp` implementing the same `gfcImageLoader` interface
- Keep `gfcimageloaderdpx.cpp` (custom in-tree parser, potentially faster for DPX)
- Keep `gfcimageloaderexr.cpp` or consolidate into OIIO (OIIO handles EXR natively) — decide during implementation
- Remove: `gflC.h`, `libgfl.h`, `libgfle.h`, `gflCFormat.h`, all GFL references

### FLU to Native FLTK
| FLU Widget | FLTK Replacement |
|------------|------------------|
| `Flu_File_Chooser` | `Fl_File_Chooser` |
| `Flu_Combo_Box` | `Fl_Input_Choice` |
| `Flu_Button` | `Fl_Button` |
| `Flu_Choice_Group` | `Fl_Tabs` or `Fl_Group` |
| `Flu_Tree_Browser` | `Fl_Tree` |
| `Flu_Spinner` | `Fl_Spinner` |

### Boost to C++20 STL
| Boost Component | C++20 Replacement |
|-----------------|-------------------|
| `boost::filesystem` | `std::filesystem` |
| `boost::thread`, `mutex`, `condition_variable` | `std::thread`, `std::mutex`, `std::condition_variable` |
| `boost::bind` | `std::bind` or lambdas |
| `boost::ref` | `std::ref` |
| `boost::program_options` | CLI11 (BSD, header-only) |

### GLEW to GLAD
- Generate GLAD loader for required OpenGL version/extensions
- Replace `#include "glew.h"` with `#include <glad/glad.h>`
- Replace `glewInit()` with `gladLoadGL()`
- Remove vendored GLEW source: `glew.c`, `glew.h`, `glext.h`, `glxew.h`, `wglew.h`

### Activation System Removal
- Remove `activatorWindow.*`, `activatorCallbacks.*`
- Remove Botan includes and references from `gfcStructures.h`
- Remove license-check code paths in `main.cpp`

## Build System

### CMake Migration
- Single `CMakeLists.txt` at project root, targeting the final dependency set from the start (no old libraries referenced)
- C++20 enforced via `CMAKE_CXX_STANDARD 20`
- `find_package` for: FLTK, OpenImageIO, OpenEXR, OpenGL, curl, zlib
- GLAD and CLI11 included in-tree (small, generated/header-only)
- Platform detection for OS-specific source files
- Out-of-source build (`build/`, gitignored)

### Remove Old Build Systems
- `win/jefecheck.sln`, `win/jefecheck.vcxproj` and filters
- `src/Makefile.am`
- `linux/jefecheck/configure.in`, `Makefile.cvs`, autotools files
- Mac build scripts

### Build Workflow
```bash
# macOS
brew install fltk openimageio openexr curl zlib
cmake -B build
cmake --build build

# Linux
sudo apt install libfltk1.3-dev libopenimageio-dev libopenexr-dev libcurl4-openssl-dev zlib1g-dev
cmake -B build
cmake --build build

# Windows (future)
vcpkg install fltk openimageio openexr curl zlib
cmake -B build -DCMAKE_TOOLCHAIN_FILE=[vcpkg-toolchain]
cmake --build build
```

## Repository Cleanup

### Remove
- `common/website/` — old website, will become GitHub Pages later
- `common/licenseGenerator/` — dead with activation removal
- `win/buildDependencies/` — vendored binaries, CMake replaces this
- `win/InstallerFolder/NSIS/` — bundled NSIS distribution (keep `.nsi` scripts)
- Vendored GLEW source files

### Add
- `LICENSE` — GPL v2 text
- `CLA.md` — Contributor License Agreement
- `README.md` — project description, build instructions, contribution guidelines
- Updated `.gitignore` — build directories, IDE files

## MVP Definition

**Goal: Build and play back an image sequence on macOS.**

### Must Work
1. App launches, main window appears (FLTK)
2. User opens a file/sequence via file chooser
3. Frames load from disk (via OIIO or existing DPX/EXR loaders)
4. Frames render to the OpenGL viewport
5. Playback controls work (play, stop, scrub, frame step)
6. At least one plate displays correctly with basic color

### Can Be Broken/Disabled for MVP
- Networking (compiles but untested)
- Effects/FX stack (shaders may need GLAD updates)
- Multi-track (single sequence is sufficient)
- Playlists
- Render/export
- Session save/load

## Implementation Phases

### Phase 1 — Clean the Tree
- Remove dead directories and files (website, license generator, build deps, NSIS dist)
- Remove activation system (Botan, activator code)
- Add LICENSE, CLA.md, README.md
- Update .gitignore

### Phase 2 — CMake Build System
- Write root CMakeLists.txt targeting final dependency set (OIIO, FLTK, GLAD, C++20 STL, OpenGL, curl, zlib — not GFL, FLU, GLEW, or Boost)
- Get the project compiling (expect many errors initially from dependency swaps)

### Phase 3 — Dependency Swaps
Each swap is its own commit, done in this order:
1. GLEW to GLAD — rendering infrastructure first
2. Boost to C++20 STL — touches many files but mechanical
3. FLU to native FLTK — UI widget replacements
4. GFL to OpenImageIO — image loading, needed for MVP validation

### Phase 4 — Get It Running
- Fix runtime issues on macOS
- Get the window launching
- Get single-sequence playback working
- This is the MVP

### Phase 5 — Cross-Platform
- Test/fix Linux build
- Test/fix Windows build (vcpkg integration)
- CI via GitHub Actions for all three platforms

## Platform Priority
1. **macOS** — primary development target, MVP here first
2. **Linux** — should be straightforward once CMake works
3. **Windows** — last, needs vcpkg for dependency management
