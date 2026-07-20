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

## See also

- `CLAUDE.md` — project conventions, build setup, platform-specific gotchas.
- `docs/superpowers/specs/` — design specs for major features (Qt load window, etc.).
- `docs/superpowers/plans/` — implementation plans matching those specs.
