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

## 11. Plate-card layout: orientation-aware, fixed-size, content-swap

The plate card (`PlateCard_qt.cpp`) has **two internal layouts**, switched by `setVertical(bool)`:
- **wide-short** (horizontal dock / floating): two rows, labels-on-top.
- **narrow-tall** (vertical/side dock): four rows, ~200px wide.

**Swap mechanism — rebuild a content widget, don't recreate controls.** `rebuildContent()` builds a fresh `QWidget` (`nc`), re-flows the *existing* control widgets into it (adding a widget to `nc`'s layouts reparents it — members survive, signal connections persist), then swaps `nc` for the old `content_` and `deleteLater()`s the old one. Order matters: build `nc` fully (reparenting the controls out of the old content) **before** deleting the old content, or the deletion takes the controls with it. Caption `QLabel`s are created per-rebuild parented to `nc`, so they die with the old content — no orphans.

**The Plate Manager is orientation-driven, not width-driven** (`PlateManager_qt.cpp`). `MainWindow` wires `QDockWidget::dockLocationChanged` (left/right ⇒ vertical) and `topLevelChanged` (floating ⇒ horizontal) to `setOrientation`. Grid is 2×2 horizontal, 4×1 vertical. The panel **fixes its own cross-axis** to the packed cards (`setFixedSize` horizontal, `setFixedWidth` + free/scrolling height vertical).

Gotchas that cost real time here:
- **`QComboBox` sizes its width to its longest item.** Once LUTs autoloaded, the LUT combo's `sizeHint` ballooned and dragged the fixed-size card wide. Fix: give a "fill the leftover" combo `QSizePolicy::Ignored` so its sizeHint is ignored and it just takes the remaining row width (popup still shows full names).
- **Reading a widget's `sizeHint()` synchronously right after a content swap or during a dock move returns stale/near-zero values** → collapsed the vertical panel to a scrollbar, and produced a 0-size window on undock. Fix: measure **deferred** via `QTimer::singleShot(0, …)` after the tree settles, **and** clamp with safety floors so a transient bad read can never collapse the panel.
- **Don't `setMinimumSize` a card smaller than its packed contents.** A too-small floor lets the container squash fixed-width children into overlap. Let the layout's own `minimumSizeHint` govern; the scroll area then scrolls instead of clipping.
- **A docked panel can't be shorter than a taller dock-row neighbor.** QMainWindow leaves a shared row at the taller dock's size and just caps the shorter (fixed) one, padding it with empty space. `resizeDocks({dock}, {extent}, orientation)` pulls the shared row to the panel's size — the shrinkable neighbor (Timeline) follows. Removed the old `updatePlateMins` helper that forced a too-tall dock minimum.
- **Caption-as-click-target.** A `QLabel` doesn't consume mouse presses, so they propagate to `PlateCard_Qt::mousePressEvent` → `clicked()`. The "Track" caption carries the `track.label` objectName and is the plate-activation handle (it replaced the old plate-number label that `test_track.py` clicked).

## 12. AspectCropCombo: aspect and crop are orthogonal; the face is a button, not an editable combo

`AspectCropCombo_Qt` folds the old aspect combo + crop button into one control. Two things worth knowing:
- **Aspect and crop are orthogonal in the renderer** (`gfcPlate::calculatePolySizesCropEtc`): a ratio with crop OFF *reshapes* the quad (anamorphic), with crop ON draws letterbox bars. Don't collapse them into one value — both must stay reachable.
- **It subclasses `QToolButton`, not an editable `QComboBox`, on purpose.** An editable combo only opens its popup when the drop-down *arrow* is clicked — a click on the line-edit body just places a cursor, so a center click (users, and Appium) never opened it. You also can't make one surface both "click to open" and "click to type". So the face is a plain button (text + optional painted icon) that opens a custom `Qt::Popup` frame; custom-ratio entry lives *inside* the popup.
- The "original" default is **displayed** as "source" (and as the frame's native ratio when known) while its **canonical value stays "original"** — kept via the popup list item's `Qt::UserRole`, so selection still sends the file-original sentinel to `aspectFromString`.

## 13. Playback FPS pacing (Qt tick + gfcPlaybackManager)

Getting steady-FPS playback in the Qt build took several layered fixes. All of these matter together — fixing one without the others regresses pacing:

- **Initialize `targetFPS` / `timePerFrame` in the `gfcPlaybackManager` constructor.** They were uninitialized. The Qt fps spinbox calls `setValue(24)` *before* its `valueChanged` is connected, so the engine never received a starting FPS, and a garbage (≈0) `timePerFrame` made the frame-advance check `intraFrameCount >= timePerFrame` true every tick → playback ran at the tick rate, not the target. Seed them (24fps) in the constructor; the spinbox still overrides live.
- **The playback timestep must come from a high-resolution clock.** `gfcTimer` is integer-millisecond (`gettimeofday` truncated to whole ms). That's tolerable at a 16ms tick but becomes the dominant noise source at a fine tick — a 4ms tick fed integer-ms deltas of 3/4/5ms (±12.5% plus a systematic floor undercount). `updateTimestep()` now reads `std::chrono::steady_clock` directly for sub-ms deltas. Don't route playback pacing through `gfcTimer`.
- **Fine, decoupled tick for tight pacing.** The Qt playback `QTimer` runs at 4ms with `Qt::PreciseTimer` (FLTK got the same effect from a near-continuous idle loop). To afford that rate, the per-tick work is split (`SequenceLoadBridge_qt`): `tickPlaybackTiming()` is the no-GL half (advance + animations) run every tick; the expensive `makeCurrent`/`generateTextures`/`doneCurrent` trio is gated on `hasPendingTextureUploads()` so it only runs when a decoded frame is actually waiting (~24×/s, not 250×/s). The timeline/status read-back is throttled to every 4th tick (~60Hz) via a counter, not run at the full tick rate.
- **The measured-FPS readout is a UX problem, not a timing problem.** Even with perfect pacing, the true cadence dithers ±0.1fps (intervals 37–47ms) from OS-timer/event-loop jitter, so any honest 2-decimal readout wobbles. The on-screen "fps:" overlay (`gfcPlate.cpp`) now: (1) reads `currentFPS` computed from a **frame-aligned EMA** of the inter-advance interval (α=0.05; the old `fpsCount/fpsTimerCount` window divided exactly-N-frames by a wall-time including a variable partial-frame remainder → ±0.5 error); (2) refreshes the *displayed* value at a slow fixed cadence (~2.5Hz) so it doesn't flicker; (3) applies a **deadband** — within 2% of target it reports the target exactly, so it sits steady at 24.0 and only shows a real number when playback genuinely can't keep up; (4) prints at **one decimal**. Discontinuities (first frame, resume-after-pause) are filtered from the EMA. When debugging this, log the *inter-advance interval* separately from the readout — the first tells you if playback is actually even, the second only tells you what the number-formatting does.

## 14. Track timeline widget (`TimelineTracks_Qt`)

The per-track rows under the scrubber are one **painted `QWidget`**, not four child widgets — Appium can address the `timeline.tracks` container but not individual lanes/bars (behavior is verified manually).

- **All sequence/track access goes through the bridge** (`jefe::qt::getTrackTimelineState` / `getTrackOffset` / `setTrackOffset` / `startLoadingTrackAt` / `getTrackHoldMode` / `setTrackHoldMode`). The widget must not include `gfcSequence.h`/`gfctrackmanager.h` (TU separation, §1). `getTrackTimelineState` returns frame values already in global-timeline coords (offset folded in).
- **Shares the scrubber's pixel↔frame mapping** via file-local `frameFromXMapped`/`xFromFrameMapped` in `TimelinePanel_qt.cpp`, so the rows line up vertically under the scrubber and the playhead.
- **Left-drag is reassigned to offset**, not scrubbing — dragging a row accumulates pixels and steps the track's frame offset ±1 per frame-width (mirrors FLTK `tracksBarCB`). Current-time scrubbing stays on `TimelineScrubber_Qt`. Alt-click loads from the clicked frame; right-click opens a `QMenu` (hold-frame toggle + "Set offset…"); file drop loads into the dropped row's track via `loadFileIntoPlate(path, track, …)`.
- **`refresh()` runs before `refreshFromPlayback`'s fast-path early-return**, because a track's loaded fill grows during decode even while the transport values (frame/range/playing) are unchanged. `refresh()` is itself cache-gated (compares the 4 `TrackTimelineState`s) so calling it every tick is cheap.
- **Loaded fill is contiguous**, anchored at the sequence range start, spanning `getLoadedFrameCount()` frames (matches the FLTK progress-bar model). Precise alt-click-load fill positioning is a deferred refinement.
- **`gfcSequence` ranges are 0-based, the timeline is 1-based.** `getRangeStart()`/`getRangeEnd()` return 0..(n-1) for an n-frame clip, but `from`/`to` and the playback limits are 1-based (`getEndLimit`/`getStartLimit` add `+1` to the sequence range; `to == getNumFrames()`). `getTrackTimelineState` adds `+1` so the bar lines up with the 1-based scrubber/playhead — without it the last frame's slice never fills.
- **The timeline view IS the `[in, out]` range, mapped to the full widget width.** The scrubber and the track rows both map `[in, out]` (not the total `[from, to]`) across the full width, so in/out act as the zoom: tick size = width ÷ (out − in + 1). `TimelineTracks_Qt::refresh()` reads `getInPoint()/getOutPoint()` for its `from_/to_`. The in/out spinboxes are un-clamped (max ~10M) so the view can zoom out past track length; loading sets `in=1, out=trackLength` (both load paths) so a fresh clip fills the width. Do **not** make `setOutPoint` grow the total `to` — that squishes the content into a sliver (an earlier attempt did this and was reverted).
- **Default playback mode is Loop** (`gfcPlaybackManager` ctor uses `LOOPMODELOOP_ID`). The Qt loop combo reflects it via `loopModeIdToComboIdx`.

## 15. Timeline frame thumbnails (filmstrip)

The track rows can render a filmstrip of decoded frames (toggle: `sett.showThumbnails`, default on; transport-bar 🎞 button `transport.thumbnails.toggle` + track right-click item, via `jefe::qt::get/setThumbnailsEnabled`).

- **Captured at decode, not by GL readback.** The live upload path is `gfcFrame::generateTexture(bool captureThumbnail)` (`gfcframe.cpp`). The decoder's CPU buffer (`gfcGLFrameInfo::dataPointer`, **BGRA**, 8- or 16-bit per `info.dataType`) is alive only between `getFrameInfo()` and `releaseMemory()` — the thumbnail is downsampled (nearest, BGRA→RGBA8) into a `GfcThumbnail` member there, before the buffer is freed. The thumbnail rides on the `gfcFrame` into `frames[indexNumber]`; `gfcSequence::getThumbnail(idx)` returns it. The old `gfcSequence::generateTexture(RawFrame*)` is dead code — don't use it.
- **Capture is opt-in + capped.** `gfcSequence::generateTextures` passes `captureThumbnail = sett.showThumbnails && (indexNumber % stride == 0)`, where `stride` caps storage at ~2000 thumbnails/sequence so long clips stay bounded.
- **Bridge:** `getTrackThumbnail(track, frameIndex)` copies the bytes out as `ThumbPixels` (RGBA8) — the widget wraps them as `QImage::Format_RGBA8888` and caches `QPixmap`s keyed `(track<<24)|frameIndex` (dropped on toggle flip and on per-track reload).
- **Layout = natural width, tick-aligned, skip-overlap** (`paintFilmstrip`): a thumbnail is always `stripH × frameAspect` (its natural ratio — never squeezed/cropped). Each loaded frame is drawn centered on its tick x; any frame whose thumb would overlap the previous drawn one is skipped. Sparse → every frame, tick-aligned with gaps; dense → auto-samples and packs. There is no manual density threshold — "min width = natural ratio" IS the threshold. Frame aspect is probed from one decoded thumbnail (16:9 fallback until one exists).
- **Don't force the timeline taller than the Plate Manager.** They share the bottom dock row (`splitDockWidget(plateDock_, timelineDock_, Horizontal)`), so a tall min-height on the tracks widget makes the whole row (and thus the plate side) over-tall. Keep the floor modest (96); thumbnails scale to whatever lane height the row provides. An over-tall row once corrupted the saved dock state and hid the Plate Manager — `kSettingsState` was bumped to `_v2` to discard that.

## 16. LUT preview / inspector (`LUTPreview_qt`)

The LUT panel has an in-window preview: a QPainter 2D curve for 1D LUTs and an interactive `QOpenGLWidget` cube inspector for 3D LUTs. Hard-won bits:

- **All LUT sample data comes through the bridge** (`jefe::qt::getLutPreview` → `LutPreviewData` with a structured `cubeRGB` grid; `getLutSummaries` for the table). The widget never includes `trilerp.h`/glad (TU separation, §1). `getLUT(int)` returns a `CubeLUT` **by value** (copies the whole cube) — fine for one-shot snapshots on selection, not for per-frame use.
- **The cloud is its own `QOpenGLWidget`** using GL 2.1 immediate-mode + fixed-function (`glBegin`, `glFrustum`, `glRotatef`) — valid under the app's NoProfile 2.1 `QSurfaceFormat`, resolved against the system GL the widget's context uses, **not glad**. `QOpenGLFunctions` supplies the core subset (`glClear/glEnable/...`).
- **Set GL state every frame in `paintGL`, not in `initializeGL`.** The QPainter used for the R/G/B axis labels **disables `GL_DEPTH_TEST`** each frame, so enabling it once in `initializeGL` makes occlusion break from frame 2 (solid faces draw back-over-front). Wrap the raw GL in `beginNativePainting`/`endNativePainting`, and re-enable depth/clear/point-size at the top of every `paintGL`. Capture the GL matrices (`glGetFloatv`) for label projection *before* `endNativePainting`, draw the text *after*.
- **Dynamic frustum**: rebuild the projection each frame from the camera distance (`znear ≈ dist−2`, `zfar = dist+8`) so it never clips as you dolly — a fixed near/far in `resizeGL` clips.
- **`emit` is a Qt macro** — never name a local lambda `emit` (use `emitVert` etc.); it fails to compile with a cryptic "expected unqualified-id".
- **Adding a new Q_OBJECT header** (e.g. `LUTPreview_qt.h`) requires wiping `build_qt/jefecheck_autogen` before `cmake -B build_qt`, or AUTOMOC won't scan it → "undefined vtable" link errors.
- **Sortable table**: the LUT browser is a `QTreeWidget` (Name/Type/Size/Depth). Numeric columns sort via a `QTreeWidgetItem` subclass comparing an int stashed in a custom data role — plain display-text sort is lexical ("1024" < "16³").
- **Image-based (.tga) LUTs load via OIIO** now: `gfcReadImageRGB8` (in the OIIO loader) feeds `CubeLUT`'s `IMAGELUT2D` path; the shipped `UnitCube.tga` is a handy identity-cube sanity check. Only 64×64 image LUTs are wired (Nuke 448×448 reports unsupported).

## 17. Session save / restore

Wires the GUI-free `gfcSessionManager` into Qt via `jefe::qt::*`. Key points:

- **`loadSession` / `loadRecoverySession` must run with the viewport GL context current** — they decode preview frames (texture uploads). `MainWindow` wraps them in `makeCurrent`/`doneCurrent` (same rule as the LUT autoload).
- **`loadSession` restores params + a preview but does NOT kick the full decode.** After a successful load, call `jefe::qt::startLoadingAllTracks()` (= the Load window's "Load All") or the footage stays unloaded. This was the "had to open the load window then press Load All" bug.
- **Async refresh.** Sequences re-decode on the loader thread (frames arrive over the next ticks). `refreshAfterSessionLoad()` refreshes the not-per-tick widgets (plate cards, LUT panel, timeline, viewport) **immediately and again at 250/750ms**. (Status labels + timeline already refresh every tick.)
- **`writeRecoverySession` is unconditional** (not gated on `enableCrashRecoverySession`) — the `On launch` preference governs whether the recovery file is *consumed*, not whether it's written.
- **Clean-exit detection:** `QSettings("Session/cleanExit")` set `true` in `closeEvent`, cleared to `false` at startup — distinguishes a crash from a clean close for the recovery prompt.
- **Recent sessions** in `QSettings("Session/recent")` (cap 5; seeded into `sett` at startup, written back on close). **Recent playlists** (JEF-18) mirror this exactly in `QSettings("Playlist/recent")` (`sett.recentPlaylists`, cap `maxRecentPlaylists=5`): the push lives in the bridge (`loadPlaylistFile`/`savePlaylistFile` → `noteRecentPlaylist`, since playlists have no shared-core load hook the way sessions do in `gfcsessionmanager.cpp`), so the Playlist-panel Load button, drag-drop, and the new **File → Open Playlist… / Recent Playlists** all populate the list. The submenu (`rebuildRecentPlaylistsMenu`) rebuilds on the File menu's `aboutToShow` alongside the sessions one. **CC favorites** persist app-globally in `favorites.jcs` *and* embed in each `.jcs` (a `ccFavorites` node). View-menu only — no `Ctrl+1–5` shortcuts (clash with layout).
- **`updateAllFromGUI` caveat (§2):** `loadSession` ends with `updateAllFromGUI`, which can reset framing/active-quad; if a restored layout is wrong, re-apply framing post-load.

## 18. Image saving / render output (`gfcImageSaver` → OIIO)

`getImageSaverInstance` was a stub that returned NULL and then dereferenced it (a guaranteed crash the moment the Render button ran — so that path had never actually executed). It now returns a concrete OIIO-backed `gfcImageSaverOIIO` (file-local in `gfcimagesaver.cpp`). Key points:

- **The render path is `gfcPlate::draw3DrectWithFX` with `forRender=true`** (driven by `gfcPlateManager::renderPlate` → `triggerSyncRender`). It allocates the saver, reads the FBO texture into the saver's buffer via `glGetTexImage(getGLFormat(), getGLPixelFormat())`, then calls `save()`. The save block sits **inside `if (theFrame.loaded)`** — an unloaded frame renders nothing.
- **Readback format:** always RGBA. LDR formats read `GL_UNSIGNED_BYTE`; EXR reads `GL_FLOAT` (OIIO converts float→half on write when `exrFormat==GFC_HALF`). The saver flips vertically (GL bottom-up → OIIO top-down) and strips alpha to RGB for JPEG/BMP; PNG/TIFF/TGA/EXR keep RGBA.
- **A current GL context is mandatory.** `renderPlate` issues GL calls outside `paintGL`, so callers must wrap it in `viewport->makeCurrent()/doneCurrent()` (same rule as session load, §17). `RenderDialog_Qt::onRenderClicked` does this via `MainWindow_Qt::viewport()`.
- **`gfcRenderParams`'s ctor now initialises its fields** (quality/compression/format) — they were uninitialised garbage, and `triggerSyncRender` doesn't set the quality knobs.
- **Output path needs a trailing separator:** `CreateRenderFilename` concatenates `path+prefix` directly; the bridge's `toCoreRenderParams` appends `/` because `QFileDialog::getExistingDirectory` omits it.
- **Stills don't render.** `loadFileIntoPlate` (Quick Load / `--open-file`) loads only a *preview* frame; the sequence frame-list the renderer reads (`getFrame(currentFrame)`, `getNumFrames()`) is populated only when `startLoadingSequence` fires — i.e. for multi-frame sequences (`getNumPreviewFrames() > 1`). Render/test with a real sequence, not a single image.
- **Headless verification:** `--render-test <dir>` (in `main_qt.cpp` → `MainWindow_Qt::runHeadlessRenderTest`) renders one frame of plate 0 in all six formats and `std::_Exit`s. It `_Exit`s deliberately to skip Qt's global teardown, which trips a **pre-existing trace trap in `gfcPlaybackGUI`'s destructor** on macOS (unrelated to saving; see plate-polish backlog). Verified: `multipart.0001.exr` → 877×876 JPEG/EXR(half)/TIFF/TGA/BMP/PNG.

## 19. Render dialog GL-context bug + video export

- **The render dialog must find the viewport via `parentWidget()`, not `window()`.** `RenderDialog_Qt` is a modal `QDialog`, so `window()` returns the dialog itself (a top-level window), not the `MainWindow`. The original `qobject_cast<MainWindow_Qt*>(window())` returned null → `makeCurrent()` skipped → the render ran on no/stale GL context → **black frames**. Walk `parentWidget()` up to `MainWindow_Qt`. (This had shipped black renders since image-saving landed; the headless `--render-test` missed it because it calls `viewport_->makeCurrent()` directly.)
- **Responsive/cancellable render:** one frame per `QTimer::singleShot(0)` step, GL on the main thread. A `QThread` is unsafe — the viewport's `QOpenGLWidget` context is thread-affine. The Render button doubles as Cancel.
- **Video export (`VideoEncoder_qt`):** shells out to the **FFmpeg CLI** via `QProcess` (not libav* — keeps the GPL app lean, one codepath on all platforms). Video formats are combo indices ≥ 6 (H.264/H.265/ProRes); they render a temp PNG sequence then encode. **Varying per-frame sizes** (e.g. the Beachball multipart EXRs) are normalised with a `scale=…:force_original_aspect_ratio=decrease,pad=…` filter to the first frame's even-rounded size — h264/h265 need even dims. Progress is parsed from ffmpeg's `frame=` stderr; cancel kills the process. ffmpeg resolution order: `$JEFECHECK_FFMPEG` → QSettings `Render/ffmpegPath` → bundled (`Contents/Resources/ffmpeg` on mac, next to the exe elsewhere) → system PATH.
- **ffmpeg bundling:** both release packaging *and* the CMake build pull a GPL static ffmpeg (`eugeneware/ffmpeg-static` `b6.1.1`) per platform into the bundled location, so a Finder-launched `.app` (minimal PATH) still finds it. `CMakeLists.txt` fetches it once at configure into the build tree and copies it into the bundle (`-DJEFECHECK_BUNDLE_FFMPEG=OFF` to skip, `-DJEFECHECK_FFMPEG_BIN=…` / `$JEFECHECK_FFMPEG_BIN` to use a local binary offline). `packaging/ffmpeg-NOTICE.txt` documents the GPL license. `create_macos_bundle.sh` accepts the binary from either `build/jefecheck` or the `.app` bundle.
- **Headless verification:** `--video-test <dir>` renders the in/out range to temp PNGs, encodes an H.264 mp4, `_Exit`s 0/2. Verified end-to-end (valid h264, 8 frames, real content) including bundled-ffmpeg resolution with a sanitised PATH.

## 20. Render dialog: resolution, file controls, 16-bit (float FBO)

- **Output resolution actually works now.** The renderer used to size the FBO to the source texture and ignore `renderParams.scale` (the old Scale spinbox was a no-op). `gfcRenderParams.outWidth/outHeight` (0 = source) now drive the `forRender` FBO size — `gfcPlate::draw3DrectWithFX` sizes the FBO to the target and the source quad is sampled across it (bilinear scale). Bonus: a fixed target gives **video a constant size** across frames of differing source dims. The dialog's Resolution row = a preset combo (Source/75/50/25%/Custom) + W×H spinners; presets recompute W×H from `jefe::qt::getRenderSourceSize(quadrant)` (→ `gfcPlate::getRenderSourceSize`), editing W/H flips to Custom. The spinners are the source of truth.
- **16-bit PNG/TIFF needs a float FBO.** The plate FBO is 8-bit (`GL_RGBA`), so a 16-bit container would otherwise carry only 8-bit precision (EXR readback was likewise 8-bit-limited). When a render requests 16-bit (PNG/TIFF) or EXR, `gfcPlate` (re)creates the FBO as `GL_RGBA16F_ARB` (`createFloatFBO` flag, `fboIsFloat` tracks the current format so it recreates when the request flips; on-screen / 8-bit renders keep the 8-bit FBO — no regression). The saver reads back `GL_FLOAT` and writes `UINT16`. Verified real precision via an 8-bit round-trip diff (16-bit values land *between* 8-bit steps). Real precision needs high-bit **source** (EXR/16-half — the default decode depth).
- **Per-format quality** is a `QStackedWidget` keyed on the format index (`updateQualityPage` maps all video formats to one page). Saver `applyFormatAttributes` sets the OIIO attributes; the EXR compression combo order **must** match `kExrComp` in `gfcimagesaver.cpp`. JPEG carries progressive + `jpeg:subsampling`; video carries bitrate-mode (CRF `-crf` vs target `-b:v`) + x264/x265 `-preset`.
- **Status link:** after a successful render the status label becomes a `QDesktopServices`-backed "Show in folder ↗" link (`lastOutputDir_`); reverts to plain text on the next render / cancel / error.

## 21. Color-pick subsystem (histogram + AOI dragging)

The in-viewport overlays (`gfcHistogramGLWindow`, AOI corners) are dragged via a GL color-pick pass, not Qt mouse hit-testing. FLTK wired this in `main.cpp`; the Qt port hadn't, so the overlays were undraggable until fixed. `initializeRenderingChain` now registers the pick subsystem (`pickManager.registerDrawee(&plateManager)` + `registerNotifee` + `registerPlatesAsPickNotifees`), and `GlViewport_Qt` press/move/release call `jefe::qt::viewportPickDown/Drag/Up` with the **GL context current** (the pick pass renders unique colors then `glReadPixels` the pixel under the cursor; coords are framebuffer pixels, bottom-left origin). A press that hits an overlay latches a drag and suppresses the plate pan; a press that hits nothing falls through to pan/active-plate (`gfcPlate::pickNotify` reports only the histogram + AOI corners, not the image frame). `update()` always fires after a pick so the transient selection render never reaches the screen.

## 22. Text renderer (`GfcTextRenderer`) implementation details

Custom renderer replacing FLTK's `gl_draw`/`gl_font` (fixes text squashing in multi-plate layouts). Singleton via `textRenderer()`; **FreeType** rasterizes hinted glyphs into a dynamically-sized `GL_ALPHA` atlas; all text draws in a **pixel-exact orthographic projection** via `gluProject` (1:1 texel-to-pixel regardless of plate projection); **two-pass shadow** (dark offset pass + foreground pass, configurable offset/color/blur); wrapper functions (`gfc_gl_font`, `gfc_gl_draw`, …) match FLTK signatures for minimal call-site changes. Files: `src/gfcTextRenderer.{h,cpp}`.

- Atlas baked at `fontSize * dpiScale` texels for Retina; `drawLine()` uses atlas pixel sizes directly (no dpiScale division) since rendering is in physical pixel space.
- `emitQuads()` snaps glyph positions to integer pixels; baseline and cursor snapped before glyph offsets applied.
- `GL_NEAREST` filter for pixel-perfect rendering; `GL_LINEAR` available via preferences.
- Hinting: `FT_LOAD_TARGET_LIGHT` (default, smooth diagonals) / `FT_LOAD_TARGET_NORMAL` / `FT_LOAD_FORCE_AUTOHINT`.
- Gamma correction (`powf(coverage, gamma)`) boosts semi-transparent edge pixels for a bolder look.
- `loadFont()`/`loadBoldFont()` invalidate all cached atlases so font changes take effect immediately.
- System fonts enumerated via FreeType from platform dirs. Font data kept in memory vectors; `FT_Library` is a static singleton; `FT_Face` created per `bakeAtlas()`.
- Before drawing text quads, disables the active shader program (`glGetHandleARB`/`glUseProgramObjectARB(0)`).

Text-rendering preferences (font path, size, color, opacity, shadow, hinting, filter, gamma) persist via XML (`gfcStructures.cpp` `saveSetting()`/`setWidgetFromNode()`); applied in `PreferencesCB` (`UICallbacks.cpp`).

## 23. Combined FX panel (`FXParamPanel_Qt`) + drag-to-reorder

The FX UI is a **single combined "effect controls" panel** for the active plate — `FXParamPanel_Qt` (`src/qt/FXParamPanel_qt.{h,cpp}`), hosted in `fxParamsDock_` (title "FX", **F3**, objectName `dock.fxparams` / menu `menu.panels.fxparams` — JEF-17 renamed the old "Dialogs" menu to "Panels"). The old separate FX browser (`FXStackPanel_Qt`, an available-list + stack-list + Add/Remove/Refresh) and its `fxDock_` (F2 "FX Stack") were **removed** — all FX autoload at startup, so there is no available/loaded-status browser to show. `LUTPanel_Qt` + `lutDock_` (F4) are untouched.

Layout, top → bottom:
- **"+ Add FX"** `QToolButton` (`fxparams.addfx.button`, `QToolButton::InstantPopup`) with a hierarchical `QMenu`. The menu is (re)built lazily on `aboutToShow` from `jefe::qt::getAvailableFXMenu()` → `vector<pair<int fxIndex, string menuName>>`. Each `menuName` is `"Category/Subcategory/Name"`; we split on `'/'`, walk/create submenus for all but the last segment, and hang the leaf `QAction` (carrying `fxIndex` via `setData`) under the last submenu. Triggering a leaf calls `setActivePlate(getActivePlate())` defensively, then `addFXToActivePlate(fxIndex)`, then `refresh()`.
- A **`QListWidget` of per-FX cards** (`fxparams.list`) in render order. Each card is a `QFrame` containing a header row (drag-handle glyph `☰`, `"N. <name>"`, an **active `QCheckBox`** `fxparams.fxN.active.check`, a **remove `QToolButton`** `fxparams.fxN.remove.button` `⌫`) followed by that FX's **inline params** — the same float `QDoubleSpinBox` / bool `QCheckBox` / choice `QComboBox` editors as before; **texture/cube/LUT/other slots stay read-only `QLabel`s**.

Wiring (all through `jefe::qt::*`, TU-safe per §1):
- active checkbox → `setFXActiveOnPlate(plate, fxIdx, on)`; remove → `removeFXFromPlate(plate, fxIdx)`; param edits → existing `setFXParamValueOnPlate(...)`.
- **Texture params** (`FX_GUI_TEXTURE`, e.g. the two inputs of a Comp/Add) are a `QComboBox` source picker with fixed options **Previous / Track A / Track B / Track C / Track D** — mirroring the FLTK `Fl_Choice` in `fxcontrolwindow.cpp`. The stored value is the option index; `gfcFX::bind()` maps `0 → previousTexID` (prior pass's FBO result), `1..4 → trackManager.getSequence(value-1)`. Wired exactly like the `Choice` combo (same `setFXParamValueOnPlate` + `viewportRepaintRequested`), and shares its fast-path value-update branch.
- **Cube / LUT params** (`FX_GUI_CUBE` = 3D LUT, `FX_GUI_LUT` = 1D LUT; e.g. the Color/3D LUT and Color/1D LUT FX) are a `QComboBox` LUT picker fed by `jefe::qt::getCubeLutChoices()` / `getLut1DChoices()` → `vector<pair<int globalLutIndex, string name>>` (from `lutManager.get3DLutNames()` / `get1DLutNames()`). **Key gotcha:** the combo DISPLAYS lut names but the stored param value is the **global lutManager index, not the list position** — `gfcFX::bind()` reads `getLUT(value).texture3D/.texture1D`. So each item carries its global index in `Qt::UserRole`; selection writes `itemData(v)`, and both the initial set and the fast-path update locate the item whose `itemData == value` (not `setCurrentIndex(value)`). Empty list (no LUTs loaded yet) renders a "(no 3D/1D LUTs loaded)" label instead of an empty combo; a later full rebuild picks them up. This matches the FLTK `Fl_Choice` that matched the current LUT by name.
- **Drag-to-reorder:** the list is `QAbstractItemView::InternalMove`. Editors inside the cards keep their own mouse events because spin/check/combo *accept* presses while the handle/empty card area (plain `QLabel`/`QFrame`) *ignore* them and the press propagates to the list viewport, which starts the drag. Reorder detection uses a `QListWidget` subclass `FXReorderList_Qt` that overrides `dropEvent`: it snapshots each item's stashed stack index (`Qt::UserRole`) **before** and **after** the base `dropEvent`, derives the single `(from, to)` move (compare first/last differing positions; up-move when `after[lo]==before[hi]`, else down-move), and emits `itemsReordered(from, to)`. The panel calls `moveFXOnPlate(plate, from, to)`. (We override `dropEvent` rather than listen to `rowsMoved` because `QListWidget` InternalMove drops don't reliably emit `rowsMoved` across Qt versions, and `setItemWidget` items get destroyed/recreated on the move anyway.)

**Repaint:** the panel emits `viewportRepaintRequested()` after every stack-changing edit (add/remove/reorder/active-toggle/param edit); MainWindow connects it to `viewport_->update()`. Necessary because the idle playback tick (`needsPlaybackTick()` gate) skips repaints when nothing is playing, so a stack mutation otherwise wouldn't show until the next viewport mouse-move. The bridge mutators already call `plateManager.setChanged()` (§3 FX equivalent).

**Refresh fast path preserved:** `refresh()` keeps the `lastActivePlate_`/`lastStack_` fingerprint + `rowCache_` from the old param panel — `plateStateChanged` fires per viewport mouse-move, so when the active plate and stack-name list are unchanged it walks `refreshValuesOnly()` (in-place editor value updates, signal-blocked) instead of tearing down + rebuilding the cards. The `refreshing_` reentrancy guard stops programmatic widget updates from firing bridge writes. Remove and reorder defer their rebuild via `QTimer::singleShot(0, …)` because the triggering widget lives inside the card being torn down.

## 24. QOpenGLWidget's default framebuffer is NOT 0 — FX/FBO "return to screen" binds

**Symptom:** with an FX active, the viewport renders **black — not even the text** — and in multiplate any *sibling* plate (even one with no FX) also goes black.

**Root cause:** `QOpenGLWidget` renders into its **own** framebuffer object (commonly id **1**), reachable via `defaultFramebufferObject()`. The FX/FBO code in `gfcPlate.cpp` was written for FLTK, where the window framebuffer was **0**, so it did `glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0)` to "return to screen". Under Qt that binds the real (unpresented) default framebuffer, so the FX plate's final composite — and **everything drawn after it in the frame** (sibling plates, text) — lands in a buffer Qt never shows. Single-plate *looked* okay only because nothing else draws after it that frame; the bug bites the moment a second plate draws.

**Fix:** publish Qt's default FBO into the rendering chain each frame and rebind *that* instead of `0`.
- `extern GLuint gScreenFBO;` (decl in `gfcPlate.h`, def in `gfcPlate.cpp`, init 0 = FLTK behavior).
- `GlViewport_Qt::paintGL()` calls `jefe::qt::setScreenFBO(defaultFramebufferObject())` **before** `onDraw()` (it can change on resize — refresh every paint).
- The three `glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0)` "return to screen" sites in `gfcPlate.cpp` (`createFBO` ×2, `draw3DrectWithFX` last pass) now bind `gScreenFBO`. These were the only bind-0 sites in the codebase.

**Diagnostic that nailed it:** a temporary probe in `paintGL` printing `defaultFramebufferObject()` + `GL_FRAMEBUFFER_BINDING` before/after `onDraw()` showed normal draws stay `1→1` but an FX draw goes `1→0` (left bound to 0).

**Verification gap this exposed (important):** the original `--fx-test` proved FX worked via the **`forRender` FBO-readback path**, which reads the result texture *before* the bind-to-screen and so never hits this bug. Any "FX renders" check **must exercise the on-screen path**. Headless coverage now does: `--fx-test` adds an on-screen `grabFramebuffer()` (forces `paintGL`) before/after; `--fx-multitest <image>` loads two plates side-by-side, adds an FX to plate 0 only, and asserts (a) the sibling plate stays non-black and unchanged (no leak) and (b) plate 0 visibly changes (FX applied on screen). Use a **bright, horizontally-asymmetric** test image — a dark one makes the flip's absolute pixel diff too small to assert on.

## 25. Playlist panel (`PlaylistPanel_Qt`) — card pattern, POD boundary, auto-advance, reorder

**Files:** `src/qt/PlaylistPanel_qt.{h,cpp}`, `src/qt/SequenceLoadBridge_qt.{h,cpp}`, `src/qt/MainWindow_qt.cpp`.

**Card pattern (mirrors FX panel §23).** `PlaylistPanel_Qt` hosts a `QListWidget` (`playlist.list`) whose items are `PlaylistItemCard` custom `QWidget` widgets set via `setItemWidget`. Each card shows a drag-handle glyph (affordance only — see reorder below), a name label (`playlist.card.<i>.name`), a chevron (`playlist.card.<i>.chevron`) that collapses/expands the per-track detail section, and a per-card remove button (`playlist.card.<i>.remove`). The card widget itself is `playlist.card.<i>`. Expanded detail rows are rebuilt from `jefe::qt::getPlaylistItemDetail(index)` — one row per loaded track showing filename (Compact vs Full-paths toggle), range, and scale. Cards connect `loadRequested` → `loadRow`, `removeRequested` → `removePlaylistItem` + `refreshList`, `toggleExpandRequested` → collapse the previously expanded card then expand the new one.

**`PlaylistTrackDetail` POD boundary.** The panel TU (`PlaylistPanel_qt.cpp`) does not include any rendering-chain manager headers. It reads per-item detail exclusively via `jefe::qt::getPlaylistItemDetail(int index)` → `std::vector<PlaylistTrackDetail>` (declared in `SequenceLoadBridge_qt.h`). `PlaylistTrackDetail` is a plain POD struct (filename, range, scale fields); the bridge TU (`SequenceLoadBridge_qt.cpp`) fills it from `playlistManager` + `trackManager` calls. This keeps the glad/QOpenGLWidget header-clash boundary (§1) intact.

**Auto-advance one-shot latch.** ONCE playback mode (`LOOPMODEONCE_ID`) clamps `currentFrame` at the end limit and leaves `isPlaying()` true — the engine never self-stops. Auto-advance is edge-detected in `tickPlaybackTiming()` (`SequenceLoadBridge_qt.cpp`): when current frame transitions from a non-end value to the end limit while playing ONCE-forward, the file-static `gPlaylistAdvanceLatch` is set to `true`. The latch is a one-shot: `consumePlaylistAdvanceSignal()` returns `true` and resets it atomically. `MainWindow_qt.cpp`'s idle tick calls `consumePlaylistAdvanceSignal()` and, if set and the playlist panel exists, delegates to `PlaylistPanel_Qt::advanceToNext()` which selects the next row and calls `loadPlaylistItemAndPlay`. The helper `playlistEffectiveEndLimit()` (anonymous namespace in `SequenceLoadBridge_qt.cpp`) mirrors the engine's private `gfcPlaybackManager::getEndLimit()` using only public accessors (`getLoopPriority`, `getFirstLastLoaded`, `getLastLastLoaded`, `getOutPoint`). **Maintenance note:** if the engine's end-limit boundary logic changes, `playlistEffectiveEndLimit()` must be updated to match. **Stills limitation:** auto-advance requires a frame transition into the end limit, so single-frame/still items do not auto-advance — a clip already clamped at its end (frame == end from the first tick) never triggers the edge-detect, and the latch is never set.

**Load path distinction.** `loadPlaylistItem(int index)` is the manual load path (double-click / Enter key / card `loadRequested` signal) — loads tracks without resuming playback. `loadPlaylistItemAndPlay(int index)` is the auto-advance path — loads tracks then calls `startPlayFwd()` to resume forward playback immediately. Never substitute one for the other.

**Reorder: DropOnly + buttons/keyboard; drag is affordance only.** The list's drag-drop mode is `QAbstractItemView::DropOnly` (not `InternalMove`) — in-list drag reorder is intentionally disabled because a `QListWidget` InternalMove would reorder the visual rows but not the backing `playlistManager` vector, silently diverging. Reorder is implemented via the ↑/↓ toolbar buttons and `Shift+↑` / `Shift+↓` keys, each calling `movePlaylistItemUp`/`movePlaylistItemDown` then `refreshList`. The drag-handle glyph on cards is a visual affordance. External file drops (Finder/Explorer → list) are accepted via a `QEvent::DragEnter` / `QEvent::DragMove` event-filter that calls `de->acceptProposedAction()` for URL payloads — necessary because `DropOnly`'s model rejects URL mime by default and would suppress the `Drop` event. Drop routing: a `.jpl` file calls `loadPlaylistFile` (replaces the whole list); a media drop on an existing card calls `appendTracksToPlaylistItem`; a media drop on an empty area calls `addPlaylistFiles`.

## 26. Remote-session runtime wiring (Qt / JEF-4)

**Files:** `src/qt/RemotePanel_qt.{h,cpp}`, `src/qt/SequenceLoadBridge_qt.{h,cpp}`, `src/qt/GlViewport_qt.cpp`, `src/qt/MainWindow_qt.{h,cpp}`, `src/main_qt.cpp`.

### Network pump — always-on, outside the playback gate

`MainWindow_Qt::playbackTimer_` fires every 4 ms (`Qt::PreciseTimer`). The timer lambda calls `jefe::qt::pumpNetwork()` **before** the `needsPlaybackTick()` gate, so RakNet sockets are serviced even when playback is idle:

```cpp
// MainWindow_qt.cpp ~308
if (jefe::qt::pumpNetwork() && remoteDialog_)
    remoteDialog_->refreshConnectionState();
const bool needsTick = jefe::qt::needsPlaybackTick();
```

`pumpNetwork()` (`SequenceLoadBridge_qt.cpp`) calls `networkManager.update()` and returns `true` when connection state, participant list, or chat log changed — that return value gates the cheap `refreshConnectionState()` call so the panel only repaints when something actually changed.

### TU-safe getters — the only path to network state

UI code (panel, overlay, dialog) never includes `gfcNetworkManager.h` directly; that header lives in the bridge TU. All state is read through four getters declared in `SequenceLoadBridge_qt.h`:

| Getter | Manager call |
|---|---|
| `jefe::qt::remoteStatusText()` | `networkManager.connectionStatusText()` |
| `jefe::qt::remoteParticipants()` | `networkManager.participantNames()` |
| `jefe::qt::remoteChatLog()` | `networkManager.chatLogLines()` |
| `jefe::qt::remoteErrors()` | `networkManager.drainErrors()` |

`drainErrors()` currently returns an empty vector — errors still surface via the RED status string returned by `connectionStatusText()`; a dedicated error queue is a later refinement. The old `*gui_qt` stub-class approach was removed in favour of these plain-function getters.

### Modeless Remote dialog — lazy-created, show/raise

`MainWindow_Qt` holds `RemoteDialog_Qt* remoteDialog_ = nullptr`. Both "File → Remote Session…" and "Dialogs → Remote Session…" share one closure:

```cpp
// MainWindow_qt.cpp ~472
if (!remoteDialog_) remoteDialog_ = new RemoteDialog_Qt(this);
remoteDialog_->show();
remoteDialog_->raise();
remoteDialog_->activateWindow();
remoteDialog_->refreshConnectionState();
```

`RemoteDialog_Qt` is a non-modal `QDialog`. It exposes **Start server** / **Connect** / **Disconnect** buttons, a status label, a participant list, an error label, and a chat-log text area — all refreshed via the TU-safe getters in `refreshConnectionState()`. The bridge functions `connectAsServer(RemoteServerParams)`, `connectAsClient(RemoteClientParams)`, and `disconnectRemote()` keep the button handlers free of manager includes.

### Chat + remote-pointer overlay in `GlViewport_Qt::paintGL`

After `onDraw()` the viewport calls:

```cpp
// GlViewport_qt.cpp ~123
jefe::qt::drawNetworkOverlay(int(width() * dpr), int(height() * dpr));
```

`drawNetworkOverlay` forwards to `networkManager.draw(w, h)`, which composites the chat-compose line and all remote-pointer cursors over the plates using the existing FLTK-era GL overlay code.

**Chat keyboard entry** (`GlViewport_Qt::keyPressEvent`, ~393):

1. If `remoteChatModeActive()` is true (composing), keystrokes route to `remoteChatAppend` / `remoteChatBackspace` / `remoteChatSubmit` (Enter) / `remoteChatCancel` (Esc); the handler returns early so plate shortcuts are suppressed.
2. If not composing and the user presses Return/Enter while `isRemoteConnected()`, `remoteChatBegin()` activates compose mode.

**Remote pointer broadcast** (`GlViewport_Qt::mouseMoveEvent`, ~239):

```cpp
jefe::qt::sendRemotePointer(xPx, yPx);
```

`sendRemotePointer` (bridge) drops unchanged positions and throttles to ~60Hz (16ms gate) so hover motion cannot flood the reliable-ordered channel shared with playback/CC/FX/chat messages.

### Automated two-process `--remote-test`

`--remote-test` (orchestrator/server, `main_qt.cpp`) and `--remote-test-peer <ip> <port>` (child/client) form a headless localhost harness. Port 60123 is used. The orchestrator:

1. Calls `jefe::qt::remoteTestServerSawPlay(port, settleMs=4000)` which hosts a server, pumps for up to 4 s, and returns `true` as soon as `isPlaying()` becomes true (the peer's mirrored play message arrived).
2. Spawns the peer via `QProcess` with `--remote-test-peer 127.0.0.1 60123`. The peer connects, calls `togglePlayFwd()` (sends a play/pause to the server), holds for 2 s, then exits.
3. Asserts `peak participants >= 1` and `mirrored_play == 1`; exits 0 on success, 2 on failure.

**Recorded test run (2026-07-03):**

```
$ ./build/jefecheck.app/Contents/MacOS/jefecheck --remote-test; echo "exit=$?"
No timer
Client Connected!
Connections = 1
Nickname added: jefe-remote-test
ColorAdded-2139062272
Sending 1 nicknames
sending jefe-remote-test
Got a LoadedFXHashes
Creating bitstream
Got 0 hashes
fxManager.getHashmap returned a hash map with 0 members
Client: Server requested we send 0 FXs...FXs sent
Server: Got a GFCNETID_REQUESTEDFXS
 parsing and loading 0 FXs
 parsing and loading 0 FXs
Got a LoadedLUTHashes
Creating bitstream
Got 0 hashes
lutManager.getHashmap returned a hash map with 0 members
Connections = 2
Nickname added: peer
ColorAdded-2139062272
Sending 2 nicknames
sending jefe-remote-test
sending peer
Client: Got GFCNETID_REQUESTLUTS
 0 LUTs Requested
Server: Got a GFCNETID_REQUESTEDLUTS
 parsing and loading 0 LUTs
Got a LoadedFXHashes
Creating bitstream
Got 0 hashes
fxManager.getHashmap returned a hash map with 0 members
 parsing and loading 0 LUTs
Server: Got a GFCNETID_REQUESTEDFXS
 parsing and loading 0 FXs
No timer
REMOTE-TEST: participants=2 mirrored_play=1
exit=0
```

### Manual two-instance verification recipe

The steps below require two GUI windows (performed by a human; `--remote-test` covers the connect + mirrored-play assertions automatically).

**Optional startup smoke-check (no human needed):** launch two instances backgrounded and confirm neither crashes before the kill:

```bash
./build/jefecheck.app/Contents/MacOS/jefecheck &
./build/jefecheck.app/Contents/MacOS/jefecheck &
sleep 2 && kill %1 %2
```

Both instances launched and terminated cleanly on 2026-07-03 (verified by the task-9 agent run). The full interactive steps were not performed by automation; they are for human verification:

1. Launch two instances. On macOS, double-clicking or `open build/jefecheck.app` a second time just re-activates the one running instance (LaunchServices single-instance behavior) — you get only one window. Force two separate processes with either:
   - `open -n build/jefecheck.app` (run twice — `-n` opens a new instance each time), or
   - `./build/jefecheck.app/Contents/MacOS/jefecheck &` (run the inner Mach-O binary directly, twice — this is what `--remote-test` does via QProcess).
2. **Instance A:** File → Remote Session… → fill in a server name and port (e.g. 60000) → **Start server**. Status should read "Hosting on port 60000".
3. **Instance B:** File → Remote Session… → fill in `127.0.0.1` and port 60000 → **Connect**. Status should read "Connected".
4. Confirm both participant lists show the peer's name; both status labels show connected/hosting.
5. Load the same media via the Playlist on both. On A, press Space to play/pause — confirm B mirrors the playback state. W-drag on the viewport to adjust gamma on A — confirm B reflects the change. Toggle an FX on A — confirm B shows the same FX active.
6. On A, press Return to enter chat mode, type a message, press Enter to send — confirm the message appears in B's viewport overlay and in both panels' chat-log area. **Note:** Entering chat mode requires the viewport to have keyboard focus (click the viewport first), since Return only enters chat mode when the GlViewport has focus.
7. Move the cursor around A's viewport — confirm B shows A's remote-pointer cursor composite over its plates.
8. On B, click **Disconnect** — confirm A's participant list shrinks to 1 (only itself). Reconnect via B's **Connect** button — confirm both participant lists grow to 2 again.

**Known limitation:** single-frame/still items do not trigger auto-advance in the Playlist (the latch requires a frame-number transition into the end limit — see §25).

## 27. Preferences window (Qt) — persistence + sections

**Files:** `src/qt/PreferencesWindow_qt.{h,cpp}`, `src/qt/qt_prefs_persist.{h,cpp}`, `src/qt/RenderBridge_qt.cpp`, `src/gfcimageloaderoiio.cpp`, `tests/ui/jefecheck/locators.py`. Spec/plan: `docs/superpowers/` (JEF-16).

**`qt_prefs_persist` is the single store.** `QSettings` (native format — `~/Library/Preferences/...plist` on macOS) is the *only* real preferences persistence in the Qt build. `loadPreferences()` reads every key into the global `sett` (gfcSettings) once at startup, after `sett`'s default constructor runs — a missing key just keeps the constructor default, so first-run behavior is unchanged. `writePreferences()` writes every `sett` field back to `QSettings` on Preferences → Done. Both functions are organized by section (Engine/Session/General/Formats/Search/Remote key prefixes) and each JEF-16 task appended its section's keys to both — see the "NOTE: later tasks append..." markers at the end of each function if a new section is ever added. **The legacy XML `saveSettings()`/`readSettings()` in `gfcStructures.cpp` are dead stubs** — grep confirms nothing in the Qt build calls them; do not resurrect that path when adding a new preference.

**Load / Done / Cancel model.** `PreferencesWindow_Qt`'s constructor snapshots the live `sett` into `sett_backup_` (`std::unique_ptr<gfcSettings>`) before building any page. Every page's widgets connect directly to lambdas that mutate `sett` *live*, as the user edits — this is deliberate, not an oversight: it lets other UI (e.g. the viewport background repainting when the color picker changes) reflect changes immediately without waiting for Done. The dialog's button box (`QDialogButtonBox::Save | Cancel`, Save relabeled "Done"):
- **Done** → `writeTextPrefs()` (Text/* keys) then `jefe::qt::writePreferences()` (everything else), then `accept()`.
- **Cancel** → `sett = *sett_backup_` (blunt full-struct revert of every live mutation made while the dialog was open) then `jefe::qt::applyTextPrefs()` (see below), then `reject()`. Nothing is written to `QSettings` on Cancel.

**Text prefs use deferred (Done-writes) persistence, not the live-mutation model.** `GfcTextRenderer` (§22) is a separate singleton, not a field of `sett`, so it can't ride the `sett_backup_` snapshot/revert. Instead: live edits on the Text page call `textRenderer()` setters directly (`setSize`, `setColor`, `setHintMode`, `setFilterNearest`, `setGamma`, `setShadow*`) for instant on-screen preview, but nothing is written to `QSettings` until Done, when `PreferencesWindow_Qt::writeTextPrefs()` writes the current widget values to the `Text/*` keys. If the user hits Cancel instead, `jefe::qt::applyTextPrefs()` re-reads the *last-saved* `Text/*` values (falling back to `GfcTextRenderer`'s constructor defaults on first run) and pushes them back into the renderer via the same setters — reverting whatever the user previewed live. `applyTextPrefs()` is also called once from `loadPreferences()` at startup.

**Checkerboard background.** General page: `bgColor` (float 0-1 gray) + `bgCheckerboard` (bool). Drawn in `RenderBridge_Qt::onDraw()` (`src/qt/RenderBridge_qt.cpp`): when `bgCheckerboard` is set, `drawCheckerboardBackground()` fills the whole framebuffer with an immediate-mode/glOrtho two-tone grid (~24 logical px cells, tones nudged ±0.06 off `bgColor` and clamped to `[0,1]`) instead of the flat `glClearColor(bgColor,...)` fill.

**EXR display-window + aspect wiring (`gfcimageloaderoiio.cpp`).** Two independent Formats toggles:
- `exrIgnoreDisplayWindow` — when clear (default), a data window smaller than the display window (OpenEXR overscan/crop — detected via `spec.full_width/full_height/full_x/full_y != spec.width/height/x/y`) is composited into a bitmap sized to the *display* window (`outW/outH = spec.full_width/full_height`), offset by `dispDX/dispDY = spec.x - spec.full_x` / `spec.y - spec.full_y`. Setting the toggle skips this and loads the data window 1:1, ignoring overscan.
- `exrIgnoreHeadersAspectRatio` — when clear (default), the loaded quad's `quadSizeX` is stretched by the header's `PixelAspectRatio` attribute (`par`) to correct non-square pixels. Setting the toggle leaves `quadSizeX` at the raw pixel width.
Both toggles are read directly off the global `sett` at load time (no caching), so they take effect on the next load — a preference change doesn't retroactively affect an already-loaded frame.

**Object-name scheme.** Every control in the redesigned dialog follows `preferences.<section>.<field>.<role>` (role = `button`/`check`/`combo`/`spin`/`edit`/`list`), e.g. `preferences.general.bgcolor.button`, `preferences.engine.balance.check`, `preferences.remote.chatfade.spin`. Dialog chrome uses `preferences.dialog` / `preferences.sidebar` / `preferences.pages` / `preferences.buttons` / `preferences.done.button` / `preferences.cancel.button`. `tests/ui/jefecheck/locators.py` mirrors every leaf as a `PREFS_<SECTION>_<FIELD>` (or `PREFERENCES_<CHROME>`) constant for Mac2/XCUITest lookups — as of JEF-16 Task 8 there are no stale pre-redesign names (`prefs.engine.*` camelCase, `prefs.session.startup`, etc.) left in either file; a full-repo grep for `prefs.engine.` / `prefs.session.` / `prefs.general.` / etc. returns only the current dotted-leaf names.

## 28. Menu / shortcut / on-screen-help cleanup (JEF-17)

The FLTK→Qt port left docks reachable from two menus (the newer View toggles *and* a legacy "Dialogs" F-key menu), with Remote/Render also duplicated into File, and the on-screen help overlay was stale FLTK text with no key binding. The cleanup (`MainWindow_qt.cpp` `buildMenuBar`/`buildDocks`, `gfcplatemanager.cpp`):

- **Menu bar is now File · View · Panels · Help.** The legacy **"Dialogs" menu is renamed "Panels"** (`menu.panels`) — deliberately *not* "Window", because macOS reserves the Window menu for Minimize/Zoom/window-list, which we don't provide; naming it "Window" would imply commands that aren't there. Verified at runtime there is exactly one such menu (no macOS auto-Window duplication).
- **Panels** is the single home for every dock: the five **dock `toggleViewAction()`s** (checkable, checkmark tracks visibility) with the FLTK F-keys reassigned there (`F2` Plate Manager, `F3` FX, `F4` LUTs; Timeline/Playlist keyless), plus **Remote Session… (`F5`)** and **Render… (`F6`)** launchers and **Hide Controls (`Ctrl+Alt+F`)**. Showing a dock also `raise()`s it (via `QAction::toggled`). The old duplicate Render/Remote entries were removed from **File**.
- **`panelsMenu_` is a stored member** populated in `buildDocks()` (after the docks exist), replacing the old fragile "find the View menu by title-string match" lookup.
- **View** is display + active-plate transforms: Fullscreen, Histogram, Status Bar, CC Favorites, then **Fit `F` / Flip `V` / Flop `M` / Text `T` / Reset Plate `Ctrl+R` / Reset CC `Shift+R`** — these were previously keyboard-only `QShortcut`s. Each is now a **View `QAction` that OWNS its shortcut** (`setShortcutContext(Qt::ApplicationShortcut)` so it still fires when a dock is floating). The **"all-plates" variants** (`Shift+F`, `Shift+V`, `Shift+M`, `Alt+T`, `Ctrl+Alt+R`, `Shift+Alt+R`) stay as standalone ctor `QShortcut`s (no menu entry). **Do not** also leave a `QShortcut` for a key a menu action owns — the duplicate binding is an "ambiguous shortcut" that misfires.
- **Bare `H` now toggles the on-screen help overlay** (`Help → Toggle On-Screen Help`, `menu.help.onscreen`, ApplicationShortcut). **Flop moved off `H` to `M`/`Shift+M`** to free it (flip/flop also have per-Plate-Manager-card toggle buttons, so the keys are a convenience, not the only path). Verified: typing `h` into a dialog text field does *not* trigger help (Qt's `ShortcutOverride` lets the focused editor claim printable keys).
- **The help overlay text** (`helpMessage`, `gfcplatemanager.cpp` ctor) was rewritten to match the real Qt bindings and must be kept in sync with `bindPlateAction`/`bindLayout` (MainWindow ctor), `GlViewport_Qt::keyPressEvent` (playback/in-out/track), and the viewport mouse handlers (pan/zoom/CC-drag/remote-pointer). The dead empty `gfcPlateManager::drawHelp()` stub is unused.
- **Locators**: `tests/ui/jefecheck/locators.py` gained `MENU_VIEW_FIT/FLIP/FLOP/TEXTMODE/RESETPLATE/RESETCC` and `MENU_PANELS_*`. On macOS these objectNames don't reach the AX tree (menu items show as `qt_itemFired:`), so tests drive menus by their **keyboard shortcut** — which is why accurate, non-colliding shortcuts matter.

## 29. Shared icon factory (`qticons`) — JEF-19

The app ships **no icon asset files** and uses no `.qrc` resources or `QStyle::standardIcon`; every icon is drawn with `QPainter`. JEF-19 promoted the pattern that lived privately in `PlateCard_qt.cpp` (`makeMirrorPixmap`/`makeMirrorIcon`/`makeIconToggle`) into a shared module, `src/qt/qticons.{h,cpp}` (`namespace jefe::qticons`):

- **`make(PaintFn, side=16)`** renders a glyph painter onto a **2×-DPR** transparent pixmap and returns a **dual-state** `QIcon`: light glyph `0xE0E0E0` for `QIcon::Off` (normal dark-theme button) and dark glyph `0x1A1A1A` for `QIcon::On` (the orange `:checked` background). Non-checkable buttons just use Off; checkable ones (play/pause, filmstrip) get the correct contrast when checked. This is the same fixed-dark-theme + orange-accent model as the original — **not** a system light/dark query.
- **`makeIconButton(parent, icon, tooltip, accessibleName, checkable, text)`** builds a compact `QPushButton`; a tooltip is mandatory (icon-only buttons are unusable without one). Icon-only → fixed 24×24; with `text` → icon+label with padding.
- **~21 named glyph getters** (`add`, `addFiles`, `remove`, `trash`, `up`, `down`, `chevron(bool)`, `dragHandle`, `rewind`, `stepBack`, `play`, `pause`, `stepForward`, `fastForward`, `filmstrip`, `refresh`, `folder`, `save`, `check`, `send`, `recent`), each a small painter routine in the `.cpp` anonymous namespace.

**Applied across (full sweep):** Playlist toolbar + per-card buttons (icon-only, the garbled-glyph fix), Timeline transport (`⏮|◀▶▶|⏭` + two-state play/pause + filmstrip toggle), FX `+ Add FX` / per-FX remove, LUT Apply/Refresh, and dialog `Browse…` / Prefs Add-Remove / Remote Send / TrackStrip Recent. **Left as text:** the plate `RGB/R/G/B` channel button (it's a *state label*), color swatches, combos, checkboxes, and `Done`/`Cancel`/`Render` primary buttons. Every touched button carries a tooltip. **All existing `objectName`s were preserved** (tests depend on them); the icon is set on the same widget.

New `.cpp` files are picked up by CMake's `file(GLOB src/qt/*.cpp)` — re-run `cmake -B build` after adding one so the glob re-evaluates.

## 30. Render output was missing colour correction + LUT (super-shader before read-back)

**Bug:** rendered stills/video dropped the plate's colour correction (gamma/exposure/BCS) and its LUT — the output looked like the raw source. FX-stack effects *did* appear.

**Root cause** (`gfcPlate::draw3DrectWithFX`): the FX stack is baked into `fboTexturev[activeFBO]` by the FX loop, but the **super-shader** (`startSuperShader()`/`stopSuperShader()`, which is where gamma/exposure/BCS **and the LUT** live) is only applied in the **on-screen LAST pass** (~line 1531), which draws to the screen. The `forRender` read-back (`glGetTexImage` on `fboTexturev[activeFBO]`, ~line 1437) happens **before** that pass, so it captures the FX result *without* the super-shader. FX survive because they're baked earlier.

**Fix:** at the top of the `if (forRender)` block, before the read-back, do a dedicated super-shader pass — ping-pong to the other FBO attachment, `startSuperShader()`, draw a full-`fboVP` quad sampling the FX result, `stopSuperShader()` — then the existing read-back reads that buffer (now FX + CC + LUT). Wrapped in `glPushAttrib(GL_ALL_ATTRIB_BITS)`; depth-test/blend off; float FBO preserved for 16-bit/EXR. `forceSingleBufferedFX` GPUs can't ping-pong (read==write), so they keep the old behaviour (a non-default fallback).

**Crop bars (JEF render setting):** a Render-dialog checkbox → `gfcRenderParams.bakeCropBars` (default off). When on + `cropOn`, the same block draws the aspect letterbox into the render by scaling the poly-space `cropBarTop/Bottom` rects by `fboVP/polySize` (opaque black — a letterbox is solid, unlike the semi-transparent on-screen preview). Pan/zoom and text/histogram overlays are deliberately NOT baked.

**Regression test:** `--cc-test <image>` (`runHeadlessCCTest`, wired in `main_qt.cpp`) has two stages:
1. **CC/LUT:** render a baseline PNG, apply `adjustPlateExposure`+`adjustPlateGamma`, render again, assert `meanAbsPixelDiff > 1`. Verified **FAIL (diff 0.0000)** with the fix disabled, **PASS (diff ~37.7)** with it.
2. **Crop bars:** `setPlateAspect(0, 0.5f)` + `setPlateCrop(0, true)` (new bridge → `gfcPlateManager::setAspect`/`setCrop`, which push straight to the plate members — the render doesn't re-read aspect/crop from the GUI mid-draw), then render with `bakeCropBars` off vs on and assert the top/bottom bands changed (`borderDiff ~70`) while the centre did not (`centreDiff 0`).

`--fx-test` still passes (no FX regression). `--cc-test` complements it — the FX test can't catch the super-shader bug because FX bake into the FBO earlier. **Note on aspect units:** the crop math treats `aspect` as *content-height ÷ width* (`polySizeX*aspect` = content height), so a value `< 1` letterboxes a square/landscape source; the "2.39:1"→2.39 preset strings are the *inverse* convention, so preset aspects produce off-frame bars on non-portrait sources — a pre-existing crop-feature quirk, orthogonal to this render fix.

## 31. Transport seam (JEF-22)

**Files:** `src/gfcTransport.h` (interface), `src/gfcRakNetTransport.{h,cpp}` (the only RakNet TU), `src/gfcnetworkclient.{h,cpp}`, `src/gfcnetworkserver.{h,cpp}`, `src/gfcNetworkManager.{h,cpp}`.

**Goal:** confine RakNet to one translation unit so a future transport (e.g. WebRTC/websocket for the web port, see `project_future_directions.md`) is a second implementation of `jefe::net::ITransport`, not a rewrite of client/server/manager.

**`ITransport` contract** (`gfcTransport.h`): host (`startHost`/`stopHost`) and client (`connect`/`disconnect`) roles, a poll-based `poll(TransportEvent&)` returning `false` when nothing's pending, `send(data,len,target,broadcastExcluding)`, `closePeer`, `connectionCount`. `PeerId` is `uint64_t` — `packPeerId(binaryAddress, port)` packs a RakNet `SystemAddress` as `binaryAddress<<16 | port`; `kInvalidPeerId = 0`. No RakNet type crosses the interface.

**Poll-based pump preserved.** `MainWindow_Qt::playbackTimer_`'s 4 ms tick (§26) still drives `Update()`/`poll()` on both client and server — the seam didn't touch pacing, only what sits behind `Packet* p = peer->Receive()` (now `transport_->poll(ev)`).

**RakNet confined to `gfcRakNetTransport.{h,cpp}`.** Every other network TU (`gfcnetworkclient.cpp`, `gfcnetworkserver.cpp`, `gfcNetworkManager.cpp`) holds only a `std::unique_ptr<jefe::net::ITransport> transport_` and is RakNet-symbol-free. Audit grep (RakPeerInterface/SystemAddress/RakNetworkFactory/MessageIdentifiers/etc.) comes back clean except literal debug-log strings (`printf("ID_CONNECTION_LOST\n")` etc. — preserved verbatim for output fidelity) and one commented-out dead `peer->Send(...)` block in `gfcnetworkclient.cpp` (kept, per Task 3, since it was already dead in legacy).

**`BitStream`/`StringCompressor` stay app-side until JEF-23.** The seam is Phase 1 (transport only); the wire format itself is still RakNet's `BitStream`, constructed directly in `gfcnetworkclient.cpp`/`gfcnetworkserver.cpp` from `ev.bytes`. A versioned, transport-agnostic serialization format is JEF-23 scope — don't conflate "no RakNet types in the client/server" with "no RakNet wire format" yet.

**`GFCNET_USER_PACKET_BASE = 91` tripwire** (`gfcNetworkStructures.h:44`): the app's `GFCNETID_*` enum starts at this value, which must equal RakNet's `ID_USER_PACKET_ENUM` for the transport to correctly distinguish system events from app packets in `poll()`. `gfcRakNetTransport.cpp:9` has a `static_assert(ID_USER_PACKET_ENUM == GFCNET_USER_PACKET_BASE, ...)` — if a future RakNet upgrade ever renumbers that enum, the build fails loudly instead of silently misrouting packets.

**The `GFCNETID_NEWPEERINSESSION` broadcast-except-target subtlety (`gfcnetworkserver.cpp:677`)**: it sends `(ev.peer, true)` — `broadcastExcluding=true` with a *real* target, not `kInvalidPeerId`. Per the `ITransport::send` contract this means "everyone except `ev.peer`" (the new client doesn't need its own nickname echoed back), not "everyone." Don't "simplify" this to a plain broadcast — it changes who gets a redundant packet, not just the addressing mechanism.

**`startFXSinc`-vs-others broadcast asymmetry (load-bearing).** `startFXSinc(peer, broadcast)` branches explicitly: `broadcast==true` sends with `kInvalidPeerId` (true "everyone"). `startLUTSinc`/`startStackSinc`/`startPlaylistMerge` do **not** branch — they pass `(peerId, broadcast)` straight through even when `broadcast==true`, which under the contract above means "broadcast except `peerId`." This asymmetry is preserved exactly from legacy (verified line-by-line in Task 4); it is not a bug to "fix" for consistency — each call site's `peerId` argument was already chosen assuming its specific semantics.

**Recorded losses.** `ID_MODIFIED_PACKET` (tampered-packet detection) is swallowed by `RakNetTransport::poll()` rather than surfaced as a distinct event — the client-side log line and the `gotMessages` flag it used to set are gone. Revisit in JEF-23 if tamper-detection logging is needed again; today a modified packet is silently dropped rather than logged.

**One-instance-per-role contract.** Both `gfcNetworkClient` and `gfcNetworkServer` assume a single `ITransport` instance for the process's lifetime in that role (mirrors legacy's single `RakPeerInterface*`); `gfcNetworkManager`'s `isServer` flag (`gfcNetworkManager.h:136`) is the one place that decides which role (if either) is active, and nothing constructs a second client or server concurrently. Don't add a second concurrent host/client without revisiting that invariant.

## 32. Versioned wire format (JEF-23)

**Files:** `src/gfcWire.h` (primitives, header-only), `src/gfcWireMessages.{h,cpp}` (message codecs + `--wire-test`), `src/gfcNetworkStructures.cpp` (`serializeFX/unserializeFX/serializeLUT/unserializeLUT` ported to `Writer&/Reader&`), `src/gfcRakNetTransport.cpp` (envelope), `src/gfcnetworkclient.cpp`, `src/gfcnetworkserver.cpp`.

**Frame layout:** `[u8 version=1][u16 msgType, little-endian][payload...]`. `msgType` reuses the existing `GFCNETID_*` enum values — no separate message-id space. `readFrameHeader` rejects any version byte != 1.

**`jefe::wire::Writer`/`Reader`:** bounds-checked, explicit little-endian (byte-wise shifts, never memcpy of a multi-byte value — the one sanctioned memcpy is float/double→same-width-uint bit-pattern conversion before the int is written byte-by-byte). Strings/bytes are u32-length-prefixed; `Writer::writeBytes` refuses (and sticky-fails `ok()`) a payload ≥ 4GB rather than truncate the prefix — message payloads here are nowhere near that, the guard just makes the failure mode a clean refusal. `Reader` failure is sticky: one out-of-bounds/truncated read (or `markBad()`, used by `readFrameHeader` on a version mismatch) latches `ok()==false` and all further reads short-circuit to `false` without touching the buffer — callers that check `ok()` once at the end can't accidentally misparse trailing bytes after a bad field. Send sites must check `w.ok()` before handing `data()/size()` to the transport.

**Envelope ownership is the transport's job, not the app's.** `RakNetTransport::send` prepends one byte (`GFCNET_USER_PACKET_BASE == 91`) so RakNet's `ID_USER_PACKET_ENUM` routing sees it as user data; `poll()` strips that one byte before handing `ev.bytes` (a self-contained frame) to the app. Frames are otherwise transport-agnostic — **a future `WebRtcTransport` (JEF-24) must NOT add an envelope byte of its own**; it hands `jefe::wire` frames to/from its channel as-is. Because frames are self-contained, the server's 12-label verbatim-forward group needs no reserialization — it forwards `ev.bytes` untouched, frame header included.

**Codec TU + tests.** `gfcWireMessages.{h,cpp}` holds `encodeX(Writer&, const T&)`/`decodeX(Reader&, T&) -> bool` for every struct-based message (play/pause, pointer, remote pointer, transformation/CC vectors, other-states, FX add/common/attrib, FX-stack message, layer-change, chat entry, playlist string/item/event) plus the FX/LUT serialize helpers moved into `gfcNetworkStructures.cpp`. `--wire-test` runs 106 assertions covering primitive round-trips (boundary values, truncated-buffer reads, version-mismatch rejection) and per-message round-trips, including golden little-endian byte comparisons for the frame header and a few primitives — a value silently flipping to big-endian on some future platform would fail those, not just the round-trip.

**Version-mismatch = sticky reader fail = silent drop.** There is no fallback parser for an unrecognized version byte; `readFrameHeader` returns `false` and the pump's `if (!readFrameHeader(...)) break;` just drops the packet. This is deliberate — the alternative (attempting to parse an unknown layout) risks misreading attacker- or corruption-supplied bytes as valid fields.

**Sanctioned wire break.** `StringCompressor`'s Huffman coding and RakNet's per-field max-length truncation are gone — strings are raw UTF-8 bytes with a u32 length prefix, no length cap. Old and new builds cannot talk to each other; this is intentional (see plan `2026-07-21-jef23-versioned-wire-format.md`), not a bug. Version starts at 1 precisely so a future break has somewhere to go.

**Load-bearing quirk — REQUESTEDFXS/REQUESTEDLUTS "requested count" loop.** The server's `GFCNETID_REQUESTEDFXS`/`GFCNETID_REQUESTEDLUTS` handlers read a `howMany` count, then loop `for (i=0;i<howMany;i++) { if (!unserializeFX(r)) break; }`. `howMany` is the *requested* count, not a promise the payload actually contains that many — the sender only serializes the items it actually had loaded, so running out of payload mid-loop before `howMany` iterations is a **normal** condition, not corruption. Do not "fix" this into a hard failure on early exhaustion; it's carried over verbatim from the legacy `BitStream` code and both FX and LUT handlers rely on the same semantics.

**Decode-failure policy.** A `decodeX` returning `false` (malformed/truncated frame) is handled by skipping that message silently — no partial application of a half-decoded struct, no crash, no user-visible error. This only fires on a genuinely corrupt or adversarial packet in normal operation; legitimate peers running the matching version never produce one.

**`serializeLUT` fopen guard (adjudicated KEEP).** Legacy crashed on an unreadable `theLUT->filename` (unchecked `fopen`). The ported version checks the `FILE*` and, on failure, logs (`printf("serializeLUT: could not open %s\n", ...)`) and emits an empty-but-well-formed payload instead of dereferencing null — a crash-to-graceful-degradation fix that was in scope for this port (not a "don't fix pre-existing bugs" violation) because it's purely defensive and doesn't change any success-path behavior or wire layout.

## 33. WebRTC transport (JEF-24)

**Files:** `src/gfcTransportFactory.{h,cpp}` (selection), `src/gfcSignaling.{h,cpp}` (WebSocket signaling stub), `src/gfcWebRtcTransport.{h,cpp}` (the `ITransport` impl), `src/main_qt.cpp` (`--remote-test-webrtc` harness), `src/qt/SequenceLoadBridge_qt.cpp` (test bridge). CMake `option(JEFECHECK_WEBRTC ON)` pulls libdatachannel v0.22.0 via FetchContent.

**Factory + `JEFECHECK_TRANSPORT`.** `jefe::net::makeTransport(kind = transportKindFromEnv())` returns a `RakNetTransport` or `WebRtcTransport`. `transportKindFromEnv()` reads env `JEFECHECK_TRANSPORT` — **case-sensitive exact `"webrtc"`** selects WebRTC; anything else (incl. unset, `"RakNet"`, `"WebRTC"`) is RakNet. RakNet is the default → zero regression. Both `gfcNetworkClient` and `gfcNetworkServer` build their `transport_` via the factory in their **constructors**.

**Env is read at static-init time — the harness re-execs.** `networkManager` is a global; its `client`/`server` members construct their transports during static initialization, **before `main()` runs**. So a `qputenv()` inside `main()` is too late for the host process (its transports are already RakNet). The `--remote-test-webrtc` orchestrator therefore `setenv("JEFECHECK_TRANSPORT","webrtc",1)` + `execv(argv[0], argv)` at the very top of `main()` (guarded on the var not already being `"webrtc"`, so it fires once): the fresh process image re-runs static init with the var set, so both client and server pick WebRTC. The spawned peer child already inherits the var at spawn (`QProcessEnvironment` + parent env), so it never re-execs. `#ifndef _WIN32` (execv/setenv are POSIX; the harness is dev/macOS-only).

**Star topology over data channels.** Client = **offerer**: `createDataChannel("jefe")` (reliable+ordered, RakNet RELIABLE_ORDERED parity) which triggers the offer; on channel open pushes `ConnectAccepted`. Host = **answerer**: never creates a channel, captures it via `pc->onDataChannel` and assigns a fresh `PeerId`. One `rtc::PeerConnection` + one `rtc::DataChannel` per client, mirroring the RakNet hub. Empty `iceServers` (LAN host candidates suffice; STUN/TURN is JEF-26).

**Callback → mutex-guarded queue → poll threading.** All libdatachannel / signaling callbacks fire on background threads; every one pushes a `TransportEvent` into a `std::mutex`-guarded `std::deque` that the existing 4 ms `poll()` drains, so the manager stays single-threaded. One mutex guards the peer maps AND the queue; `dc->send()` is done **outside** the lock (shared_ptrs copied out first).

**Synthetic PeerIds.** Monotonic `std::atomic` counter from 1 (`kInvalidPeerId==0`). Client keys its single host-peer under the fixed `kHostPeerId=1`. Safe because JEF-22 confirmed PeerId is opaque everywhere above the transport (RakNet's `ip<<16|port` packing was RakNet-specific and is not reproduced).

**No envelope byte.** Unlike `RakNetTransport` (§32, which prepends `GFCNET_USER_PACKET_BASE==91` for `ID_USER_PACKET_ENUM` routing), WebRTC frames are self-contained: `send()` transmits the `jefe::wire` frame verbatim, `poll()` delivers received bytes verbatim as `Data`.

**`send()` contract (RakNet parity).** `broadcastExcluding=false` → unicast to `target` only; `target==kInvalidPeerId` → **nobody** (never a broadcast). `broadcastExcluding=true` → everyone except `target`; `target==kInvalidPeerId` → everyone. Only `open` channels are addressed — a pre-open send silently drops (matches RakNet).

**Signaling stub schema.** `SignalingServer` wraps `rtc::WebSocketServer` on the host port; `SignalingClient` dials `ws://ip:port`. Flat-JSON `SignalMessage`: `{ "type":"offer"|"answer"|"candidate"|"hello", "sdp"/"candidate"/"mid":…, "peer":<int> }`, hand-rolled encoder/parser (no JSON dep), defensive (never throws). This LAN-only WebSocket rendezvous is a stub: **JEF-25 replaces it with the cloud coordinator**; NAT traversal (STUN/TURN) is JEF-26.

**Lifecycle segfault gotcha (spike-verified).** The process segfaults at exit if a `PeerConnection` is created without `rtc::Preload()` at startup and `rtc::Cleanup()` at shutdown. `WebRtcTransport` calls `rtc::Preload()` in its ctor and `rtc::Cleanup()` in its dtor (**after** all rtc objects are destroyed — `teardownAll()` then `d_.reset()` then `Cleanup()`); `rtc::InitLogger(LogLevel::Error)` runs once via `std::call_once`. Preload/Cleanup are refcounted, so two transports in one process (see loopback below) are fine.

**CMake.** `set(CMAKE_POLICY_VERSION_MINIMUM 3.5)` before the FetchContent block (old `plog` submodule vs CMake 4.x), `if(APPLE) set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl@3)`, libdatachannel `v0.22.0 GIT_SHALLOW` with examples/tests/media OFF, `target_compile_definitions(jefecheck PRIVATE JEFECHECK_WEBRTC=1)`. When the option is OFF the transport TU compiles to a stub and the factory falls back to RakNet.

**`clientActive` connect/disconnect guard.** The client-session flag (mtx-protected) is set true in `connect()` **before** the async dial and flipped false in `disconnect()`/`teardownClient()`. The WS-thread callbacks (`onClientSignalingOpen` still constructing a pc/dc) check it under the lock and bail/drop their orphan if a `connect()`-then-immediate-`disconnect()` raced them, so a live pc/dc can't be published into an abandoned session.

**`--remote-test-webrtc` harness + the one-shot-play race.** Two-process LAN proof: orchestrator hosts, spawns `--remote-test-webrtc-peer <ip> <port>`, asserts `participants>=1 && mirrored_play==1`, exits 0/2. The host mirrors playback through its **own loopback client** (`gfcNetworkManager::startServer` connects a 127.0.0.1 client to the host's own server; the peer's play is *forwarded* to that loopback, which applies it, and `isPlaying()` reads the shared `playbackManager`). Two subtleties WebRTC exposes that RakNet's instant connect hid: (1) the peer's play is **one-shot** (sent once inside `togglePlayFwd`), so the peer must wait until it is fully connected before toggling — `remoteTestPeerConnect`'s `connectTimeoutMs` is bumped to 9 s for the WebRTC path (a toggle before the channel opens would be dropped and never re-sent); (2) a WebRTC loopback can open its channel **later** than a fast remote peer, so if the peer played first the forward would reach 0 channels and be lost — the orchestrator is **split-phase** (`remoteTestServerStart` brings the loopback fully up — its nickname registered == channel open both ways — **before** the peer is even spawned, then `remoteTestServerSettleForPlay` waits for the mirror). Budgets: loopback-await 10 s, settle 10 s, peer hold 6 s. Data flows over a **DTLS-encrypted SCTP data channel** (structural — libdatachannel mandates DTLS). Set `JEFECHECK_REMOTE_TEST_DEBUG=1` for gated state-transition tracing (`[webrtc:host|client] …` signaling/offer/answer/pc-state/datachannel-open) and to forward the child's stdio.

**Carried-over JEF-24 hardening TODOs (from the JEF-23 review).** The wire layer is bounds-safe for *shape*, but these remain: ~~**path traversal** in `unserializeFX`/`unserializeLUT`~~ **RESOLVED in JEF-28 (§35)** — both now reduce every peer-supplied filename to its `std::filesystem::path(...).filename()` basename before touching disk; **wire-index validation** (received quad/track/FX indices are trusted, not range-checked against the local managers) — still open; **envelope/frame-id check** (no per-peer replay/spoof guard on message type vs sender role) — still open; **`ftell` guard** in the LUT/DPX read paths (a negative/failed `ftell` isn't checked before use) — still open. None of the remaining items matter on a trusted LAN (this ticket's scope); all must be closed before the JEF-25 cloud coordinator ever carries a session across the public internet.

**Two more internet-exposure gaps specific to `WebRtcTransport` (from the JEF-24 whole-branch review — also BLOCK internet exposure).** (1) **Session password is dropped:** `WebRtcTransport::startHost`/`connect` ignore the `password` param — `maxClients` is the only join gate, so any peer that reaches the signaling port joins unauthenticated (RakNet enforced the password). JEF-25 must enforce it in the signaling handshake. (2) **Signaling WebSocket binds to the wildcard address (0.0.0.0), unauthenticated** — required for LAN reachability, but it's an open rendezvous on all interfaces. Data channels are DTLS-encrypted end-to-end, but signaling itself is not gated; JEF-25's coordinator must add auth (and ideally bind-scope) before this crosses the public internet.

**Unverified.** The Ubuntu/MinGW CI legs for the libdatachannel FetchContent build were not runnable on this macOS host — the reviewer/controller must confirm them green.

## 34. Cloud coordinator client (JEF-27)

**Files:** `src/gfcCoordinatorSignaling.{h,cpp}` (JEF-25 signaling client + envelope codec), `src/gfcWebRtcTransport.{h,cpp}` (coordinator mode host/joiner), `src/gfcTransportFactory.{h,cpp}` (`TransportConfig` selection), `src/gfcNetworkStructures.h` (params fields), `src/gfcnetworkserver.*`/`gfcnetworkclient.cpp` (plumbing), `src/qt/RemotePanel_qt.{h,cpp}` (Cloud UI), `src/qt/SequenceLoadBridge_qt.{h,cpp}` (off-thread connect + getters), `src/gfcTestCoordinator.{h,cpp}` + `src/main_qt.cpp` (`--coord-test*`). Builds on JEF-24 (§33): coordinator mode reuses ALL the PeerConnection/DataChannel/event-queue/PeerId machinery — only the signaling backend + config plumbing are new.

**Coordinator protocol (the CONTRACT).** The client speaks the JEF-25 rendezvous protocol as JSON over one `rtc::WebSocket` (ws://host:port or wss://host). Canonical source: `~/projects/jefecheck-coordinator` (`docs/coordinator-protocol.md` + `src/protocol.ts`) — the C++ must match it verbatim; do NOT invent fields. Client→coord messages are tagged by `action`, coord→client by `type`:
- **Client→coord:** `{"action":"create-session"}` (host) · `{"action":"join-session","code":<str>}` (joiner) · `{"action":"signal","to":<connId>,"payload":<obj>}` · `{"action":"leave"}`.
- **Coord→client:** `{"type":"session-created","code","token","iceServers?"}` (host) · `{"type":"roster","peers":[connId…],"iceServers?"}` (joiner; `peers` EXCLUDES self) · `{"type":"peer-joined","peerId"}` · `{"type":"peer-left","peerId"}` · `{"type":"signal","from":<connId>,"payload":<obj>}` · `{"type":"error","code","message"}`.
- The signal `payload` is OPAQUE to the coordinator and nests the JEF-24 `SignalMessage` (§33) as a JSON object (`encodeSignalEnvelope` reuses `encodeSignal`; inbound re-parsed via `parseSignal`). Every peer id (`peerId`/`signal.to`/`signal.from`/roster entries) is a connection-id **string** (`connId`) — hence `std::string` everywhere. The envelope codec (`encodeCreateSession`/`encodeJoinSession`/`encodeSignalEnvelope`/`encodeLeave` + `parseServerMsg`) is pure/unit-testable; `parseServerMsg` is defensive (never throws) and captures `iceServers` as an opaque raw-JSON array substring. Round-trip gate: `--coord-signal-test` (54 checks + a bounded loopback against a scripted `rtc::WebSocketServer`).

**Three transport modes + selection.** `jefe::net::makeTransport` picks between (1) **RakNet** (default, zero-regression), (2) **JEF-24 LAN WebRTC stub** (`SignalingServer`/`Client` on the host port, `JEFECHECK_TRANSPORT=webrtc`), (3) **cloud coordinator** (this ticket). Selection widened to `makeTransport(TransportConfig{kind, coordinatorUrl, coordinatorMode, sessionCode, password})` (old `makeTransport(kind)` still works). A non-empty `coordinatorUrl` implies `coordinatorMode` and forces `TransportKind::WebRtc`; env `JEFECHECK_COORDINATOR_URL` / `JEFECHECK_SESSION_CODE` seed the config when unset. `gfcServerParams`/`gfcConnectionParams` gained `coordinatorMode`/`coordinatorUrl`/`sessionCode` (additive — RakNet + LAN WebRTC construction unchanged); `startServer`/`startConnection` thread them through to `WebRtcTransport::configureCoordinator(url, sessionCode, password)` **before** `startHost`/`connect`. In coordinator mode `startHost` create-session's (host = answerer; each `peer-joined` → a new answerer PeerConnection), `connect` join-session's by code (joiner = offerer; on roster/peer-joined builds the offerer PeerConnection). Coordinator `connId`s map to transport `PeerId`s via `coordToPeer`.

**iceServers (JEF-26 TURN) consumption.** The coordinator's optional `iceServers` (STUN/TURN with `urls`/`username?`/`credential?`) arrive as an opaque raw-JSON substring on `session-created`/`roster`, are stashed in `coordIceServersJson`, and fed into `rtc::Configuration` at BOTH PeerConnection build sites via `makeConfig()` (`parseIceServers`). Empty list ⇒ the JEF-24 LAN host-candidates-only behavior, unchanged. `--coord-test` returns a dummy STUN list to prove the plumbing without exercising a real relay.

**Session-code UX (Qt).** `RemoteDialog_Qt` (`RemotePanel_qt`) gets a third **Cloud** segment (Host / Cloud / Join): a shared coordinator-URL field (defaults to `JEFECHECK_COORDINATOR_URL`), "Create session" (on success shows the assigned **code** in a copy-able banner) and "Join by code". The two bridge entry points `jefe::qt::connectAsCloudHost(RemoteCloudHostParams)` / `connectAsCloudClient(RemoteCloudJoinParams)` funnel into `gfcNetworkManager::startServer`/`startConnection` with coordinator params; `jefe::qt::remoteSessionCode()` surfaces the code. **The cloud connect runs OFF the UI thread** (it blocks ~5s waiting for the coordinator) and sets `gCloudConnectInFlight` — a global atomic that **gates ALL GUI-thread manager reads** (`remoteSessionCode`/`remoteErrors`/participant/status getters AND `drawNetworkOverlay`) so the worker thread owns `networkManager` exclusively during bring-up; every gated getter returns empty/false while it is set. Object names follow the dotted-leaf locator scheme.

**Reconnect / backoff + the rendezvous-only-post-connect principle (design spec §5).** The coordinator is a rendezvous needed for **JOIN and RECONNECT only**. Once peers hold a P2P DataChannel, a coordinator drop must NOT kill the established session — the P2P PeerConnections live in `WebRtcTransport` and continue independently. `CoordinatorSignaling` owns ONLY the coordinator WebSocket: on an UNEXPECTED `onClosed` (no deliberate `close()`/`leave()`) it runs a bounded exponential-backoff reconnect loop on its own thread (1s/2s/4s… capped 30s, `kMaxReconnectAttempts=6`, then `onError("reconnect-failed",…)`), re-dialing the same URL. Thread-safety: an `intentionalClose` atomic (set by `close()`/`leave()` BEFORE the mtx-guarded thread move, re-checked in `scheduleReconnect()` under the same lock) means a deliberate close cancels/skips reconnect and no loop is ever orphaned; a `condition_variable` wakes a sleeping loop immediately on close; `reconnectStatus()` exposes the state. **The transport deliberately does NOT wire `coord->onClosed`** — audit: coordinator-drop never reaches the PeerConnection teardown (`teardownServer`/`teardownClient`/`dc->onClosed` are the only teardown paths), so P2P survives. **Phase-1 limitation:** on reconnect success the signaling channel is restored but session MEMBERSHIP is **not** resumed (the protocol has no resume/re-announce), so reconnect deliberately does NOT re-fire `onOpen` (which would create a NEW session with a fresh code / re-join) — it restores the socket for future signal relay and logs. *Verification:* `--coord-test` establishes the P2P session and asserts `mirrored_play==1`; killing the test coordinator afterward leaves the mirrored session intact (the reconnect loop merely retries the WS in the background, bounded, never touching the PCs).

**`--coord-test` two-process E2E + the co-located-coordinator libdatachannel bug.** The test-double coordinator (`jefe::net::TestCoordinator`, a `rtc::WebSocketServer` implementing the JEF-25 contract, field-matched to the coordinator repo) MUST run in its **own process** (`--coord-test-server`, which prints `COORD-URL=ws://127.0.0.1:<port>/`): libdatachannel misroutes/drops inbound messages when a `WebSocketServer` and multiple client `rtc::WebSocket`s (the host's coordinator socket + its loopback client's) share one process. The `--coord-test` orchestrator spawns that server process, brings the host up in coordinator mode (create-session → code), spawns `--coord-test-peer <coordUrl> <code>` (joins by code), and asserts `participants>=1 && mirrored_play==1` (exit 0/2). Reuses the JEF-24 `--remote-test-webrtc` re-exec (`JEFECHECK_TRANSPORT=webrtc` before static init) + split-phase loopback pattern, swapping ip/port for coordinatorUrl + code. **Escape hatch:** set `JEFECHECK_COORDINATOR_URL` (+ `JEFECHECK_SESSION_CODE`) to run the real client against the actual Node coordinator instead of the test double.

**Still-open items.** (1) **Real cloud deploy is the operator's step (JEF-25/26)** — verified locally only; no live wss:// endpoint is provisioned here. (2) **Password is NOT enforced** — the phase-1 coordinator gates by code/token, not a password field; `configureCoordinator` carries `password` but the protocol has no slot for it, so **the session code IS the gate** (residual: anyone with the code joins; document, don't fake enforcement). (3) The **co-located-coordinator libdatachannel bug** (above) is worked around by process isolation, not fixed — it mirrors real deployment (remote coordinator) so it isn't a blocker. (4) The JEF-24 internet-exposure hardening TODOs (§33: wire-index validation, replay/spoof guard, unauthenticated signaling — path traversal was closed in JEF-28, §35) STILL BLOCK public-internet exposure and must be closed before any session crosses it.

## 35. P2P asset transfer (JEF-28)

**Files:** `src/gfcStructures.{h,cpp}` (`jefe::contentHash*`), `src/gfcfx.cpp`, `src/trilerp.cpp` (FX/LUT hashing call sites), `src/gfcNetworkStructures.cpp` (`unserializeFX`/`unserializeLUT` basename sanitization), `src/main_qt.cpp` (`sett.receivedPath` assignment, `--asset-test`/`--asset-test-webrtc` harnesses), `src/gfcTransport.h` (`Channel` enum), `src/gfcRakNetTransport.{h,cpp}`, `src/gfcWebRtcTransport.{h,cpp}` (assets channel + chunking), `src/gfcnetworkclient.cpp`/`gfcnetworkserver.cpp` (Assets-channel send sites), `src/qt/offscreen_gl_qt.cpp` (headless GL bring-up for `--asset-test`).

**Scope correction up front.** This ticket HARDENS the existing sync state machine (§31/§32), it did not rebuild it. The late-join host→joiner LUT/FX push (`startFXSinc`/`startLUTSinc`/MISSINGFXS/MISSINGLUTS/REQUESTEDFXS/REQUESTEDLUTS, §31) already existed and was already correct — `--asset-test` is the first thing to actually VERIFY it end-to-end with a real fixture LUT+FX and a real late-joining peer (RakNet transport, headless offscreen GL so the joiner's loaders can create real GL objects — see `offscreen_gl_qt.cpp`). Everything below is either a genuine bug fix underneath that machinery or new QoS/transport plumbing carrying the same messages.

**Portable content digest (`jefe::contentHash*`, `gfcStructures.h`).** `GetMD5Hash` (still used elsewhere) is `std::hash` over a *string* — implementation-defined per build/platform, and for LUTs it hashed *parsed sample values*, not file bytes; for FX it hashed metadata only (name/params), never the GLSL source. Both are wrong for cross-peer dedup: two peers on different compilers/platforms could compute different digests for byte-identical files, and two FX with identical metadata but different shader code collided. Fix: `jefe::contentHash(data,len)` is a fixed FNV-1a 64-bit over raw bytes (not cryptographic — dedup only), rendered as 16 hex chars; `contentHashFile(path)` streams a file through it in 64KB chunks (never materializes the whole file); `contentHashFiles(paths)` feeds several files' bytes through one running hash in order. LUT loading (`trilerp.cpp`, all three `CubeLUT::load` cube/1D-cases) now hashes the raw `.cube`/`.1dlut` bytes via `contentHashFile`. FX loading (`gfcfx.cpp`) now hashes `{ .jfx, .vert, .frag }` bytes concatenated via `contentHashFiles` — this is the fix that closes the metadata-only collision, since the digest now covers the actual shader source. Both call sites fall back to the old `GetMD5Hash` only if the content hash comes back empty (file unreadable at hash time) — a defensive fallback, not the normal path.

**`sett.receivedPath` (`main_qt.cpp`).** Was declared in `gfcSettings` but never assigned in the Qt build, so `unserializeFX`/`unserializeLUT` wrote received assets into the process's CWD. Now set once at startup to `getApplicationDataPath()+"received/"` (created via `std::filesystem::create_directories`), after `setMacExecutablePath` primes `getApplicationDataPath()` on macOS and before any `--remote-test`/`--coord-test`/`--asset-test` dispatch.

**Path-traversal fix (`gfcNetworkStructures.cpp`) — resolves the §32/§33 open TODO.** `unserializeFX`/`unserializeLUT` write files whose *names* are peer-supplied over the wire. Previously the raw name (with any `../../` or absolute path a hostile peer chose to send) was concatenated onto `receivedPath` and opened for write — a peer could escape the intended directory. Fix: every filename is reduced to `std::filesystem::path(name).filename().string()` before use — `filename()` strips all directory components and `..` segments (`"../../evil"` → `"evil"`, `"/etc/passwd"` → `"passwd"`). If sanitizing any required filename yields an empty string, the function logs and returns `false` (no write, no partial FX). §33's "carried-over JEF-24 hardening TODOs" and §34's "still-open items" list have been updated to mark path traversal RESOLVED — wire-index validation, the replay/spoof guard, and unauthenticated signaling remain open and still block public-internet exposure.

**Dedicated "assets" QoS lane.** `ITransport::send` gained a defaulted `Channel channel = Channel::State` parameter (`gfcTransport.h`) — every pre-existing call site is source-compatible and unchanged behavior. `Channel::Assets` exists so a large LUT/FX transfer can't head-of-line-block live state/chat/pointer/play/CC traffic sharing the same reliable-ordered stream. Only the four bulk-body send sites use it: server-side `MISSINGFXS`/`MISSINGLUTS` responses (`gfcnetworkserver.cpp`) and client-side `REQUESTEDFXS`/`REQUESTEDLUTS` responses (`gfcnetworkclient.cpp`) — i.e. the messages that actually carry serialized FX/LUT bodies. All handshake/sync-signaling messages (the `howMany`/hash-list requests, `FXSINCCOMPLETE`, etc.) stay on `Channel::State` — no reordering risk between "here's what I'm missing" and "here it is."
- **RakNet:** `Channel::Assets` maps to RakNet ordering channel 1 (`gfcRakNetTransport.cpp`); `Channel::State` stays channel 0. Receive-side channel tagging is best-effort — RakNet doesn't cheaply return which ordering channel a packet arrived on, so `TransportEvent.channel` is left `State` for all RakNet events. Harmless: dispatch is by `GFCNETID` regardless of channel.
- **WebRTC:** the offerer opens a SECOND `rtc::DataChannel` per peer, `"assets"` (reliable/ordered, alongside the existing `"jefe"` = State channel). The answerer's `onDataChannel` branches on `dc->label()`. `PeerConnected`/`ConnectAccepted` still fire only on the State channel opening; the assets channel opens silently in the background. `send(..., Channel::Assets)` routes to the peer's `assetsDc`.

**Chunking + backpressure (WebRTC assets channel only, `gfcWebRtcTransport.cpp`).** SCTP data channels have a per-message limit (only 64KB guaranteed unless the peer advertises a larger `max-message-size`), and the sync layer hands the transport whole serialized LUT/FX bodies in one `send()` call — those can be multi-MB. So `Channel::Assets` sends are split at the transport level into ordered chunks; the sync layer stays oblivious, and the receive side reassembles into one `Data(Channel::Assets)` event. This framing is assets-channel-only transport framing — it is NOT the app's `jefe::wire` frame format (§32) and never carries the RakNet envelope byte; the reassembled payload IS the `jefe::wire` frame verbatim.
- **Chunk header** (24 bytes, little-endian, prepended to every assets-channel wire frame — single-chunk messages use `chunkCount==1` so the receive path is uniform): magic `'A''C'`, version (1), flags (reserved 0), `messageId` (u32, per-peer monotonic from 1), `chunkIndex` (u32), `chunkCount` (u32), `offset` (u32, this chunk's byte offset in the reassembled payload), `totalLen` (u32). `offset` makes reassembly independent of the sender's chunk size, so a sender can clamp chunk size to the peer's negotiated `max-message-size` without a wire renegotiation.
- **Thresholds:** 60KB payload per chunk (frame stays under the 64KB guaranteed limit), 1MB high-water mark (TX pauses `dc->send()` once `bufferedAmount()` reaches it), resumes via `onBufferedAmountLow` once SCTP drains below a 256KB low threshold, 128MB reassembly cap on the RX side (anti-abuse — a `totalLen` above this is rejected outright to bound memory).
- **TX queue** is per-peer (`assetTxQueue`, `assetTxMsgId`), drained by `drainAssetQueue` with `assetTxDraining` serializing the app thread (`send()`) against the rtc callback thread (`onBufferedAmountLow`) so ordered chunks stay strictly in order; `dc->send()` itself runs outside the peer-map mutex (shared_ptr copied out first — same pattern as the rest of the WebRTC transport, §33).
- **RX reassembly** is keyed by `(peer, messageId)` — since SCTP is reliable+ordered, at most one message is in flight per peer at a time; `assetRxLastComplete` rejects stale/duplicate chunks. Guards: header too short, bad magic/version, `count==0`, `idx>=count`, an absurd `chunkCount` relative to `totalLen`, or a chunk slice (`offset+payLen`) that would run past `totalLen` are all rejected without touching the reassembly buffer — malformed/hostile input can't crash the receiver or corrupt another message's buffer.

**Harnesses.** `--asset-test` (RakNet, Task 2): orchestrator loads a fixture LUT+FX, hosts, spawns `--asset-test-peer` which joins late and must receive+hot-load both via the existing late-join push; asserts `lut=1 fx=1`. `--asset-test-webrtc` (Task 4): the WebRTC variant, drives a multi-MB `.cube` LUT specifically to exercise chunking (41 chunks at the 2.46MB fixture used in review) and asserts both delivery AND a post-transfer content-hash match (`hashmatch=1`) between host and joiner — proving reassembly reproduces the exact bytes, not just "some data arrived."

**Still-open / future.** Media-file transfer (the actual video/image sequences, as opposed to LUT/FX sidecar assets) is out of scope — future work. Resume-after-drop is not implemented — a dropped assets-channel transfer restarts from scratch on reconnect, there is no partial-resume protocol. The reassembly cap is per connected peer (~256MB worst case per in-flight message — the 128MB `totalLen` buffer plus the up-to-128MB per-chunk-received tracking vector), so memory scales with peer count on a busy host — acceptable on a trusted LAN (this ticket's scope) but worth revisiting (and tightening the per-message bound) before internet-facing hardening. The backpressure pause branch (`bufferedAmount >= kAssetHighWater`) is exercised by the 2.46MB fixture but not stress-tested against a slow/lossy link — loopback never gets close to real-world congestion. `getApplicationDataPath()/received/` may land under a read-only app-bundle `Resources` directory in a hardened/signed install (macOS Gatekeeper, etc.) — a packaging follow-up to point it at a proper per-user writable location (e.g. Application Support) before shipping a signed release.

## See also

- `CLAUDE.md` — project conventions, build setup, platform-specific gotchas.
- `docs/superpowers/specs/` — design specs for major features (Qt load window, etc.).
- `docs/superpowers/plans/` — implementation plans matching those specs.
