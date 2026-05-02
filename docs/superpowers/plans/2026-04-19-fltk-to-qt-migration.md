# FLTK-to-Qt Migration Plan — Status & Remaining Work

> **Re-evaluated 2026-05-01** after PR-35 (bit depth + scale) and PR-36 (System Specs dialog) merged into `qt-migration` (HEAD: `56463ac`). Original 6-phase plan superseded by this version; phases retained as section headers for traceability.

## Context

JefeCheck has been running on FLTK since 2006. Phases 0, most of 1, and the bulk of Phase 2 (Qt implementation) are now in place: the Qt build boots, renders multi-plate via QOpenGLWidget, drives playback through the manager tick, has the dark VFX theme, and ships a working menu bar / docks / preferences / status bar. The remaining work is a focused list of dialog ports, one decoupling refactor, parity tests, and Phase 4 deletion of FLTK assets — in that order.

**Goal (unchanged):** Replace FLTK with Qt6 while maintaining a working app throughout the transition. Dark VFX-industry theme (matching Nuke/DaVinci Resolve aesthetic).

## Original Scope Snapshot (April 2026)

- 76 of 404 source files used FLTK (19%)
- 354 callback instances, 85 `Fl::check()/wait()` calls, 150+ `Fl::event_*` calls
- 14 active FLUID-generated windows (~7,700+ lines of `.cxx`)
- 6 custom `Fl_*_gfc` widgets

Most of this surface is now behind the abstractions or already deleted from the Qt build via CMake exclusions.

## Existing Abstraction Pattern

```
src/gfcplategui.h           ← abstract interface
src/gfcplategui.cpp         ← shared logic
src/gfcplategui_fltk.h      ← FLTK implementation
src/gfcplategui_fltk.cpp
src/qt/gfcplategui_qt.h     ← Qt implementation (now exists for all six)
src/qt/gfcplategui_qt.cpp
```

Same pattern lives for `gfcplaybackgui`, `gfcsequencegui`, `gfcnetworkclientgui`, `gfcnetworkservergui`, `gfcplatemanagergui`.

---

## Current Status

### Phase 0 — UI abstraction layer ✅ DONE
- All six interfaces in `src/ui/`: `IGLViewport.h`, `IEventSystem.h`, `IMainWindow.h`, `IApplication.h`, `IFileChooser.h`, `IMessageDialog.h`.
- `gfcPlate`'s 23 direct widget pointers extracted (PR-2). `gfcPlateManagerGUI` triplet exists (PR-6). `GlViewport` dual-inherits `IGLViewport` (PR-5).

### Phase 1 — decouple core logic ⚠️ MOSTLY DONE
- **1A** UICallbacks split → `src/callbacks/{Playback,Load,LUT,Network,Preferences,Menu,Render}Callbacks.cpp`. Original plan listed 9 modules; actual implementation consolidated to 7 (Plate/FX merged into Menu and Playback).
- **1B AppContext singleton — NOT DONE.** ~25 `extern` globals still live in `src/main.cpp`/`src/main_qt.cpp`. **Sequenced as PR-37 (next).**
- **1C** Qt event loop has zero `Fl::check()/wait()` calls; runs through `IApplication_Qt` + `QApplication::exec()` (`src/qt/iapplication_qt.cpp`).
- **1D** GLUT replaced; no `glutGet`/`glutInit` in active source.

### Phase 2 — Qt implementation ⚠️ 75% DONE
- **2A–2D** ✅ done: `USE_QT` CMake option (default `OFF`), QOpenGLWidget viewport, all six `gfc*GUI_qt`, dark theme at `src/qt/theme/jefecheck_dark.qss`.
- **2E** Window ports — see remaining work below.
- **2F** ✅ done: `QApplication::exec()` + `QTimer` playback tick.

### Phase 3 — parity validation ⚠️ 70% TESTED
Tests exist for: smoke launch, transport, plate ops/reset, load (incl. depth combo persistence), track, FX stack add/remove, LUT panel, preferences, layouts, visual diff, MinSpecs menu wiring. Untested: per-FX param editing, render, remote, playlist (because their Qt counterparts don't exist yet — tests land with each port).

### Phase 4 — cleanup ⏳ NOT STARTED
Default backend still FLTK. Backup `.fl` files (`mainWindowBAK01`, `mainWindowNonPlastic`, `mainWindowTest`, `loadWindowOld`, `demoWatermarkDummy`) and dead-shell windows (`gammaWindow`, `drawingToolsWindow`, `moreOptionWindow`, `shorcutsWindow`, `splashWindow`, `exrWindow`) still on disk and in CMake's exclusion list.

---

## Remaining Work

User-confirmed sequencing (2026-05-01):
1. **PR-37 — AppContext singleton (Phase 1B).**
2. **PR-38 — FX param editor (Phase 2E).** Top user-visible gap.
3. **PR-39 — Render dialog.**
4. **PR-40 — Playlist panel.**
5. **PR-41 — Remote sessions.**
6. **PR-42 — Flip USE_QT default to ON.**
7. **PR-43+ — Phase 4 cleanup.**

### PR-37: `AppContext` singleton (Phase 1B)

**Why:** Decoupling refactor done now while both FLTK and Qt builds compile, so the abstraction settles before Phase 4 deletes the FLTK side.

**Approach:**
- Create `src/AppContext.h/.cpp` with `AppContext::instance()` returning references to: `IMainWindow&`, `IGLViewport&`, `IApplication&`, `gfcTrackManager&`, `gfcPlateManager&`, `gfcPlaybackManager&`, `gfcNetworkManager&`, `gfcFXManager&`, `gfcLUTManager&`, `gfcSessionManager&`, `gfcPlaylistManager&`, `gfcSettings& sett()`.
- Mechanical sweep: replace `extern X x;` declarations and uses in non-`_fltk` / non-`_qt` files with `AppContext::instance().X()`. Each backend keeps its own `extern` definitions in `main.cpp` / `main_qt.cpp` and registers them with `AppContext` at startup.
- Both build configs (`USE_QT=OFF` and `USE_QT=ON`) must compile and pass the regression sweep at the end.

**Verification:**
```bash
cmake -B build && cmake --build build && \
cmake -B build_qt -DUSE_QT=ON && cmake --build build_qt && \
cd tests/ui && JEFECHECK_BIN=../../build_qt/JefeCheck.app \
  .venv/bin/pytest test_smoke.py test_layouts.py test_load.py \
  test_plate_reset.py test_fx.py --timeout=120
```

### PR-38: FX param editor (`FXParamPanel_qt`)

**Status (PR-38b, in flight on `qt/38b-fx-param-edit`):** Editor widgets landed. `gfcFXStack::setWidgetValue` exposes a direct widget mutation path mirroring `processNetFXAttribInfo`'s logic without the network round-trip. New bridge accessor `setFXParamValueOnPlate(plate, fx, group, name, value)` calls into it and flags `plateManager.setChanged()`. `FXParamPanel_Qt::refresh` now builds per-row editors: `FX_GUI_FLOAT` → `QDoubleSpinBox`, `FX_GUI_BOOL` → `QCheckBox`, `FX_GUI_CHOICE` → `QComboBox`. Texture/cube/LUT slots remain read-only labels (need GL handle plumbing — PR-38c). A `refreshing_` reentrancy guard suppresses spurious bridge writes during panel rebuild. Five Mac2 tests now: prior three plus `test_fx_params_bcs_produces_editable_count` and `test_fx_params_brightness_spinbox_resolvable`. PR-38a (read-only) merged as #69 (commit `9b130b0`).

**Status (PR-38a, merged as #69):** Read-only viewer landed. `FXParamType`/`FXParamMeta`/`FXMeta`/`getFXStackMetaOnPlate` added to `SequenceLoadBridge_qt.{h,cpp}`. `FXParamPanel_Qt` lives in the **left dock area** (right area was over-subscribed and the AX bridge was eliding the status text under tabified-behind / split-collapsed layouts). Refresh wired to `GlViewport_Qt::plateStateChanged` and a new `FXStackPanel_Qt::stackChanged` signal.

**Why:** `src/fxcontrolwindow.cpp` (1309 lines) is the per-FX parameter UI — sliders + value inputs once an FX is on a plate. The Qt build today has no way to tune FX parameters once added; most user-visible gap.

**Approach:**
- `src/qt/FXParamPanel_qt.h/.cpp` as a right-side `QDockWidget` (`dock.fxparams`) sibling of the existing FX/LUT dock. No floating-window equivalent needed.
- Read the FX parameter list from `gfcFXStack::getFX(i)` — params are `gfcFXParam` with `name`, `min`, `max`, `value`, `type` (slider, choice, checkbox). Map to QSlider + QDoubleSpinBox row, QComboBox, or QCheckBox.
- Refresh the panel on the existing `plateStateChanged` signal and after `addFXToActivePlate` / `removeFXFromPlate` in `SequenceLoadBridge_qt`.
- Param edits write back via a new `jefe::qt::setFXParam(plateIdx, fxIdx, paramIdx, value)` bridge mirroring the existing FX bridge pattern in `qt_globals.cpp`.
- objectName scheme: `fxparams.fx<i>.param.<name>.{slider,spin,combo,check}`.

**Files:** `src/qt/FXParamPanel_qt.h/.cpp` (new), `src/qt/MainWindow_qt.cpp` (add dock + View menu toggle), `src/qt/qt_globals.cpp` + `SequenceLoadBridge_qt.h` (param accessor), `tests/ui/jefecheck/locators.py`, `tests/ui/test_fx_params.py` (new).

### PR-39: Render dialog (`RenderDialog_qt`)

**Status (PR-39a, in flight on `qt/39-render-dialog`):** Minimum-viable modal landed — quadrant / format / range / scale / output path / prefix / postfix / padding plus auto-range, output-path browse, live first/last filename preview, Render and Done buttons. New bridge accessors in `SequenceLoadBridge_qt`: `RenderParams` struct (Qt-friendly mirror of `gfcRenderParams`), `previewRenderFilename`, `triggerSyncRender`, plus `abortRender` / `isRendering` stubs (today only meaningful from a worker thread, wired for symmetry). Render runs synchronously on the GUI thread; the dialog freezes until done. Format-specific quality (jpeg/png/tiff/exr), video codec, and movie-creation pipeline (mencoder + clearFXStack + reload) come in PR-39b along with a QThread driver for cancel.

**Why:** `src/renderwindow.cpp` (488 lines) drives offline export to JPEG/PNG/TIFF/movie with format/scale/range/quality knobs. Wired into `plateManager.startRender(gfcRenderParams)` (`src/callbacks/RenderCallbacks.cpp`). FLTK build has it; Qt build does not.

**Approach:**
- Modal `QDialog` triggered from File → Render… (slot already stubbed at `MainWindow_qt.cpp:396` with `/* TODO */`).
- Fields: quadrant `QComboBox`, format `QComboBox` with format-specific subgroups (JPEG quality + progressive + optimize; PNG compression; TIFF compression), output `QLineEdit` + Browse, prefix/postfix `QLineEdit`, padding `QSpinBox`, frame range start/end, scale `QDoubleSpinBox`, video codec/quality, "Create movie" `QCheckBox`. Live preview label using `CreateRenderFilename(params)`.
- Render/Cancel buttons drive `plateManager.startRender(params)` / `plateManager.abortRender()`. Disable form during render; show progress fed from `plateManager.isRendering()` polled on the existing playback `QTimer`.
- objectName scheme: `dialog.render.<field>`.

**Files:** `src/qt/RenderDialog_qt.h/.cpp` (new), `src/qt/MainWindow_qt.cpp` (replace TODO stub), `tests/ui/test_render.py` (new — dialog opens, format combo populated, render button gated on path/range; full export run gated behind a slow marker).

### PR-40: Playlist panel (`PlaylistPanel_qt`)

**Why:** `src/playlistwindow.cpp` (`plw.theWindow->show()` invoked at startup `main.cpp:721` and from `LoadCallbacks.cpp:62`, `PlaybackCallbacks.cpp:149`, `MenuCallbacks.cpp:317`). Sequential playback queue.

**Approach:**
- `QDockWidget` (matches existing dock paradigm) hosting `QListWidget` of items + add/remove/up/down/clear buttons + double-click to load. Mirror `gfcPlaylistManager::*` API (already FLTK-free).
- Read `gfcPlaylistManager` directly via `AppContext` (post-PR-37).
- File → Playlist menu toggle plus the existing `View` menu's auto-collected dock toggles.
- objectName: `dock.playlist`, `playlist.list`, `playlist.{add,remove,up,down,clear}.button`.

**Files:** `src/qt/PlaylistPanel_qt.h/.cpp`, `src/qt/MainWindow_qt.cpp`, `tests/ui/test_playlist.py`.

### PR-41: Remote sessions (`RemotePanel_qt`)

**Why:** `src/remoteWindow.fl` + `src/callbacks/NetworkCallbacks.cpp` + `gfcNetworkManager` host the RakNet collaborative review feature. Real and shipped, just not yet on the Qt side.

**Approach:**
- `QDockWidget` "Remote" with two sub-tabs (Server / Client) each showing connection state, participant `QListWidget`, chat `QTextEdit` + input `QLineEdit`, Connect/Disconnect/Save-chat. The existing `gfcnetworkclientgui_qt` and `gfcnetworkservergui_qt` already implement the GUI abstractions; the dock is the host UI.
- Wire menu IDs (`MENUREMOTEMANAGER_ID`, `MENUREMOTESAVECHAT_ID`) as Qt actions on a new "Remote" top-level menu.
- Largest unknown: how `gfcnetwork*gui_qt` are currently instantiated/owned. PR will likely move ownership into `MainWindow_Qt`.
- objectName scheme: `dock.remote`, `remote.{server,client}.tab`, `remote.chat.input`, etc.

**Files:** `src/qt/RemotePanel_qt.h/.cpp`, `src/qt/MainWindow_qt.cpp`, possibly small edits to `src/qt/gfcnetwork{client,server}gui_qt.cpp` for parent-widget plumbing, `tests/ui/test_remote.py` (smoke only — full network test would need a paired second instance).

### PR-42: Flip `USE_QT` default to `ON`

**Why:** Phase 2E feature-complete after PR-41. Switching the default makes Qt the canonical build, reorients CI, unblocks Phase 4 deletion.

**Changes:** one line in `CMakeLists.txt` (`option(USE_QT ... ON)`), README/CLAUDE.md updates, CI matrix flip in `.github/workflows/build.yml` to build Qt by default and FLTK as secondary.

### PR-43+: Phase 4 cleanup

Run as one or two PRs once PR-42 is merged and the Qt build has soaked.

**Delete:**
- All `*_fltk.cpp/.h` (network clients/servers, plate GUI, plate manager GUI, playback GUI, sequence GUI, GUI base, application, file chooser, message dialog, viewport).
- All `.fl` files and their FLUID-generated `.cxx`/`.h` pairs: backups (`mainWindowBAK01`, `mainWindowNonPlastic`, `mainWindowTest`, `loadWindowOld`, `demoWatermarkDummy`); dead shells (`gammaWindow`, `drawingToolsWindow`, `moreOptionWindow`, `shorcutsWindow`, `splashWindow`, `exrWindow`); ported originals (`mainWindow`, `loadWindow`, `preferencesWindow`, `aboutWindow`, `minSpecsWindow`, `fxWindow`, `lutWindow`, `renderWindow`, `remoteWindow`, `playlistwindow`).
- Custom widgets `Fl_Choice_gfc`, `Fl_Button_gfc`, `Fl_Button_RGBA_gfc`, `Fl_Spinner_gfc`, `Fl_Slider_Timeline_gfc`, `Fl_Input_Choice_gfc`, `Fl_DragBar`.
- `src/UICallbacks.cpp` (already excluded from Qt build), `src/main.cpp` (FLTK entrypoint), `src/GlViewport.cpp`, `src/fxcontrolwindow.cpp`, `src/playlistwindow.cpp`, `src/renderwindow.cpp`, `src/trackwidget.cpp`, `src/checkmateResoucesWindow.cxx`.
- FLTK from `CMakeLists.txt` (`find_package(FLTK)`, `target_link_libraries`), `USE_QT` option, all `list(FILTER ... EXCLUDE)` lines for FLTK files.
- `gfctrackmanagergui.cpp` if no longer reachable (verify).

**Simplify the abstraction layer where single-backend makes it pointless:** `IFileChooser` and `IMessageDialog` likely collapse to direct Qt calls; the `gfc*GUI` abstract bases stay because they've shaped the data flow but their pure-virtual nature can be loosened.

**Update:** `CLAUDE.md` (drop FLTK build instructions, GFL/FLU notes about FLTK interaction), `docs/manual.md`, `docs/quick-start.md`, GitHub Actions CI to drop FLTK platform matrix.

**Verification:** clean build on macOS / Linux / Windows; full Phase C + visual-diff test sweep; manual run-through of every menu and every dock to confirm nothing was a hidden FLTK dependency.

---

## Critical Files

| File | Role |
|---|---|
| `src/qt/MainWindow_qt.{h,cpp}` | Hosts every menu, dock, and dialog wire-up — touched by every Phase 2E PR. |
| `src/qt/qt_globals.cpp` + `SequenceLoadBridge_qt.{h,cpp}` | Bridge accessor pattern (`jefe::qt::*`). New ports add accessors here when they need to call into glad-using code. |
| `src/callbacks/RenderCallbacks.cpp` | Reference for PR-39 (render param plumbing). |
| `src/callbacks/NetworkCallbacks.cpp`, `src/gfcnetworkmanager.cpp` | Reference for PR-41. |
| `src/playlistwindow.cpp` + `gfcplaylistmanager.{h,cpp}` | Reference for PR-40. |
| `src/fxcontrolwindow.{h,cpp}` + `gfcfx.h` (`gfcFXParam` struct) | Reference for PR-38. |
| `tests/ui/conftest.py` + `tests/ui/jefecheck/{app,locators}.py` | Test harness; each new port adds locators here and a `test_<feature>.py`. |

## Estimated Remaining: 4-6 weeks (single developer)

Phase 1B + Phase 2E (PR-37 through PR-41) is the bulk: ~1 week per PR with tests. Phase 4 cleanup is mechanical (~1 week). Tighter if AppContext sweeps cleanly and remote-sessions reuses existing `gfcnetwork*gui_qt` plumbing.
