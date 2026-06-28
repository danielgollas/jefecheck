# Qt FX Stack — make effects actually work + redesign the UI

**Branch:** `qt/fx-stack` (off `qt-experimental`). Squash-PR back to `qt-experimental`. Never touch `main`.
**Date:** 2026-06-27
**Build:** `cmake --build build_qt -j8` (warm Qt build; produces `build_qt/jefecheck.app`).

## Goal

Make the GLSL FX (shader effects) pipeline usable in the Qt build, and restructure
the FX UI to a single "effect controls" panel.

## What already exists (do not rebuild)

- **Rendering pipeline is implemented.** `gfcPlate::draw3DrectWithFX` (src/gfcPlate.cpp,
  ~L1187-1530) does a 3-pass FBO ping-pong over `fxStack`. `gfcPlate::draw()` routes to it
  when `fxStack.getNumOfActiveFXs() || forRender`. Qt build sets `sett.glsl=sett.fbo=true`
  in `initializeRenderingChain` (SequenceLoadBridge_qt.cpp).
- **FX default to active.** `gfcFX::active=true` (gfcfx.cpp:70). `gfcFXStack::addFX` pushes
  and a fresh FX is active, so `getNumOfActiveFXs()` should be >0 after an add.
- **Autoload.** Startup scans the FX path and `loadFX()`s every `.jfx` (MainWindow_qt.cpp
  `autoloadStep`, FX phase). All FX are always loaded; there is no per-FX autoload choice.
- **Bridge (SequenceLoadBridge_qt.{h,cpp}) already has:** `getAvailableFXNames()`,
  `getFXStackOnPlate(plate)`, `addFXToActivePlate(fxIndex)`, `removeFXFromPlate(plate,idx)`,
  `clearFXStackOnPlate(plate)`, `getFXStackMetaOnPlate(plate)` (full per-param metadata),
  `setFXParamValueOnPlate(plate,fxIdx,group,widget,value)`.
- **UI today:** `FXStackPanel_Qt` (in src/qt/FXLutPanel_qt.{h,cpp}) = available-list +
  stack-list + Add/Remove/Refresh (this is the "FX Loader"/browser). `FXParamPanel_Qt`
  (src/qt/FXParamPanel_qt.{h,cpp}) = editable float/bool/choice params; texture/cube/LUT
  read-only. `LUTPanel_Qt` (also in FXLutPanel_qt) = LUT browser (LEAVE UNTOUCHED).
  Docks created in MainWindow_qt.cpp (~L796-836): `fxDock_` (FX Stack), `lutDock_` (LUTs,
  tabbed with fxDock_), `fxParamsDock_` (FX Params). Menu items F2/F3/F4 (~L616-624).

## Design decisions (locked with the user)

1. **Add-FX UX = categorized "+" menu.** A "+ Add FX" button opens a hierarchical QMenu
   built from each FX's `menuName` ("Category/Subcategory/Name"). Picking adds it to the
   active plate's stack.
2. **Reorder = drag-to-reorder.** Drag an FX row's handle to change stack order (order
   changes the render result).
3. **Texture/cube/LUT FX params stay read-only this PR** (deferred, same as today). Wire
   only float/bool/choice param editing + add/remove/reorder/active-toggle + rendering.
4. **Drop the separate FX browser window.** Since all FX autoload, there is no available/
   loaded-status browser. The combined panel IS the FX UI. Remove `FXStackPanel_Qt` and its
   dock; remove autoload/loaded-status UI. `LUTPanel_Qt` + `lutDock_` stay.

## Target UI (one combined "FX" panel)

Repurpose `FXParamPanel_Qt` (or a renamed successor) into the effect-controls panel for the
active plate:

```
┌ FX — Plate 1 ───────────────┐
│ [ + Add FX ▾ ]              │   ← QToolButton, hierarchical QMenu from menuName
│                             │
│ ⠿ 1. BCS        [x] [⌫]    │   ← drag handle, index+name, active checkbox, remove
│     Brightness  [ 1.00 ]    │   ← params (float spinbox / bool / choice), inline
│     Contrast    [ 1.00 ]    │
│ ⠿ 2. Gaussian   [x] [⌫]    │
│     Radius      [ 4.0  ]    │
└─────────────────────────────┘
```

## Constraints (developer_notes.md)

- **§1 TU separation:** ONLY `SequenceLoadBridge_qt.cpp` includes rendering-chain/manager
  headers (they pull glad). All other `src/qt/*.cpp` go through `jefe::qt::*` accessors.
  Add bridge wrappers; never `#include "gfcfx*.h"`/manager headers in a Qt UI TU.
- **§3 propagatePlateChanges:** plate-card GUI writes need propagation; FX edits already go
  through `setFXParamValueOnPlate` → `plateManager.setChanged()`, which is the FX equivalent.
- **GL context:** any bridge op issuing GL calls outside `paintGL` must be wrapped in
  `viewport->makeCurrent()/doneCurrent()` (see §18/§21). Adding an FX to a stack is pure CPU
  (copies a gfcFX struct); rendering happens in paintGL — so add/remove/reorder need no
  makeCurrent. Only confirm during diagnosis.

## Phases

### Phase 1 — Diagnose & prove rendering (FOUNDATIONAL, do first)
Build, then prove whether adding an FX to a plate actually changes rendered output.
Extend the headless harness: add `--fx-test <image>` (mirror `--render-test`/
`runHeadlessRenderTest` in main_qt.cpp + MainWindow_qt.{h,cpp}) that: loads the image into a
plate, renders once (baseline), adds a visually-obvious FX (prefer an Invert/Negative/Mono
`.jfx` in src/FX — pick one whose output is unmistakably different) to the plate's stack,
renders again, and reports whether the pixels differ. If they DON'T differ, that's the
wiring bug — find and fix it (systematic-debugging: trace addFXToActivePlate → which plate's
fxStack → draw() routing → draw3DrectWithFX → bind() → shader compile state
`loadedAndCompiled`/`ShaderProgram`). Deliver: FX provably apply headlessly + the `--fx-test`
flag committed.

### Phase 2 — Bridge additions
In SequenceLoadBridge_qt.{h,cpp} (+ gfcFXStack as needed), add:
- `void setFXActiveOnPlate(int plate, int fxIndex, bool active)` (+ `gfcFXStack::setActive`).
- `void moveFXOnPlate(int plate, int from, int to)` (+ `gfcFXStack::moveFX`) for drag reorder.
- An accessor exposing, for each available FX, its `menuName` AND the fxManager index to pass
  to `addFXToActivePlate` (e.g. `std::vector<std::pair<int,std::string>> getAvailableFXMenu()`).
  Verify index alignment with `addFXToActivePlate(int fxIndex)`.
Keep TU-safe. Each mutation calls `plateManager.setChanged()`.

### Phase 3 — Combined FX panel UI
- Rebuild `FXParamPanel_Qt` into the combined panel: "+ Add FX" QToolButton + hierarchical
  QMenu (from `getAvailableFXMenu`), per-FX rows (drag handle, index+name, active checkbox,
  remove button) + inline params. Drag-to-reorder calls `moveFXOnPlate`; active checkbox calls
  `setFXActiveOnPlate`; remove calls `removeFXFromPlate`; add calls `addFXToActivePlate`.
- Keep float/bool/choice editors; texture/cube/LUT read-only.
- Remove `FXStackPanel_Qt` (from FXLutPanel_qt.{h,cpp}), `fxDock_`, and the F2 "FX Stack"
  menu item. Rename `fxParamsDock_` title to "FX" (or keep "FX Params"); keep its menu entry.
  Preserve the existing refresh wiring (plateStateChanged → refresh).
- Keep `LUTPanel_Qt` + `lutDock_` untouched.
- Update CLAUDE.md (FX UI description) + add a developer_notes.md FX section.

### Phase 4 — Verify
Build clean; run `--fx-test` and `--render-test`; launch the app and add/reorder/toggle an FX
on a loaded image; `/code-review` the diff.

## Out of scope
Texture/cube/LUT FX param editing (later PR-38c), FX stack save/load UI (the .fxs
serialization already exists in gfcFXStack), per-FX reset button (nice-to-have).
```
