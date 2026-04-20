# FLTK-to-Qt Migration Plan

## Context

JefeCheck has used FLTK since 2006. The UI toolkit is showing its age — limited widget styling, no native dark mode, bitmap-based text rendering, and poor HiDPI support. Qt provides modern widget capabilities, proper GL integration via QOpenGLWidget, signal/slot architecture, and native platform integration.

The codebase already has a partial GUI abstraction layer: 5 subsystems (`gfcPlateGUI`, `gfcPlaybackGUI`, `gfcSequenceGUI`, `gfcNetworkClientGUI`, `gfcNetworkServerGUI`) use abstract base classes with `*_fltk` concrete implementations. This plan extends that existing pattern to cover all remaining FLTK dependencies, then builds Qt implementations behind the same interfaces.

**Goal:** Replace FLTK with Qt6 while maintaining a working app throughout the transition. Dark VFX-industry theme (matching Nuke/DaVinci Resolve aesthetic).

## Scope

- **76 of 404 source files** use FLTK (19%)
- **354 callback instances**, **85 Fl::check()/wait() calls**, **150+ Fl::event_* calls**
- **14 active FLUID-generated windows** (~7,700+ lines of .cxx)
- **6 custom Fl_*_gfc widgets**
- **~8,000-10,000 lines** are already FLTK-free (reusable as-is)

## Existing Abstraction Pattern (template for migration)

```
src/gfcplategui.h           ← abstract interface
src/gfcplategui.cpp         ← shared logic
src/gfcplategui_fltk.h      ← FLTK implementation
src/gfcplategui_fltk.cpp
```

Same pattern exists for: `gfcplaybackgui`, `gfcsequencegui`, `gfcnetworkclientgui`, `gfcnetworkservergui`

---

## Phase 0: Complete the UI Abstraction Layer (3-4 weeks)

### 0A. Define missing interface classes

Create in `src/ui/`:
- **IGLViewport.h** — Abstract GL rendering surface (draw, handle events, makeCurrent, geometry queries, pixelsPerUnit)
- **IEventSystem.h** — Replaces 150+ `Fl::event_*` calls (mouseX/Y, isCtrl/Shift/Alt, isKeyDown, mouseButton)
- **IMainWindow.h** — Replaces concrete MainWindow (show, hide, getViewport, toggleFullscreen)
- **IApplication.h** — Replaces Fl:: namespace (processEvents, wait, run, screenGeometry, quit)
- **IFileChooser.h** — Replaces NativeFileChooser wrapper
- **IMessageDialog.h** — Replaces fl_alert/fl_choice/fl_message

### 0B. Extract gfcPlate's direct FLTK widget pointers

`gfcPlate.h` lines 234-261 hold 23 FLTK widget pointers directly (Fl_Choice*, Fl_Value_Slider*, etc.) despite already having `gfcPlateGUI* myGUI`. Remove direct pointers, route through myGUI.

### 0C. Create gfcPlateManagerGUI abstraction

`gfcPlateManager` holds Fl_Group*, Fl_Choice_gfc*, Fl_Round_Button* directly. Extract into abstract `gfcPlateManagerGUI` + `gfcPlateManagerGUI_fltk`.

### 0D. Wrap GlViewport behind IGLViewport

GlViewport currently inherits Fl_Gl_Window. Make it implement IGLViewport. Route all Fl::event_* through IEventSystem.

**Files to create:** `src/ui/IGLViewport.h`, `src/ui/IEventSystem.h`, `src/ui/IMainWindow.h`, `src/ui/IApplication.h`, `src/ui/IFileChooser.h`, `src/ui/IMessageDialog.h`, `src/gfcplatemanagergui.h`, `src/gfcplatemanagergui_fltk.h/.cpp`

**Files to modify:** `src/gfcPlate.h/.cpp`, `src/gfcplatemanager.h/.cpp`, `src/GlViewport.h/.cpp`

**Verification:** App compiles and runs identically on FLTK. All FLTK usage behind abstractions.

---

## Phase 1: Decouple Core Logic (4-5 weeks)

### 1A. Break UICallbacks.cpp (2,550 lines) into domain modules

Split into `src/callbacks/`:
- `PlaybackCallbacks.cpp` — play/pause/frame nav
- `PlateCallbacks.cpp` — zoom/pan/color correction
- `LoadCallbacks.cpp` — file loading
- `FXCallbacks.cpp` — FX stack
- `LUTCallbacks.cpp` — LUT management
- `NetworkCallbacks.cpp` — remote sessions
- `PreferencesCallbacks.cpp` — settings
- `MenuCallbacks.cpp` — menu bar
- `RenderCallbacks.cpp` — rendering

### 1B. Eliminate ~25 global extern objects

Replace with `AppContext` singleton:
```cpp
class AppContext {
    static AppContext& instance();
    IMainWindow& mainWindow();
    IGLViewport& viewport();
    gfcTrackManager& trackManager();
    gfcPlateManager& plateManager();
    // ... etc
};
```

Mechanical refactor: `extern MainWindow mw;` → `AppContext::instance().mainWindow()`

### 1C. Replace Fl::check()/wait() with IApplication

85 scattered calls. Main loop becomes `IApplication::processEvents()` / `IApplication::waitForEvents(timeout)`.

### 1D. Replace GLUT timing with std::chrono

Remove GLUT dependency (`glutInit`, `glutGet(GLUT_ELAPSED_TIME)`) → `std::chrono::steady_clock`.

**Verification:** App runs on FLTK. No FLTK headers in non-`_fltk` files. Business logic fully decoupled.

---

## Phase 2: Qt Implementation (6-8 weeks)

### 2A. CMake infrastructure

```cmake
option(USE_QT "Build with Qt instead of FLTK" OFF)
find_package(Qt6 COMPONENTS Widgets OpenGLWidgets QUIET)
```

Compile `*_qt` files when ON, `*_fltk` files when OFF.

### 2B. QOpenGLWidget GL context (`src/qt/GlViewport_qt.h/.cpp`)

Implements IGLViewport. Key differences from Fl_Gl_Window:
- `update()` instead of `redraw()`
- `makeCurrent()` instead of `make_current()`
- `devicePixelRatio()` instead of `pixels_per_unit()`
- QOpenGLWidget renders to internal FBO — test with JefeCheck's own FBO pipeline

### 2C. Qt implementations of existing GUI abstractions

- `src/qt/gfcplategui_qt.h/.cpp`
- `src/qt/gfcplaybackgui_qt.h/.cpp`
- `src/qt/gfcsequencegui_qt.h/.cpp`
- `src/qt/gfcnetworkclientgui_qt.h/.cpp`
- `src/qt/gfcnetworkservergui_qt.h/.cpp`
- `src/qt/gfcplatemanagergui_qt.h/.cpp`

### 2D. Dark VFX theme (`src/qt/theme/jefecheck_dark.qss`)

Match current `GFC_BG_COLOR` (32,32,32) / `GFC_WIDGET_COLOR` (42,42,42) values. Orange accent (#d4771e) for active states. Flat buttons, dark inputs, Nuke-style aesthetics.

### 2E. Qt dialog windows (priority order)

1. MainWindow_qt (5,720 lines of FLUID → Qt Designer or programmatic)
2. LoadWindow_qt
3. PreferencesWindow_qt
4. FX/LUT windows
5. Remote/Playlist/Render windows
6. Simple dialogs (About, MinSpecs, etc.)

### 2F. Main event loop

Replace `while(!quitNow) { Fl::check(); managers.update(); }` with:
- `QApplication::exec()` as the main loop
- `QTimer(0ms)` for playback manager updates
- Qt signal-based scheduling for non-time-critical updates

**Verification:** `cmake -DUSE_QT=ON` builds and runs. Basic playback works in Qt.

---

## Phase 3: Feature Parity Validation (4-6 weeks)

Migrate and validate each window with full feature testing:
1. Main window + viewport (keyboard shortcuts, multi-plate layouts, drag interactions)
2. Load window (progress, abort, EXR channels)
3. FX/LUT windows
4. All remaining dialogs
5. Custom widget Qt equivalents (timeline slider is most complex)

**Verification:** Full feature parity between FLTK and Qt builds.

---

## Phase 4: Cleanup (2-3 weeks)

- Remove all `*_fltk` files, `.fl` FLUID files, Fl_*_gfc widgets
- Remove FLTK from CMakeLists.txt and CI
- Remove `USE_QT` option (Qt is the only backend)
- Simplify abstraction layer where single-backend makes it unnecessary
- Update CLAUDE.md and docs

**Verification:** Clean Qt-only build on all 3 platforms. No FLTK remnants.

---

## Critical Files

| File | Lines | Migration Role |
|------|-------|----------------|
| `src/GlViewport.h/.cpp` | 1,263 | Highest risk — Fl_Gl_Window → QOpenGLWidget |
| `src/gfcPlate.h/.cpp` | 3,246 | Remove 23 direct widget pointers |
| `src/UICallbacks.cpp` | 2,550 | Split into ~10 domain modules |
| `src/main.cpp` | 800+ | Replace globals + event loop |
| `src/mainWindow.cxx` | 5,720 | FLUID-generated → Qt Designer |
| `src/gfcplategui.h` | — | Template for all new abstractions |

## Estimated Total: 19-26 weeks (single developer)

Phase 0 and 1 can begin immediately without Qt installed. They improve code quality regardless of whether the Qt migration proceeds.
