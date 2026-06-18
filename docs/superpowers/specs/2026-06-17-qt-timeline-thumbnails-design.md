# Qt Timeline Frame Thumbnails — Design Spec

**Status:** Approved (2026-06-17)
**Branch target:** `qt-experimental`
**Backlog item:** #4b (follow-up to the track timeline widget, PR #97)

## Goal

Render a downsampled frame filmstrip along each track's timeline bar — actual decoded frames as thumbnails — with a thin green loaded-bar beneath, letterboxed, on by default with a show/hide toggle.

## Background / current state

- The track timeline widget (`TimelineTracks_Qt`, `src/qt/TimelinePanel_qt.{h,cpp}`, PR #97) paints 4 rows; loaded tracks draw a dark bar with a light-green loaded fill (`QColor(150,185,150)`) over `[rangeStart, rangeEnd]`. It reaches all sequence state through `jefe::qt::*` bridge accessors (TU separation — the widget must not include `gfcSequence.h`/glad).
- Frames decode on a loader thread into a per-sequence `rawFrames` queue; the playback tick drains it via `gfcTrackManager::generateTextures()` → `gfcSequence::generateTexture(...)`, which uploads each frame to a GL texture with `glTexImage2D`. **At that point the decoded CPU pixels are still available** as `bitmap[0]` — a `GFLC_BITMAP` (`src/gfcpixelbuffer.h`, accessors `getDataPtr()`, `getWidth()`, `getHeight()`, `getComponentsPerPixel()`), plus `totalW`/`totalH` and the frame's index. After upload the CPU buffer is released; `gfcFrame` keeps only the GL `textureID`. So thumbnails must be captured **at decode/upload time**, not by GL readback.
- Pixel order from the OIIO loader is BGRA (it swizzles RGB→BGRA for `GL_BGRA`); `getComponentsPerPixel()` is 4 for loaded frames.
- `gflResize` (`gfcpixelbuffer.h`) operates on the legacy `GFL_BITMAP`, not `GFLC_BITMAP`; its implementation is a nearest-neighbor resample. A small manual nearest-neighbor downsample over `getDataPtr()` is simpler than adapting it and avoids the type mismatch.
- `gfcSettings` (`sett`, `src/gfcStructures.h`) holds runtime flags (e.g. `defaultTextureFormat`, `defaultDecodeFilter`) — the right home for a `showThumbnails` flag the bridge exposes.

## Decisions (from brainstorming)

1. **Thumbnails + a thin green loaded bar.** The lane splits into a thumbnail strip (top) and a thin green loaded-bar (bottom). The bar keeps the explicit loaded-extent indicator from PR #97.
2. **Letterbox each thumbnail** — whole frame visible, scaled to fit its slot, centered with padding (not center-cropped).
3. **On by default, with a toggle** (View menu + track right-click). Falls back to the PR-#97 green-fill rendering when off.

## Architecture (Approach A: capture-at-decode + cached letterboxed filmstrip)

### Thumbnail generation (rendering-chain side)

- In `gfcSequence::generateTexture(...)`, after a frame's `bitmap[0]` is confirmed loaded and **before** the CPU buffer is released, downsample it to a small thumbnail when `sett.showThumbnails` is true.
- Downsample: nearest-neighbor box from `getDataPtr()` (`srcW=getWidth()`, `srcH=getHeight()`, `comps=getComponentsPerPixel()`) into a fixed-height buffer that preserves the frame aspect — target height `kThumbH = 48` px, width = `round(kThumbH * srcW / srcH)`, clamped to `kThumbWMax = 96`. **Normalize to RGBA8 at capture**: the loader's pixels are BGRA, so swap B↔R while sampling so the stored thumbnail is always tightly-packed RGBA8 (R,G,B,A). The widget then always wraps it as `QImage::Format_RGBA8888` — no format branching downstream.
- Store in a per-sequence cache: `std::vector<Thumbnail>` indexed by the frame's sequence index, where `struct Thumbnail { int w=0, h=0; std::vector<uint8_t> rgba; };` (empty `rgba` = not captured yet). A new `gfcSequence` member + accessors:
  - `const Thumbnail& getThumbnail(int frameIndex) const;` (returns an empty thumbnail for out-of-range / not-yet-decoded).
  - cleared by the existing unload/clear path (`unloadAndClear` / `clearSequence`).
- Memory cap: cap stored thumbnails per sequence at `kMaxThumbs = 2000`. Beyond that, store every Nth frame (stride = `ceil(numFrames / kMaxThumbs)`) so very long sequences stay bounded; `log`/`fprintf` a one-line note when the stride kicks in. At ~48×27×4 ≈ 5KB each, 2000 × 4 tracks ≈ 40MB worst case.

### Bridge surface (new `jefe::qt::*` in `SequenceLoadBridge_qt.{h,cpp}`)

```cpp
struct ThumbPixels {
    bool present = false;     // a thumbnail exists for this frame
    int  w = 0, h = 0;
    std::vector<unsigned char> rgba;   // tightly packed, row-major, RGBA8
};

// Thumbnail for a frame (sequence-local 0-based index) of a track. Returns
// present=false when thumbnails are off, the track is empty, the index is
// out of range, or that frame hasn't decoded yet.
ThumbPixels getTrackThumbnail(int track, int frameIndex);

bool getThumbnailsEnabled();           // reads sett.showThumbnails
void setThumbnailsEnabled(bool on);    // writes sett.showThumbnails; on->off
                                       // may free caches; calls plateManager.setChanged()
```

`getTrackThumbnail` copies the bytes out (small) so the widget never holds a pointer into sequence memory. `getTrackTimelineState` (PR #97) still provides range/offset/loaded/numFrames.

### Widget (`TimelineTracks_Qt`)

- When `getThumbnailsEnabled()` and a track is loaded:
  - Split the lane: thumbnail strip on top (`thumbH` ≈ lane height − bar − padding), a thin green loaded bar (~4px) at the bottom (reuse the PR-#97 fill color/extent).
  - Compute slots across the loaded extent: `slotW = thumbWMax + gap`; `nSlots = loadedPixelWidth / slotW`. For each slot, map its center x → timeline frame → sequence-local frame index (`frame − rangeStart`, accounting for offset), fetch the thumbnail, and draw it **letterboxed** centered in the slot box (scale to fit, pad with the lane background).
  - Cache `QPixmap`s keyed by `(track, frameIndex)` (a `QHash`); only call `getTrackThumbnail` for slots whose pixmap isn't cached yet. Missing thumbs (not yet decoded, or strided-out) draw as an empty padded slot.
- When disabled: render exactly as PR #97 (dark bar + green fill), no thumbnail work.
- Lane height: bump the widget's minimum height when thumbnails are enabled (e.g. ~40px/lane → min height ≈ 168 for 4 lanes) so letterboxed thumbs are legible; the dock is user-resizable. Restore the PR-#97 min height when disabled.

### Toggle

- A checkable **View → Show Frame Thumbnails** `QAction` in `MainWindow_qt`, and a matching item in the track right-click `QMenu`, both routed through `jefe::qt::setThumbnailsEnabled`. Default checked (on).
- Toggling on mid-session shows thumbnails for frames decoded while enabled; already-decoded frames fill in on the next decode (play-through / scrub / reload). Documented behavior, not a bug.

### Data flow / refresh / invalidation

- The existing ~60Hz `TimelinePanel_Qt::refreshFromPlayback` → `tracks_->refresh()` already runs before its fast-path. `refresh()` additionally drops cached `QPixmap`s for a track when its `getTrackTimelineState` shows a reload (e.g. `numFrames`/label changed) so stale thumbs don't linger.
- Offset drags don't invalidate the pixmap cache — a frame's thumbnail doesn't change, only its x position (keyed by `frameIndex`, not slot).
- Resize re-picks slots but reuses cached pixmaps.

## Out of scope (v1)

- Thumbnails reflecting color-correction / FX (raw decoded frame only, like FLTK).
- Thumbnail persistence across sessions (regenerated on load each run).
- Thumbnail tooltips / hover-preview.

## Files

- **Modify:** `src/gfcSequence.{h,cpp}` — `Thumbnail` struct, per-sequence cache + capture in `generateTexture`, `getThumbnail`, clear on unload.
- **Modify:** `src/gfcStructures.h` — `bool showThumbnails = true;` in `gfcSettings`.
- **Modify:** `src/qt/SequenceLoadBridge_qt.{h,cpp}` — `ThumbPixels`, `getTrackThumbnail`, `getThumbnailsEnabled`/`setThumbnailsEnabled`.
- **Modify:** `src/qt/TimelinePanel_qt.{h,cpp}` — filmstrip rendering, pixmap cache, lane-height bump, right-click toggle item.
- **Modify:** `src/qt/MainWindow_qt.{h,cpp}` — View → Show Frame Thumbnails action.
- **Modify:** `tests/ui/jefecheck/locators.py` + `tests/ui/test_track_timeline.py` — toggle-action locator + smoke assertion.
- **Reference:** `src/gfcpixelbuffer.h` (`GFLC_BITMAP`), PR #97 widget code.

## Testing

- **Appium smoke:** assert the View toggle action exists and the tracks widget renders; toggling doesn't crash. (Per-pixel filmstrip content isn't AX-addressable; verified manually / via the GL pixel-diff harness if needed.)
- **Manual:** load a sequence → filmstrip fills as frames decode; letterboxed thumbs look right; thin green loaded bar tracks beneath; toggle hides/shows (falls back to green fill); offset drag keeps thumbnails aligned with the playhead; a long (>2000-frame) sequence stays responsive and logs the stride note.
