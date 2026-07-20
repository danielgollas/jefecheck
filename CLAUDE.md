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
Custom renderer replacing FLTK's `gl_draw`/`gl_font` (fixes text squashing in multi-plate layouts). Singleton via `textRenderer()`; FreeType glyphs in a `GL_ALPHA` atlas, drawn in a pixel-exact ortho projection; two-pass shadow; FLTK-signature wrappers (`gfc_gl_font`, `gfc_gl_draw`). **Implementation details, Retina/hinting/gamma notes, and preference persistence: `developer_notes.md` §22.** Files: `src/gfcTextRenderer.{h,cpp}`.

**Alignment constants must match FLTK:**
```
GFC_ALIGN_CENTER=0x0000, GFC_ALIGN_TOP=0x0001, GFC_ALIGN_BOTTOM=0x0002,
GFC_ALIGN_LEFT=0x0004, GFC_ALIGN_RIGHT=0x0008, GFC_ALIGN_INSIDE=0x0010, GFC_ALIGN_WRAP=0x0080
```

### Image I/O
- **gfcImageLoaderOIIO** — OpenImageIO for all general formats (JPEG, PNG, TIFF, EXR with multi-layer support)
- **gfcImageLoaderDPX** — Custom DPX parser (kept for potential performance, uses `gfcpixelbuffer.h` for bitmap ops)
- **gfcpixelbuffer.h** — Drop-in replacement for removed GFL SDK types (`GFL_BITMAP`, `GFL_COLOR`, `gflResize`, `gflCrop`, etc.)
- Custom EXR loader disabled — OIIO handles EXR natively
- Image saving stubbed out (TODO: implement via OIIO)

### UI
- **Qt6** GUI hosted in `src/qt/`. Single `MainWindow_Qt` with native menu bar, central `GlViewport_Qt` (QOpenGLWidget), and dockable panels (Plate Manager, Timeline, FX, LUTs, Playlist).
- **Menu bar** (JEF-17): **File · View · Panels · Help**. *View* = display + active-plate transforms (Fullscreen, Histogram, Status Bar, CC Favorites, Fit `F`/Flip `V`/Flop `M`/Text `T`/Reset `Ctrl+R`/Reset-CC `Shift+R`). *Panels* (renamed from the legacy "Dialogs" menu — not "Window", to avoid implying macOS window-management commands) is the single home for every dock toggle (`F2`/`F3`/`F4`) plus Remote (`F5`), Render (`F6`), Hide Controls (`Ctrl+Alt+F`). Bare **`H` toggles the on-screen help overlay** (flop moved to `M`); the overlay text is kept in sync with the real key bindings. See `developer_notes.md` §28.
- **FX panel** (`FXParamPanel_Qt`, dock "FX", F3) — one combined effect-controls panel for the active plate: a hierarchical "+ Add FX" menu (categorized by each FX's `menuName`), per-FX cards (active checkbox + remove button + inline param editors: float/bool/choice, texture source picker (Previous / Track A–D), and cube/1D-LUT pickers), and drag-to-reorder the stack. Texture/cube/LUT pickers match the FLTK Fl_Choice; cube/LUT combos store the global lutManager index (carried in item data), not the list position. See `developer_notes.md` §23. All FX autoload at startup, so there is no separate FX browser. See `developer_notes.md` §23.
- **Playlist dock** (`PlaylistPanel_Qt`) — snapshots the current multi-track setup as a playlist item (Add Current), or builds items from arbitrary file sets (Add Files…). Each item shows collapsible per-track detail cards (filename, range, scale); Compact and Full-paths checkboxes control label verbosity. Drag-drop: drop a `.jpl` file to replace the list, drop media onto a card to append tracks to that item, drop media on empty space to add a new item. Keyboard: Enter/double-click to load, Delete/Backspace to remove, Shift+↑/↓ to reorder (drag-handle on cards is affordance only — reorder via buttons/keys to keep the backing vector in sync). Scale override combo applies a decode scale to all tracks on load. Auto-advance (loop-once → advance to next item and resume play) + Loop (wrap at end). See `developer_notes.md` §25. Playlists can also be opened from **File → Open Playlist…**, with a **File → Recent Playlists** submenu (JEF-18) mirroring Recent Sessions (`QSettings "Playlist/recent"`, cap 5).
- Native file dialogs via `QFileDialog`.
- Modal dialogs: About (`AboutDialog_Qt`), System Specs (`MinSpecsDialog_Qt`), Preferences (`PreferencesWindow_Qt`), Render (`RenderDialog_Qt`), Load Sequence Manager (`LoadWindowDialog_Qt`).
- **Preferences** (`PreferencesWindow_Qt`, File → Preferences…) is a sidebar-navigated dialog with five sections: **General** (background RGB color + checkerboard toggle, default browse path with picker, start fullscreen, open Load Window at startup, crash-recovery session, on-launch session behavior, aspect-fit opacity, thumbnails, feedback-message size/fade — plus the folded Playback & Engine controls: frames-to-read-ahead queue cap, default decode filter, default bit depth), **Text** (size, color, hint mode, filter, gamma, shadow enable/offset/blur/color — live preview via `GfcTextRenderer`), **Formats** (EXR: ignore display window, ignore header aspect ratio), **Search Paths** (enable, recursive, path list with add/remove), and **Remote** (nickname, chat fade/auto-fade/background/font-size/opacity/line-count, remote-pointer fade/color). Color pickers are live (real-time viewport preview) and the viewport repaints while the dialog is open. Removed as part of the JEF-16 audit: the standalone "Paths" tab, the FLTK placeholder pages, the inert engine knobs (rendering engine, vsync, decode partitions, force PBO), `balanceReads` (superseded by the read-ahead queue cap), and the send/auto-accept load-request toggles. See "Preferences System" below and `developer_notes.md` §27.
- The Remote Session dialog (`RemoteDialog_Qt`) is **modeless** (persistent, `show()`/`raise()`, owned by `MainWindow_Qt::remoteDialog_`) so status/participants/chat stay visible during a live session — see `developer_notes.md` §26.
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

**Before touching anything under `src/qt/` or wiring a new Qt control into the rendering chain, read `developer_notes.md` at the repo root.** It documents (in full, with line/file refs) every pattern the FLTK→Qt port landed on. Index of load-bearing gotchas:

- §1 **TU separation** — only `SequenceLoadBridge_qt.cpp` includes the rendering-chain managers; everything else routes through `jefe::qt::*` accessors (glad + QOpenGLWidget can't share a TU on macOS).
- §2 `updatePlatesFromGUI` vs `updateAllFromGUI` (use the former from Qt) · §3 **plate-card slots must call `propagatePlateChanges()`** (GUI write alone is a silent no-op) · §4 `gfcPlate::showPreview` has one writer (`setLoadWindowOpen`).
- §5–6 **macOS drag-perf playbook** (QueuedConnection, targeted signals, cache-gated writes, 60Hz throttle; AppKit a11y cascade fires per write).
- §8 CBArgs enum (`LOOPMODEONCE_ID` = positional 22/23/24, translate at the bridge) · §11 plate-card orientation-aware fixed-size layout · §12 AspectCropCombo is a `QToolButton` (aspect vs crop orthogonal).
- §13 **playback FPS pacing** (seed `targetFPS` in ctor; `steady_clock`; on-screen FPS is an EMA indicator) · §18–20 **render/export** (OIIO saver; viewport via `parentWidget()`; float FBO for 16-bit/EXR; FFmpeg CLI not libav*; incremental, never a `QThread`) · §21 **pick subsystem** (GL color-pick pass, GL context current).
- §26 **remote-session runtime** (pump always-on outside `needsPlaybackTick`; TU-safe getters for status/participants/chat/errors; `remoteDialog_` modeless lazy-created; chat+pointer overlay in `paintGL`; `--remote-test` two-process harness).

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

### Preferences System
- Persistence is **Qt `QSettings`** via `src/qt/qt_prefs_persist.{h,cpp}` — the single store for every preference. `loadPreferences()` reads all keys into the global `sett` (gfcSettings) at startup; `writePreferences()` writes `sett` back on Preferences → Done. The legacy XML `saveSettings()`/`readSettings()` (`gfcStructures.cpp`) are dead stubs, not called anywhere in the Qt build.
- `PreferencesWindow_Qt` snapshots `sett` into `sett_backup_` when the dialog opens; every page mutates `sett` live as the user edits (so other UI, e.g. the viewport background, updates immediately). Cancel restores `sett = *sett_backup_` verbatim instead of writing anything.
- Text-renderer prefs (`GfcTextRenderer`, a separate singleton, not part of `sett`) use **deferred persistence**: live edits call `textRenderer()` setters directly for instant preview, `QSettings` `Text/*` keys are written only on Done (`writeTextPrefs()`), and Cancel calls `jefe::qt::applyTextPrefs()` to re-read the last-saved `Text/*` values back into the renderer, reverting unsaved live edits.
- File dialogs are native `QFileDialog` (the old FLTK `NativeFileChooser` wrapper is gone).
- See `developer_notes.md` §27 for the full persistence model, section-by-section key list, checkerboard background, and EXR display-window/aspect wiring.

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
- Always initialize GL object handles to 0 in constructors (uninitialized handles crash `glDeleteObjectARB`). GLAD must be initialized before any GL call — done on first `GlViewport::draw()`.
- Version string lives in three places: `src/gfcStructures.h` (`JEFE_VERSION`), `CMakeLists.txt` (`project VERSION`), and `src/aboutWindow.cxx`/`.fl` (splash label).
- Legacy branches (`floatEXR`, `gfcTrackIntroduced`, `subsequence`, `trackloading`) contain incomplete refactoring from the original development period.
