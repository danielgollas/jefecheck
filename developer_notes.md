# Developer Notes

Hard-won lessons from the FLTK→Qt port. Read before working in `src/qt/` or touching the rendering chain. These are the *why* behind patterns that look surprising in isolation.

## 1. TU separation: glad vs Qt OpenGL on macOS

> *"TU"* = **translation unit**. C++ jargon for one `.cpp` file plus every header it `#include`s after the preprocessor runs — the chunk the compiler sees in a single invocation. Each `.cpp` is its own TU; the linker stitches them together at the end. Two TUs in the same binary can include different headers (that's why this whole pattern works); two `#include`s in the *same* TU can't.

The rendering chain (`gfcPlateManager`, `gfcSequence`, `gfcPlate`, `gfcFXManager`, `gfcLUTManager`, `gfcTextRenderer`, etc.) pulls in `glad/glad.h` for OpenGL function loading. Qt's `QOpenGLWidget` pulls in the system OpenGL headers. The two **refuse to share a translation unit on macOS** — both define the same GL prototypes with conflicting linkage, and the build fails with "OpenGL header already included, remove this include, glad already provides it" or symbol redefinition errors.

**Rule:**
- `src/qt/SequenceLoadBridge_qt.cpp` is the **only Qt-side TU** allowed to include the rendering-chain managers. Its header (`.h`) does NOT pull glad.
- All other `src/qt/*.cpp` files (`TrackStrip_qt.cpp`, `LoadWindowDialog_qt.cpp`, `MainWindow_qt.cpp`, `GlViewport_qt.cpp`, `PlateCard_qt.cpp`, `PlateManager_qt.cpp`, panels, etc.) route every rendering-chain access through `jefe::qt::*` bridge accessors in `SequenceLoadBridge_qt.h`.
- Never `#include "gfcSequence.h"` (or any other manager header) in a Qt UI TU. Add a bridge accessor instead.
- The exception is `gfcTextRenderer.h` — it also pulls system OpenGL, so it can't be included in Qt UI TUs either. When you need a function from it, forward-declare locally (see `GlViewport_qt.cpp`'s `void gfc_gl_set_suppressed(bool);` forward decl that was used before the text-suppress code was reverted).

If you find yourself reaching for a manager directly from Qt code, the answer is almost always "add a small bridge wrapper."

## 2. `updatePlatesFromGUI` vs `updateAllFromGUI`

`gfcPlateManager::updateAllFromGUI()` does two things:
1. Mirrors per-plate GUI state into each plate (gamma, exposure, BCS, LUT, showPreview, transforms, etc.).
2. **Also resets `framingMode` and `activeQuad`** from the plate-manager GUI's mirrored fields.

In the Qt build, the layout (framingMode) is driven by Cmd+1/2/3/4 directly through `jefe::qt::setFramingMode`, bypassing the plate-manager-GUI mirror. Same for active-quad (plate-card clicks and `setActivePlate`). The plate-manager-GUI's `layoutChoice_` etc. stay stale.

So calling `updateAllFromGUI` from a Qt-side path (e.g. dialog close, drag end) **clobbers the layout the user just set with Cmd+2** and resets it to whatever the stale GUI mirror happens to say.

**Use `updatePlatesFromGUI()` instead** — added specifically for the Qt build's update points. It does per-plate state propagation only, leaves layout/active-quad alone. See `setAllPlatesShowPreview()` in the bridge for the canonical use site.

## 3. Plate-card slot wiring: GUI write + `propagatePlateChanges`

`PlateCard_Qt` slots write to `gfcPlateGUI_Qt` (the Qt GUI mirror), but **`gfcPlate`'s own fields are mirrored from the GUI by `updateValuesFromGUI`, which the Qt build doesn't otherwise call** unless something explicitly triggers it.

Pattern for every plate-card slot that should affect rendering:

```cpp
connect(gammaSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        this, [g](double v) {
    g->setGamma(v);                  // 1. write to GUI mirror
    jefe::qt::propagatePlateChanges();  // 2. propagate GUI → plate fields
});
```

Skipping step 2 = silent no-op: the spinbox moves but the plate doesn't change. This was the root cause of "most color-correction controls don't do anything" (PR #92).

LUT and Layer combos are exceptions — they route through `applyLUTToPlate` / `setLayerOnPlate` bridge functions that handle their own super-shader rebuild + propagation.

## 4. Plate state writers and the `showPreview` single-source rule

`gfcPlate::showPreview` used to be set by multiple writers (drop fast path, autoload, FLTK loadWindow callbacks). The Qt port consolidated this:

- The **only writer** is `GlViewport_Qt::setLoadWindowOpen(bool)`, which routes through `jefe::qt::setAllPlatesShowPreview(bool)` → loops `setPlateShowPreview` for plates 0..3 → `updatePlatesFromGUI()`.
- `LoadWindowDialog_Qt::showEvent` / `accept` / `reject` toggles the flag.
- Nothing else — not the fast-path drop, not the autoload — touches it.

If you find yourself wanting to write `showPreview` from a new path, **stop**. The whole point of the consolidation was eliminating the race between "preview frame arrived" and "loaded frame arrived." Find a different signaling mechanism.

## 5. macOS Qt drag-performance optimizations

Qt's `QOpenGLWidget` on macOS uses an internal FBO + composite, which adds a per-frame cost FLTK's native `Fl_Gl_Window` (NSOpenGLView-backed) doesn't pay. Combined with AppKit's accessibility cascade firing on every Qt widget value change, naïve viewport-drag handling gets noticeably laggy.

The pattern that gets you close to FLTK smoothness:

### a. `Qt::QueuedConnection` for slots that update widgets in response to viewport events

Direct connections run synchronously inside `mouseMoveEvent`. Queued connections post an event and let the handler return immediately. Qt's event loop coalesces and services them on the next iteration.

```cpp
connect(viewport_, &GlViewport_Qt::plateTransformChanged,
        plateManagerWidget_, &PlateManager_Qt::refreshPlateTransform,
        Qt::QueuedConnection);
```

### b. Targeted signals over broad signals

`plateStateChanged` walks all 4 plate cards + the FX param panel — too heavy for per-frame drag work. Use a narrower signal that only refreshes what actually changed:

- `plateTransformChanged(int plateIdx)` → just the dragged plate's zoom/panX/panY/rot spinboxes.
- `plateColorChanged(int plateIdx)` → just that plate's gamma/exposure/BCS spinboxes.

Each plate card has a matching cheap slot (`refreshTransformOnly`, `refreshColorOnly`).

### c. Cache-gated widget writes

Even with a targeted slot, calling `setValue` with an unchanged value still triggers AppKit accessibility events. Cache the last-shown value per field; skip the `setValue` entirely when it hasn't moved. See `PlateCard_qt.cpp::CachedState`.

### d. Throttle continuous emissions to ~60Hz

macOS mouse events arrive at the device polling rate (often 100Hz+). Use `QElapsedTimer` in `mouseMoveEvent` to gate emissions:

```cpp
constexpr qint64 kEmitIntervalMs = 16;
if (!dragEmitTimer_.isValid() || dragEmitTimer_.elapsed() >= kEmitIntervalMs) {
    emit plateTransformChanged(dragPlate_);
    dragEmitTimer_.restart();
}
```

Always emit the full `plateStateChanged` once on `mouseReleaseEvent` so the rest of the UI (FX panel, inactive plate cards) gets a final sync.

### e. Surface-format tweaks

In `main_qt.cpp`'s `QSurfaceFormat` setup:

```cpp
fmt.setSwapInterval(1);  // explicit vsync — Qt's macOS default was inconsistent
```

In `GlViewport_Qt` constructor:

```cpp
setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);  // we full-redraw; skip the preserve-FBO copy
```

### f. Skip the playback tick when nothing's playing

The 60Hz playback timer's `makeCurrent` + `tickPlayback` + `doneCurrent` cycle is expensive on macOS. Gate it on `jefe::qt::needsPlaybackTick()` (true when `playbackManager.isPlaying()` OR any track has pending raw frames). Without this the app idles at ~12% CPU; with it, ~0.5%.

## 6. AppKit accessibility cascade — the invisible cost

On macOS, every Qt widget value/text/check change triggers `NSAccessibilityValueChangedNotification`. AppKit walks the responder chain and updates VoiceOver state regardless of whether VoiceOver is enabled. This is why even "cheap" `setValue` calls on hidden or unfocused widgets cost real time during drag.

The mitigations from Section 5 (especially caching, targeted slots, and queued connections) exist primarily to **reduce the number of widget writes**, not to make each write faster.

## 7. Pinch gesture wiring

`QPinchGesture` requires `grabGesture(Qt::PinchGesture)` in the widget constructor and a `event(QEvent*)` override that dispatches `QEvent::Gesture`. macOS trackpads, Magic Trackpads, and Windows precision touchpads all deliver pinch through this path.

Key points:
- `QPinchGesture::centerPoint()` returns **global screen coordinates**. Use `mapFromGlobal` to convert to widget-local before hit-testing.
- `scaleFactor()` is **incremental** per gesture event (1.0 = no change). Convert to an additive delta via `scaleFactor() - 1.0` when feeding into a delta-style accumulator.
- Capture the target plate on `GestureStarted` and reuse it through `GestureUpdated`; the center point drifts during pinch, but the user expects "the plate I started pinching."
- Clear the captured plate on `GestureFinished` / `GestureCanceled` so an interrupted pinch doesn't bleed into the next one.

See `GlViewport_qt.cpp::handlePinchGesture` for the canonical implementation.

## 8. CBArgs enum mismatch — `LOOPMODEONCE_ID` is not 0

`enum CBArgs` in `src/UIConstants.h` is a long flat enum that starts at `TARGETFPS_ID = 1` and continues. `LOOPMODEONCE_ID`, `LOOPMODELOOP_ID`, and `LOOPMODEBOUNCE_ID` end up at values 22 / 23 / 24, not 0 / 1 / 2.

When a Qt combo passes its index directly to `playbackManager.setPlaybackMode`, the switch inside `update()` matches against 22/23/24 — combo indices 0/1/2 don't match any case, the switch falls through, and playback freezes.

The bridge layer must translate at the boundary:

```cpp
// SequenceLoadBridge_qt.cpp
void setLoopMode(int comboIdx) {
    playbackManager.setPlaybackMode(comboIdxToLoopModeId(comboIdx));
}
int getLoopMode() {
    return loopModeIdToComboIdx(playbackManager.getPlaybackMode());
}
```

This pattern (translate at the bridge boundary, not at the call site) applies anywhere a Qt combo index meets a `CBArgs`-derived enum.

## 9. `findSequence` endNum initialization (gfcSequence.cpp)

A pre-existing FLTK-era bug surfaced in PR #91. When the user picks a frame in the middle of a sequence (e.g. `singlepart.0008.exr` with `0001..0008` on disk):

1. The down-walk from the picked frame finds 0001 → sets `startNum = 1`.
2. `endNum = startNum;` ← **bug**: now endNum is 1.
3. The up-walk starts at picked+1 (= 9), doesn't exist, exits.
4. `endNum` stays at 1 instead of 8. Range reported as (1, 1).

Fix: `endNum = atoi(num);` (the originally-picked frame number) so the up-walk has a sane starting point. The picked frame is on disk by definition — it's what triggered detection.

## 10. Plate-card debug pattern

When a plate-card control "doesn't do anything," the diagnostic is always: did we propagate to the plate, or did we just write to the GUI mirror?

1. Find the `connect` in `PlateCard_qt.cpp`. Check the slot body.
2. If it ends with bare `g->setX(v);` → bug. Add `jefe::qt::propagatePlateChanges();`.
3. If it routes through a bridge function (`applyLUTToPlate`, `setLayerOnPlate`, etc.) → the bridge function itself needs to handle the propagation (most do).

## See also

- `CLAUDE.md` — project conventions, build setup, platform-specific gotchas.
- `docs/superpowers/specs/` — design specs for major features (Qt load window, etc.).
- `docs/superpowers/plans/` — implementation plans matching those specs.
