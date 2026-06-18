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

## See also

- `CLAUDE.md` — project conventions, build setup, platform-specific gotchas.
- `docs/superpowers/specs/` — design specs for major features (Qt load window, etc.).
- `docs/superpowers/plans/` — implementation plans matching those specs.
