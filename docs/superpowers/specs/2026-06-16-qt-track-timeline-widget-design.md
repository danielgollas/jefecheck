# Qt Track Timeline Widget — Design Spec

**Status:** Approved (2026-06-16)
**Branch target:** `qt-experimental`
**Backlog item:** #4 (per-track timeline display) from `project_qt_plate_polish`

## Goal

Replace the stub `TimelineTracks_Qt` (4 fixed lanes with placeholder text) with a real, interactive per-track timeline that shows each track's frame range, offset, and loaded-vs-unloaded fill, and supports the FLTK `TrackWidget` interactions: drag-to-offset, alt-click-to-load-from-frame, drag-drop-to-load, and a right-click "more options" popup (numeric offset entry + hold-frame mode).

## Background / current state

- The current `TimelineTracks_Qt` (`src/qt/TimelinePanel_qt.{h,cpp}`) paints 4 equal lanes with "Track A/B/C/D" text and no live data. It sits below `TimelineScrubber_Qt` in the timeline dock's `QVBoxLayout` (stretch=1), so it shares the scrubber's horizontal extent.
- The original FLTK `TrackWidget` (git history, commit `9a1c605`: `src/trackwidget.{h,cpp}`) was one widget per track. It drew a background bar (sequence range within the visible timeline), a loaded-frames progress fill, and a filename label. Its `handle()` only fired a callback; the real actions lived in `tracksBarCB` (`UICallbacks.cpp` @ `9a1c605`):
  - Left-drag → accumulate pixels; when the accumulation exceeds one GUI frame-width, `setOffset(getOffset() ± 1)`.
  - Alt + left-click → `startLoadingSequenceAt(track, getClickedFrame())`.
  - Right-click → "more options" popup with a numeric frame-offset input and a hold-frame toggle.
  - File drop (FL_PASTE) → load the dropped sequence into the track.
- **Data model (current code, still present):**
  - `gfcSequence`: `getRangeStart()`/`getRangeEnd()` (offset baked in), `getOffset()`/`setOffset(int)`, `getNumFrames()`, `isEmpty()`, `trackID` (char `A`..`D`), `getHoldMode()`/`setHoldMode(int holdMode, int holdFrame=0)`, per-frame `gfcFrame.loaded`, the `loadedFrames` vector, and `hasPendingRawFrames()`.
  - `gfcTrackManager`: `getSequence(int)`, `startLoadingSequenceAt(int track, int startFrame)` (kicks the loader thread; texture upload happens later in the playback tick — **no GL context needed at the call site**), `getMaxTrackLength()`.
  - `gfcPlaybackManager`: global timeline `[from, to]` (= `[1, maxTrackLength]`), `getFromFrame()`/`getToFrame()`. All 4 tracks share this one range; per-track offset is internal to the sequence and is NOT reflected as a separate global range.
- **TU separation rule (developer_notes.md §1):** Qt UI `.cpp` files must NOT include the rendering-chain managers (`gfcSequence.h`, `gfctrackmanager.h`, etc.) — glad and Qt's QOpenGLWidget refuse to share a TU on macOS. Only `src/qt/SequenceLoadBridge_qt.cpp` includes them; everything else routes through `jefe::qt::*` accessors in `SequenceLoadBridge_qt.h`. The track widget therefore reaches all sequence/track state through new bridge accessors.

## Decisions (from brainstorming)

1. **Always render 4 rows (A–D).** Empty tracks render as faint placeholder lanes that double as drop / alt-click-load targets. Consistent layout; no row reflow as tracks load.
2. **Full FLTK interaction parity in v1**, including the right-click popup.
3. **Rows do not scrub.** Because left-drag is reassigned to offset, current-time scrubbing stays on `TimelineScrubber_Qt` above. The rows draw a read-only playhead line for alignment only.
4. **Loaded fill is contiguous (progress-style).** Loading proceeds sequentially from a start frame, so the decoded region is modeled as a contiguous run `[firstLoadedFrame, firstLoadedFrame + loadedCount)` in timeline-frame coordinates.

## Architecture (Approach A: one multi-row widget + QMenu)

`TimelineTracks_Qt` is fleshed out into the real widget. It owns painting, hit-testing, the drag-offset state machine, alt-click load, file drops, and the right-click `QMenu`. It shares the scrubber's `[from,to]`→pixel mapping so rows stay pixel-aligned beneath the scrubber. New `jefe::qt::*` bridge accessors carry per-track state and mutations.

### Coordinate mapping

- `xFromFrame(frame)` and `frameFromX(x)` map between widget pixels and the global `[from,to]` range, identical in form to `TimelineScrubber_Qt`'s. Factor the shared mapping into small helpers (free functions or a tiny shared struct) used by both, rather than duplicating logic.
- Frame width in pixels = `widgetWidth / (to − from + 1)`. The drag-offset threshold is one frame width.

### Painting (per row, lane height = widget height / 4)

- **Loaded track:** a bar spanning `[rangeStart, rangeEnd]` (offset already baked in) in dark gray; the decoded portion `[firstLoadedFrame, firstLoadedFrame + loadedCount)` filled in a lighter gray; the filename label drawn over the bar.
- **Empty track:** a faint placeholder lane with muted "drop or alt-click to load" text.
- **Playhead:** a thin vertical line at the current frame spanning all rows. Read-only.
- Colors follow the dark VFX theme; reuse the scrubber's palette where practical.

### Mouse / interaction

- **Left-drag (horizontal):** accumulate dx; each time `|accum|` exceeds one frame width, call `setTrackOffset(track, getTrackOffset(track) ± 1)` and subtract one frame width from the accumulator. The track under the initial press is the drag target for the whole gesture.
- **Alt + left-click:** `startLoadingTrackAt(track, frameFromX(clickX))`. On an empty track this is a no-op (nothing assigned to load).
- **Right-click:** `QMenu` with:
  - a checkable **"Hold last frame"** action reflecting / toggling `getTrackHoldMode`/`setTrackHoldMode`;
  - a **"Set offset…"** action opening `QInputDialog::getInt` seeded with `getTrackOffset(track)`, writing back via `setTrackOffset`.
- **Drag-drop a file** onto a row: accept the drop, resolve the row under the cursor, and load the dropped path into that track via the existing Qt load path (the same one used for viewport drops / Quick Load), targeted to the dropped row's track index.
- Every mutation routes through a bridge accessor that calls `plateManager.setChanged()` so the viewport repaints.

### Bridge surface (new `jefe::qt::*` in `SequenceLoadBridge_qt.{h,cpp}`)

```cpp
struct TrackTimelineState {
    bool present;          // a sequence is assigned to this track
    int  rangeStart;       // first timeline frame of the sequence (offset baked in)
    int  rangeEnd;         // last timeline frame of the sequence (offset baked in)
    int  offset;           // current frame offset
    int  numFrames;        // total frames in the sequence
    int  firstLoadedFrame; // timeline frame of the first decoded frame
    int  loadedCount;      // count of contiguous decoded frames
    std::string label;     // filename for the row label
};

TrackTimelineState getTrackTimelineState(int track);   // read for paint
int  getTrackOffset(int track);
void setTrackOffset(int track, int offset);            // calls plateManager.setChanged()
void startLoadingTrackAt(int track, int frame);        // kicks loader thread
bool getTrackHoldMode(int track);
void setTrackHoldMode(int track, bool hold);           // calls plateManager.setChanged()
```

- `getFromFrame()` / `getToFrame()` already exist for the global range.
- `getTrackTimelineState` computes `firstLoadedFrame` / `loadedCount` as the contiguous decoded run, expressed in timeline-frame coordinates (range + offset). `present` is `!isEmpty()`. `label` is the sequence's generic filename basename (reuse the same source as `getLoadedSequenceName`).
- Hold-mode int↔bool translation lives at the bridge boundary: `getTrackHoldMode` returns `getHoldMode() != 0`; `setTrackHoldMode(true)` calls `setHoldMode(1)` and `(false)` calls `setHoldMode(0)`. (`gfcSequence::getHoldMode()` is `int`; the widget only needs the on/off state.)
- File drop reuses the existing load entry point; if a track-targeted variant isn't already exposed, add a thin `loadFileIntoTrack(int track, const std::string& path)` bridge accessor rather than including managers in the widget.

### Data flow / refresh

- The existing ~60Hz `TimelinePanel_Qt::refreshFromPlayback` tick (driven by `MainWindow_qt.cpp`'s playback timer, already throttled to ~60Hz) also pulls the 4 `TrackTimelineState`s.
- Cache the last-seen state per track (same pattern as `TimelinePanel_Qt`'s `last*` fields and the plate cards' cache-gated writes) and call `update()` only when something changed — e.g. `loadedCount` grows during decode, or an offset changes. This keeps the AppKit accessibility cascade cost down (developer_notes.md §5/§6).

## Out of scope (v1)

- **Timeline zoom / pan** (visible-range sub-windowing). Rows show the full global `[from,to]`, matching the scrubber. The FLTK `setVisibleRange` capability is deferred.
- **Per-plate multi-layer on a shared track** (backlog #8) — separate architectural effort.
- **Per-row independent playheads** — single global playhead only.

## Testing

- **Appium UI test** (`tests/ui/`): give the tracks widget and its rows stable object names (per the dotted-leaf scheme in `tests/ui/jefecheck/locators.py`). Assert: (1) a loaded track's row reports loaded state via the bridge; (2) alt-click triggers a load-from-frame (loaded extent starts at the clicked frame); (3) drag and the "Set offset…" popup change the bridge-visible offset; (4) the hold-frame toggle round-trips. Where pixel state is needed, use the existing GL pixel-diff harness (PR-26).
- **Manual verification loop:** `cmake --build build_qt -j`; relaunch; load sequences across tracks; verify bar extents, loaded fill growth during decode, offset drag, alt-click load, drop-to-load, and the right-click popup.

## Files

- **Modify:** `src/qt/TimelinePanel_qt.{h,cpp}` — flesh out `TimelineTracks_Qt` (paint, mouse, drop, QMenu, refresh + cache); share the x↔frame mapping with `TimelineScrubber_Qt`.
- **Modify:** `src/qt/SequenceLoadBridge_qt.{h,cpp}` — add `TrackTimelineState`, `getTrackTimelineState`, `getTrackOffset`/`setTrackOffset`, `startLoadingTrackAt`, `getTrackHoldMode`/`setTrackHoldMode`, and (if needed) `loadFileIntoTrack`.
- **Modify:** `tests/ui/jefecheck/locators.py` + new test file under `tests/ui/`.
- **Reference (read-only, from git `9a1c605`):** `src/trackwidget.cpp`, `UICallbacks.cpp::tracksBarCB`.
