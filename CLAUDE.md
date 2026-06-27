# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

JefeCheck is a professional C++ video frame processing and playback application for color correction, effects processing, and real-time playback of digital cinema content. Originally written 2006-2014, modernized for open-source release in 2026 under GPL v2.

**GitHub:** github.com/danielgollas/jefecheck
**Version:** 1.7.0
**Default branch:** `main`

## Build System

**CMake** is the single build system for all platforms. C++20 on macOS/Linux/Windows.

**UI backend:** Qt6 is the only backend. The FLTK build path was removed in PR-43f after the migration completed.

### macOS (primary development)
```bash
brew install qt openimageio openexr curl zlib cmake freetype
cmake -B build && cmake --build build
```

### Linux (Ubuntu 24.04)
```bash
sudo apt install cmake build-essential qt6-base-dev qt6-base-private-dev libqt6opengl6-dev libopenimageio-dev openimageio-tools libopenexr-dev libimath-dev libcurl4-openssl-dev zlib1g-dev libgl-dev libglu1-mesa-dev libfreetype6-dev
cmake -B build && cmake --build build -j$(nproc)
```

### Windows (MinGW/MSYS2 via GitHub Actions)
Uses MSYS2 with `mingw-w64-x86_64-qt6-base`, `mingw-w64-x86_64-qt6-tools`, and `mingw-w64-x86_64-freetype`. See `.github/workflows/build.yml`.

### Runtime Resources
The app finds FX and fonts via `getApplicationDataPath()` (platform-specific install path). For development, symlink to the source tree:
```bash
ln -sf $(pwd)/src/FX FX && ln -sf $(pwd)/src/fonts fonts
```
Release builds package `src/FX/` and `src/fonts/` alongside the binary.

## Architecture

### Core Manager Pattern
Singleton-style manager classes coordinate subsystems:
- **gfcPlaybackManager** — Timeline, FPS, playback state control
- **gfcTrackManager** — Manages up to 4 video sequences/tracks
- **gfcPlateManager** — Manages 4 display quadrants (plates) with compositing, multi-plate layouts (1x1, 2x1, 1x2, 2x2)
- **gfcPlaylistManager** — Sequential playlist of items
- **gfcSessionManager** — Save/load sessions with crash recovery via XML
- **gfcNetworkManager** — RakNet-based client/server for remote control

### Rendering Pipeline
- **GlViewport** (`Fl_Gl_Window` subclass) — Main OpenGL rendering context. GLAD initialized in first `draw()` call (not in main).
- **gfcPlate** — A display quadrant with color correction, FX stack, and text overlay
- **gfcFX / gfcFXStack** — Shader-based effects pipeline. Shaders in `common/FX/` (.frag/.vert/.jfx)
- **Super Shader** — Dynamically generated GLSL in `gfcPlate::buildShader()` for gamma/exposure/BCS/LUT. Uses ARB extension functions (`glCreateShaderObjectARB`, etc.)

### Text Rendering (`GfcTextRenderer`)
Custom text renderer replacing FLTK's `gl_draw`/`gl_font`. Fixes text squashing in multi-plate layouts.

**Architecture:**
- Singleton `GfcTextRenderer` accessed via `textRenderer()`
- **FreeType** rasterizes glyphs with hinting into a dynamically-sized `GL_ALPHA` atlas texture
- All text draws in a **pixel-exact orthographic projection** via `gluProject` — 1:1 texel-to-pixel mapping regardless of plate projection
- **Two-pass shadow**: dark offset pass + foreground pass (configurable offset, color, blur)
- **Wrapper functions** (`gfc_gl_font`, `gfc_gl_draw`, etc.) match FLTK signatures for minimal call site changes

**Key implementation details:**
- Atlas baked at `fontSize * dpiScale` texels for Retina support
- `drawLine()` uses atlas pixel sizes directly (no dpiScale division) since rendering is in physical pixel space
- `emitQuads()` snaps glyph positions to integer pixels; baseline and cursor snapped before glyph offsets applied
- `GL_NEAREST` filter for pixel-perfect rendering; `GL_LINEAR` available via preferences
- Hinting mode: `FT_LOAD_TARGET_LIGHT` (default, smooth diagonals) or `FT_LOAD_TARGET_NORMAL` / `FT_LOAD_FORCE_AUTOHINT`
- Gamma correction (`powf(coverage, gamma)`) boosts semi-transparent edge pixels for bolder appearance
- `loadFont()`/`loadBoldFont()` invalidate all cached atlases so font changes take effect immediately
- System fonts enumerated via FreeType from platform-specific directories
- Font data kept in memory vectors; `FT_Library` is a static singleton; `FT_Face` created per `bakeAtlas()` call

**Alignment constants must match FLTK:**
```
GFC_ALIGN_CENTER=0x0000, GFC_ALIGN_TOP=0x0001, GFC_ALIGN_BOTTOM=0x0002,
GFC_ALIGN_LEFT=0x0004, GFC_ALIGN_RIGHT=0x0008, GFC_ALIGN_INSIDE=0x0010, GFC_ALIGN_WRAP=0x0080
```

**Files:** `src/gfcTextRenderer.h`, `src/gfcTextRenderer.cpp`

### Image I/O
- **gfcImageLoaderOIIO** — OpenImageIO for all general formats (JPEG, PNG, TIFF, EXR with multi-layer support)
- **gfcImageLoaderDPX** — Custom DPX parser (kept for potential performance, uses `gfcpixelbuffer.h` for bitmap ops)
- **gfcpixelbuffer.h** — Drop-in replacement for removed GFL SDK types (`GFL_BITMAP`, `GFL_COLOR`, `gflResize`, `gflCrop`, etc.)
- Custom EXR loader disabled — OIIO handles EXR natively
- Image saving stubbed out (TODO: implement via OIIO)

### UI
- **Qt6** GUI hosted in `src/qt/`. Single `MainWindow_Qt` with native menu bar, central `GlViewport_Qt` (QOpenGLWidget), and dockable panels (Plate Manager, Timeline, FX Stack, FX Params, LUTs, Playlist).
- Native file dialogs via `QFileDialog`.
- Modal dialogs: About (`AboutDialog_Qt`), System Specs (`MinSpecsDialog_Qt`), Preferences (`PreferencesWindow_Qt`), Render (`RenderDialog_Qt`), Remote Session (`RemoteDialog_Qt`), Load Sequence Manager (`LoadWindowDialog_Qt`).
- Dark VFX theme at `src/qt/theme/jefecheck_dark.qss`.
- Object names follow the dotted-leaf scheme documented in `tests/ui/jefecheck/locators.py` so Mac2/XCUITest can resolve widgets via `identifier ENDSWITH '<leaf>'`.

### Load Sequence Manager

The Qt build's load flow has two paths:

- **Cmd+L → Load Sequence Manager** (`LoadWindowDialog_Qt`). Modal with four track strips. Edits live-update each track's preview frame; while the modal is open, all plates render their tracks' previews (deterministic — `viewport.loadWindowOpen_` is the single source of `gfcPlate::showPreview`). "Load All" closes the modal and fires `trackManager.startLoadingSequence` per non-empty track.
- **Cmd+O → Quick Load…** and **drag-drop on viewport**: existing fast path. `jefe::qt::loadFileIntoPlate` runs immediately; no preview indirection.

`Cmd+Shift+O` is reserved for the future Open Session feature (FLTK convention) and is intentionally unbound today.

OIIO loader resize uses `OIIO::ImageBufAlgo::resize` with `Filter2D::create` so the Preferences → Engine → Default decode filter setting (`nearest` / `triangle` / `mitchell` / `lanczos3`, default `lanczos3`) actually controls scale quality.

### Render & export (`RenderDialog_Qt`, `gfcImageSaverOIIO`, `VideoEncoder_Qt`)

The Render dialog (File → Render…, F6) renders the active plate's frame range and writes:
- **Stills** — JPEG / PNG / TIFF / TGA / BMP / EXR via OIIO (`gfcImageSaverOIIO`). The render path is `gfcPlateManager::renderPlate` → `gfcPlate::draw3DrectWithFX(forRender)` → FBO readback → OIIO write. The caller (`onRenderClicked`) **must** make the viewport GL context current first (resolve the MainWindow via `parentWidget()`, not `window()`).
- **Video** — H.264 / H.265 (mp4) / ProRes (mov) by rendering a temp PNG sequence then encoding with the **FFmpeg CLI** (`VideoEncoder_Qt`, `QProcess` — not libav*).

Controls: output **resolution** (`gfcRenderParams.outWidth/outHeight` size the FBO; 0 = source), per-format quality (JPEG quality/progressive/subsampling, PNG level, TIFF/EXR compression, EXR depth, **8/16-bit** PNG/TIFF, video CRF-vs-bitrate + preset). **16-bit / EXR renders use a float (`RGBA16F`) FBO** for real precision (8-bit renders keep the 8-bit FBO). The render is incremental (`QTimer::singleShot(0)` per frame — responsive + cancellable) with a progress bar and a "Show in folder" status link.

FFmpeg is bundled per-platform (release packaging *and* `CMakeLists.txt` fetch a GPL static build into the bundle); resolved at runtime via `$JEFECHECK_FFMPEG` → QSettings → bundled → PATH. Headless verification: `--render-test` / `--video-test` / `--playlist-test`. See `developer_notes.md` §18–21.

## Key Dependencies

| Library | Purpose | License |
|---------|---------|---------|
| Qt6 | GUI framework (Widgets + OpenGLWidgets) | LGPL v3 |
| OpenImageIO | Image I/O (all formats) | BSD |
| FreeType | Font rasterization with hinting | FreeType License (BSD-like) |
| GLAD | OpenGL loading (compatibility profile 3.3) | Public domain |
| CLI11 | CLI argument parsing | BSD |
| OpenEXR / Imath | EXR support (via OIIO) | BSD |
| RakNet | Networking (vendored in src/) | GPL v2 |
| libcurl | HTTP | MIT |
| zlib | Compression | zlib |
| xmlParser | XML parsing (vendored in src/) | BSD |
| FFmpeg | Video export (H.264/H.265/ProRes) — invoked as a CLI subprocess, **not linked**; bundled per-platform | GPL (bundled static build) |

**Removed dependencies:** FLTK 1.4 (replaced by Qt6 in PR-43f), GFL SDK (proprietary), FLU (proprietary), Boost, GLEW, Botan, stb_truetype

**Bundled fonts:** Roboto Regular/Bold (Apache 2.0, default), Inter Regular/Bold (SIL OFL), DejaVu Sans Regular/Bold (Bitstream Vera)

## Directory Layout

```
src/                    All C++ source. Entry point: main.cpp
src/FX/                 Effect shaders (.frag/.vert), metadata (.jfx), LUTs (.lut/.cub)
src/fonts/              Bundled TTF fonts (Roboto, Inter, DejaVu Sans)
third_party/glad/       Generated GLAD loader (OpenGL 3.3 compatibility)
third_party/cli11/      CLI11 single-header argument parser
scripts/                Build/setup scripts (build_linux.sh)
docs/                   User manual, quick start, design specs, plans
docs/manual.md          User manual (converted from 2014 .docx)
docs/quick-start.md     Quick reference guide
docs/manual-images/     Screenshots (2014, need updating)
.github/workflows/      CI (build.yml) and releases (release.yml)
```

## Platform-Specific Notes

### macOS
- OpenGL is Metal-backed (GL 2.1 via Apple's translation layer). OpenGL is deprecated but functional.
- `GLhandleARB` is `void*` (not `GLuint`). Shader functions use ARB variants (`glShaderSourceARB`, `glLinkProgramARB`). Do NOT mix ARB and modern GL calls.
- GLAD must be initialized via `mw.vp->make_current()` + `gladLoadGL()` before any GL calls in `main()`.
- `Fl_Gl_Window::pixels_per_unit()` returns 2.0 on Retina displays.
- FreeType: use `<OpenGL/glu.h>` (not `<GL/glu.h>`).

### Linux
- X11 `#define None 0L` conflicts with OIIO's `enum { None=0 }` and CLI11 internals. Include order matters: CLI11 first (top of main.cpp), then `#undef None` before OIIO headers (in `gfcimageloaderoiio.h`).
- OIIO 2.x `tostring()` API differs from 3.x. Metadata extraction uses manual type checking with `#if OIIO_VERSION_MAJOR >= 3` guard.
- GCC is stricter than Clang: missing returns are SIGILL (not just UB), `const` correctness enforced, `mutable` required for mutexes in const methods.
- Link GLU explicitly (`-lGLU`).
- `#include <cmath>` required for `powf`/`floorf` (GCC doesn't implicitly include).

### Windows (MinGW/MSYS2)
- `using namespace std;` in headers causes `std::byte` vs Windows `byte` conflict. Removed from `mtpoly.h`.
- `alloca.h` doesn't exist — use `malloc.h`. Guard: `#if (defined(__GNUC__) || defined(__GCCXML__)) && !defined(_WIN32)`
- `GLhandleARB` casts require `(GLuint)(uintptr_t)` on 64-bit.
- Link: `glu32 opengl32 ws2_32 winmm iphlpapi`.
- `-fpermissive` needed for `void*` to `long` casts in FLTK callbacks.
- `#include <windows.h>` must come before `<GL/glu.h>` (GLU callbacks need Windows types).

## Key Code Patterns

### Qt UI code — read `developer_notes.md` first

Before touching anything under `src/qt/` or wiring a new Qt control into the rendering chain, read **`developer_notes.md`** at the repo root. It captures the patterns the FLTK→Qt port landed on:

- **TU separation**: only `src/qt/SequenceLoadBridge_qt.cpp` may include the rendering-chain managers (`gfcPlateManager`, `gfcSequence`, etc.). Glad and Qt's QOpenGLWidget refuse to share a TU on macOS. Every other `src/qt/*.cpp` routes through `jefe::qt::*` bridge accessors in `SequenceLoadBridge_qt.h`.
- **Plate-card slots must propagate**: writing to the Qt plate GUI alone (`gui->setGamma(v)`) is a silent no-op — the plate's actual fields stay stale. Every plate-card slot follows the GUI write with `jefe::qt::propagatePlateChanges()`.
- **`updatePlatesFromGUI` vs `updateAllFromGUI`**: the latter resets layout (framingMode) and active-quad, which the Qt build drives via separate paths. Always use `updatePlatesFromGUI` from Qt-side update points.
- **macOS drag-perf playbook**: `Qt::QueuedConnection` for slot wiring, targeted signals (`plateTransformChanged` / `plateColorChanged`) over the heavy `plateStateChanged`, cache-gated widget writes in `refreshFromState`, 60Hz throttle on continuous emissions. AppKit's accessibility cascade fires on every Qt widget write regardless of whether the value changed — caching reduces *write count*, not *per-write cost*.
- **`gfcPlate::showPreview` has one writer**: `setLoadWindowOpen` via `setAllPlatesShowPreview`. Don't add another.
- **CBArgs enum**: `LOOPMODEONCE_ID` etc. are positional values 22/23/24 inside a flat enum, not 0/1/2. Translate combo indices at the bridge boundary.
- **Plate-card layout is orientation-aware & fixed-size**: the card has two internal layouts (wide-short / narrow-tall) swapped via a rebuilt content widget; the Plate Manager keys off `QDockWidget::dockLocationChanged` and fixes its own extent. Gotchas (see developer_notes.md §11): a `QComboBox` sizes to its longest item — use `QSizePolicy::Ignored` for a fill combo so it can't balloon a fixed card; never read a widget's `sizeHint()` synchronously during a dock/content swap (defer with `QTimer::singleShot(0)` + safety floors); use `resizeDocks` to pull a shared dock row to the panel's size rather than fighting a taller neighbor.
- **AspectCropCombo is a `QToolButton`, not an editable combo** (developer_notes.md §12): an editable combo only opens its popup on the arrow, so a body click never opened it. Aspect (reshape) and crop/letterbox are orthogonal in the renderer — keep both reachable. The default shows as "source"/native ratio but its canonical value stays "original" (via the list item's `Qt::UserRole`).
- **Playback FPS pacing** (developer_notes.md §13): `gfcPlaybackManager` must seed `targetFPS`/`timePerFrame` in its constructor (the fps spinbox's initial value is set before its signal is connected). `updateTimestep()` reads `std::chrono::steady_clock`, not the integer-ms `gfcTimer`. The Qt playback `QTimer` runs fine (4ms, `PreciseTimer`) with the per-tick GL upload gated on `hasPendingTextureUploads()` and UI read-back throttled to ~60Hz. The on-screen FPS is an EMA with a slow display refresh, a 2%-of-target deadband, and 1-decimal formatting — it's a "keeping up?" indicator, not raw instantaneous timing.
- **Render/export** (developer_notes.md §18–20): `getImageSaverInstance` was a NULL-deref stub — the OIIO saver (`gfcImageSaverOIIO`) writes all still formats. The render dialog must get the viewport via `parentWidget()` (a modal `QDialog`'s `window()` is itself, not the MainWindow → null vp → black frames). `renderParams.outWidth/outHeight` size the FBO (resolution control); 16-bit/EXR renders need a float FBO (`createFloatFBO`) for real precision. Video export shells out to the **FFmpeg CLI** (`VideoEncoder_Qt`/`QProcess`, never libav*). Render is incremental (`singleShot(0)` per frame, cancellable); never a `QThread` (the GL context is thread-affine).
- **Pick subsystem** (developer_notes.md §21): in-viewport overlay dragging (histogram, AOI) goes through the GL color-pick pass, registered in `initializeRenderingChain` and driven by `jefe::qt::viewportPick*` with the GL context current — not Qt mouse hit-testing.

### Image Loading Flow
1. `gfcFrame::loadFrame()` creates a loader based on file extension
2. `GFCLOADER_GFL` and `GFCLOADER_FIL` → `gfcImageLoaderOIIO` (replaced GFL)
3. `GFCLOADER_EXR` → `gfcImageLoaderOIIO` (custom EXR loader disabled)
4. `GFCLOADER_DPX` → `gfcImageLoaderDPX` (custom parser)
5. Loader fills `gfcGLFrameInfo` (GL format, data pointer, target)
6. `gfcFrame::generateTexture()` uploads to GL via `glTexImage2D`
7. Texture target is `GL_TEXTURE_RECTANGLE_ARB` — tex coords are in pixel space, not normalized

### OIIO Loader (`gfcimageloaderoiio.cpp`)
- Discovers EXR layers from channel names (e.g. `R,G,B,right.R,right.G,right.B`)
- Reads selected layer's channels using `inp->read_image(0, 0, chBegin, chBegin+srcChannels, ...)`
- Converts RGB→BGRA (swizzle) for OpenGL `GL_BGRA` format
- Supports texture format modes: `GFC_4BPC`, `GFC_8BPC`, `GFC_16BPC`, `GFC_16HALF`, `GFC_S3TCDX1`
- Sets `texCoords` to pixel space `(0, 0, width, height)` and `quadSizeX/Y` for the viewport

### Shader System
- `gfcPlate::startSuperShader()` / `stopSuperShader()` — dynamically built GLSL for color correction
- `gfcPlate::buildShader()` constructs vertex + fragment shader source strings based on active features (LUT, gamma/exposure, BCS, RGBA masks)
- `gfcFX::load()` / `gfcFX::bind()` — loads .jfx effect definitions, compiles shaders from .frag/.vert files
- All shader handles (`ssProgram`, `ssVertexShader`, `ssFramgmentShader`) MUST be initialized to 0 in constructors — uninitialized handles cause trace traps on macOS
- Text renderer disables active shader programs (`glGetHandleARB`/`glUseProgramObjectARB(0)`) before drawing text quads

### File Chooser (`gfcfilechooser.h`)
- `NativeFileChooser` wraps `Fl_Native_File_Chooser` with `Fl_File_Chooser` API compatibility
- Converts FLTK filter format `"Desc (*.{ext1,ext2})"` to native format `"Desc\t*.{ext1,ext2}"`
- Writes selected path to global `gFilename[2048]` for legacy code compatibility

### Preferences System
- Settings saved/loaded via XML (`gfcStructures.cpp`) using `saveSetting()`/`setWidgetFromNode()` templates
- Text rendering settings: font path, size, color, opacity, shadow, hinting mode, filter mode, gamma — all persisted
- `PreferencesCB` callback in `UICallbacks.cpp` applies all preference changes including text renderer settings
- System font enumeration (`enumerateSystemFonts()`) scans platform-specific directories via FreeType

## Known Issues

- **`gfcTrack.cpp` is an exact duplicate of `gfcSequence.cpp`** — excluded from build in CMakeLists.txt. The `subsequence` branch was an incomplete refactoring.
- **Image saving not implemented** — `gfcimagesaver.cpp` has stubs for OIIO-based saving (TODO).
- **Image-based LUT loading disabled** — `trilerp.cpp` IMAGELUT2D case returns -1 (needs OIIO image reading).
- **Custom EXR loader disabled** — `gfcimageloaderexr.cpp` excluded from build due to OpenEXR 3.x API changes. OIIO handles EXR.
- **Text rendering quality** — FreeType with light hinting is good but not Core Text quality on macOS. Diagonal strokes (/, 7, k) have some stairstepping at small sizes. Possible future improvement: render via Core Text on macOS.
- **Preferences font dropdown z-order** — `Fl_Choice` popup may appear behind the preferences window on macOS. Workaround: make window modal.

## CI & Releases

- **Build CI** (`.github/workflows/build.yml`): Runs on push to `main` and all PRs targeting `main`. Builds on macOS (ARM64), Linux (x64 Ubuntu 24.04), and Windows (x64 MSYS2/MinGW).
- **Release CI** (`.github/workflows/release.yml`): Triggers on `v*` tags. Builds Release binaries on all 3 platforms, packages with FX plugins and fonts, publishes to GitHub Releases via `softprops/action-gh-release`.
- **To publish a release:** `git tag v1.7.0 && git push origin v1.7.0`
- **Artifacts:** `jefecheck-linux-x64.tar.gz`, `jefecheck-macos-arm64.tar.gz`, `jefecheck-windows-x64.zip`

## Documentation

- **User Manual:** `docs/manual.md` (1,104 lines, converted from 2014 .docx)
- **Quick Start:** `docs/quick-start.md` (250 lines, task-oriented how-to reference)
- **Screenshots:** `common/Manual/Images/` (41 images from 2014, need retaking for current UI)
- **Original .docx files:** `common/Manual/JefeCheckManual.docx`, `common/Manual/JefeCheckQuickStart.docx` (archived)

## Notes

- No automated test suite exists.
- The `gfc` prefix on classes stands for the project's internal namespace convention.
- UI windows follow: `*Window.fl` → FLUID generates `*Window.cxx` + `*Window.h`. Editing `.cxx` directly is fine since FLUID is not actively used.
- Never put `using namespace std;` in header files — causes `std::byte` conflict on Windows.
- Always initialize GL object handles to 0 in constructors — uninitialized handles cause crashes when `glDeleteObjectARB` is called.
- GLAD must be initialized before any GL calls. On macOS, `draw()` is called during `Fl::check()` before `main()` reaches the GL init code — GLAD is initialized in `GlViewport::draw()` on first call.
- Version string is defined in `src/gfcStructures.h` (`JEFE_VERSION`), `CMakeLists.txt` (`project VERSION`), and `src/aboutWindow.cxx`/`.fl` (splash label).
- Legacy branches (`floatEXR`, `gfcTrackIntroduced`, `subsequence`, `trackloading`) contain incomplete refactoring work from the original development period.
