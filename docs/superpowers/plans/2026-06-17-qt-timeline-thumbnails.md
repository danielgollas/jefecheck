# Qt Timeline Frame Thumbnails Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render a downsampled frame filmstrip along each track's timeline bar (letterboxed thumbnails + a thin green loaded bar), captured at decode, with an on-by-default toggle on the timeline panel.

**Architecture:** Each frame's thumbnail is captured inside `gfcFrame::generateTexture()` — the live upload path — from the decoder's CPU buffer (`gfcGLFrameInfo::dataPointer`, BGRA, 8/16-bit) right before it's freed, and stored on the `gfcFrame`. `gfcSequence` exposes thumbnails by frame index; the Qt track widget reaches them through new `jefe::qt::*` bridge accessors (TU separation) and blits cached `QPixmap`s.

**Tech Stack:** C++20, Qt6 Widgets, the `SequenceLoadBridge_qt` bridge, OpenImageIO decode path.

**Spec:** `docs/superpowers/specs/2026-06-17-qt-timeline-thumbnails-design.md`

**Grounding (verified):**
- Live upload: `gfcFrame::generateTexture()` (`src/gfcframe.cpp:205-240`). `gfcGLFrameInfo info = theImageLoader->getFrameInfo();` then `glTexImage2D(info.target,0,info.internalFormat,sizeX,sizeY,0,info.format,info.dataType,info.dataPointer)` at line 223, then `theImageLoader->releaseMemory()` at line 235 frees the CPU data. **Capture must happen between those.**
- `info.dataPointer` is BGRA (OIIO swizzles RGB→BGRA), 4 components. `info.dataType` is `GL_UNSIGNED_BYTE` (8-bit) or `GL_UNSIGNED_SHORT` (16-bit). `sizeX`/`sizeY` are the frame dims (gfcFrame members).
- Call site: `gfcSequence::generateTextures()` (`src/gfcSequence.cpp:780`): `GLuint tmpTexID=tmpRawFrame.generateTexture();` then `frames[tmpRawFrame.indexNumber]=tmpRawFrame;` (line 786) — so a thumbnail stored on `tmpRawFrame` is copied into `frames[]`. `tmpRawFrame.indexNumber` is the 0-based sequence index.
- The old `gfcSequence::generateTexture(RawFrame*)` (lines 362-720) and the `RawFrame` class are **dead code** — do not touch.
- Clear paths: `gfcSequence::clearSequence()` (`src/gfcSequence.cpp:1240`) clears `frames` via `frame[i].clearFrame()`; `unloadAndClear()` (1231) wraps it.
- `gfcSettings` struct constructor sets defaults around `src/gfcStructures.h:296`.

---

## Conventions for every task

- Build: `cmake --build build_qt -j` → expect `[100%] Built target jefecheck`. clangd "QWidget file not found" diagnostics are false positives (no Qt include paths in the language server) — only the CMake result matters.
- Manual launch: `pkill -f jefecheck.app/Contents/MacOS/jefecheck; sleep 0.5; open build_qt/jefecheck.app`.
- No C++ unit-test harness exists; C++ is verified by build + manual launch. Automated tests are Appium/Python.
- Commit each task; stage explicit file lists (never `git add -A`).
- End commit messages with: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`

## File Structure

- **`src/gfcStructures.h`** — `bool showThumbnails` in `gfcSettings`.
- **`src/gfcframe.{h,cpp}`** — `Thumbnail` struct + member on `gfcFrame`; capture in `generateTexture(bool captureThumbnail)`; clear in `clearFrame`.
- **`src/gfcSequence.{h,cpp}`** — `getThumbnail(int)` accessor; stride/cap + flag passing in `generateTextures`.
- **`src/qt/SequenceLoadBridge_qt.{h,cpp}`** — `ThumbPixels`, `getTrackThumbnail`, `get/setThumbnailsEnabled`.
- **`src/qt/TimelinePanel_qt.{h,cpp}`** — filmstrip rendering, `QPixmap` cache, lane-height bump, transport-bar toggle button, right-click toggle item.
- **`tests/ui/jefecheck/locators.py`**, **`tests/ui/test_track_timeline.py`**, **`developer_notes.md`**.

---

## Task 1: `showThumbnails` setting

**Files:** Modify `src/gfcStructures.h`

- [ ] **Step 1: Add the field + default**

Find the `gfcSettings` struct's defaults (near `defaultTextureFormat=GFC_16HALF;`, ~line 296). Add a member declaration alongside the other `bool`s in the struct, and initialize it in the same constructor:

Declaration (with the other members):
```cpp
    bool showThumbnails;   // timeline filmstrip thumbnails (default on)
```
Initialization (next to `defaultTextureFormat=GFC_16HALF;`):
```cpp
        showThumbnails=true;
```

- [ ] **Step 2: Build**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`.

- [ ] **Step 3: Commit**

```bash
git add src/gfcStructures.h
git commit -m "settings: add showThumbnails flag (default on)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: `gfcFrame` thumbnail capture

Add a thumbnail to `gfcFrame` and populate it inside `generateTexture` from the decoder buffer before it's freed. Capture is opt-in per call (the sequence decides, Task 3) so the memory cap stays in one place.

**Files:** Modify `src/gfcframe.h`, `src/gfcframe.cpp`

- [ ] **Step 1: Declare the Thumbnail struct + member + capture flag (header)**

In `src/gfcframe.h`, just before `class gfcFrame` (after the `RawFrame` class, ~line 102), add:

```cpp
// A small RGBA8 thumbnail of a decoded frame, for the timeline filmstrip.
// Empty rgba = not captured. Stored on the gfcFrame so it rides along when
// the frame is copied into the sequence's frames[] vector.
struct GfcThumbnail {
    int w = 0;
    int h = 0;
    std::vector<unsigned char> rgba;   // tightly packed RGBA8, row-major
};
```

Inside `class gfcFrame`'s public members (e.g. near `int sizeX; int sizeY;`), add:
```cpp
    GfcThumbnail thumbnail;
```

Change the `generateTexture` declaration (the `gfcFrame` one at `src/gfcframe.h:161`) to take an opt-in flag:
```cpp
    GLuint generateTexture(bool captureThumbnail = false);
```

(Ensure `#include <vector>` is present in `gfcframe.h`; it already uses `std::vector` for `RawFrame::bitmap`, so it is.)

- [ ] **Step 2: Capture in the implementation**

In `src/gfcframe.cpp`, change the definition signature to match:
```cpp
GLuint gfcFrame::generateTexture(bool captureThumbnail)
```

Then, immediately **after** the `glTexImage2D(...)` call (the line after the `glErr` check, ~line 226) and **before** `theImageLoader->releaseMemory();` (~line 235), insert:

```cpp
	// Capture a downsampled RGBA8 thumbnail for the timeline filmstrip
	// while the decoder's CPU buffer is still alive (releaseMemory frees
	// it just below). Source is BGRA, 8- or 16-bit per info.dataType.
	if (captureThumbnail && info.dataPointer && sizeX > 0 && sizeY > 0
	    && (info.dataType == GL_UNSIGNED_BYTE || info.dataType == GL_UNSIGNED_SHORT)) {
		const int srcW = sizeX, srcH = sizeY;
		const int dstH = 48;
		int dstW = (int)((double)dstH * srcW / srcH + 0.5);
		if (dstW < 1) dstW = 1;
		if (dstW > 96) dstW = 96;
		thumbnail.w = dstW;
		thumbnail.h = dstH;
		thumbnail.rgba.assign((size_t)dstW * dstH * 4, 0);
		const bool sixteen = (info.dataType == GL_UNSIGNED_SHORT);
		const unsigned char*  s8  = (const unsigned char*)info.dataPointer;
		const unsigned short* s16 = (const unsigned short*)info.dataPointer;
		for (int y = 0; y < dstH; ++y) {
			const int sy = y * srcH / dstH;
			for (int x = 0; x < dstW; ++x) {
				const int sx = x * srcW / dstW;
				const size_t si = ((size_t)sy * srcW + sx) * 4; // BGRA, 4 comps
				unsigned char b, g, r, a;
				if (sixteen) {
					b = (unsigned char)(s16[si+0] >> 8);
					g = (unsigned char)(s16[si+1] >> 8);
					r = (unsigned char)(s16[si+2] >> 8);
					a = (unsigned char)(s16[si+3] >> 8);
				} else {
					b = s8[si+0]; g = s8[si+1]; r = s8[si+2]; a = s8[si+3];
				}
				const size_t di = ((size_t)y * dstW + x) * 4;
				thumbnail.rgba[di+0] = r;  // BGRA -> RGBA
				thumbnail.rgba[di+1] = g;
				thumbnail.rgba[di+2] = b;
				thumbnail.rgba[di+3] = a;
			}
		}
	}
```

- [ ] **Step 3: Clear the thumbnail in clearFrame**

Find `gfcFrame::clearFrame()` in `src/gfcframe.cpp` (search `::clearFrame`). Add, with the other resets:
```cpp
	thumbnail.w = 0;
	thumbnail.h = 0;
	thumbnail.rgba.clear();
	thumbnail.rgba.shrink_to_fit();
```

- [ ] **Step 4: Build**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`.
(Existing callers of `generateTexture()` compile unchanged thanks to the default arg.)

- [ ] **Step 5: Commit**

```bash
git add src/gfcframe.h src/gfcframe.cpp
git commit -m "gfcFrame: capture downsampled RGBA thumbnail at decode (opt-in)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: `gfcSequence` thumbnail accessor + capture gating

Gate capture on the setting and a per-sequence stride cap, and expose thumbnails by frame index.

**Files:** Modify `src/gfcSequence.h`, `src/gfcSequence.cpp`

- [ ] **Step 1: Declare the accessor (header)**

In `src/gfcSequence.h`, near `getLoadedFrameCount()` / `getNumFrames()`, add:
```cpp
    // Thumbnail for a decoded frame (0-based sequence index), for the
    // timeline filmstrip. Returns an empty thumbnail (w==0) when the
    // index is out of range or that frame hasn't been captured.
    const GfcThumbnail& getThumbnail(int frameIndex) const;
```
(`gfcSequence.h` includes `gfcframe.h`, so `GfcThumbnail` is visible.)

- [ ] **Step 2: Implement the accessor**

In `src/gfcSequence.cpp`, near `getLoadedFrameCount()`:
```cpp
const GfcThumbnail& gfcSequence::getThumbnail(int frameIndex) const {
	static const GfcThumbnail empty;
	if (frameIndex < 0 || frameIndex >= (int)frames.size()) return empty;
	return frames[frameIndex].thumbnail;
}
```

- [ ] **Step 3: Gate capture in generateTextures (cap + setting)**

In `src/gfcSequence.cpp`, at the upload call site (~line 780), replace:
```cpp
			GLuint tmpTexID=tmpRawFrame.generateTexture();
```
with:
```cpp
			// Capture a filmstrip thumbnail when enabled, capped per
			// sequence: store at most kMaxThumbs frames (every Nth beyond
			// that) so very long sequences stay bounded in memory.
			constexpr int kMaxThumbs = 2000;
			const int totalFrames = (int)files.size();
			const int thumbStride = totalFrames > kMaxThumbs
			                        ? (totalFrames + kMaxThumbs - 1) / kMaxThumbs : 1;
			const bool capture = sett.showThumbnails
			                     && (tmpRawFrame.indexNumber % thumbStride == 0);
			GLuint tmpTexID=tmpRawFrame.generateTexture(capture);
```
(`sett` is the global settings instance already used throughout `gfcSequence.cpp`.)

- [ ] **Step 4: Build**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`.

- [ ] **Step 5: Commit**

```bash
git add src/gfcSequence.h src/gfcSequence.cpp
git commit -m "gfcSequence: getThumbnail() + capped/gated thumbnail capture

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Bridge surface

**Files:** Modify `src/qt/SequenceLoadBridge_qt.h`, `src/qt/SequenceLoadBridge_qt.cpp`

- [ ] **Step 1: Declare in the header**

In `src/qt/SequenceLoadBridge_qt.h`, before `}  // namespace jefe::qt`, add:
```cpp
// A copy of a frame's filmstrip thumbnail for the timeline widget.
// present=false when thumbnails are off, the track/frame is invalid, or
// that frame hasn't decoded yet. rgba is tightly-packed RGBA8 (wrap as
// QImage::Format_RGBA8888).
struct ThumbPixels {
    bool present = false;
    int  w = 0;
    int  h = 0;
    std::vector<unsigned char> rgba;
};

ThumbPixels getTrackThumbnail(int track, int frameIndex);

bool getThumbnailsEnabled();        // reads sett.showThumbnails
void setThumbnailsEnabled(bool on); // writes sett.showThumbnails; setChanged()
```
(Ensure `#include <vector>` is present in the header — it already uses `std::vector` for `TrackParams::channelOptions`, so it is.)

- [ ] **Step 2: Implement**

In `src/qt/SequenceLoadBridge_qt.cpp`, in `namespace jefe::qt` (e.g. after `getTrackTimelineState`):
```cpp
ThumbPixels getTrackThumbnail(int track, int frameIndex) {
    ThumbPixels out;
    if (!sett.showThumbnails) return out;
    auto* seq = trackManager.getSequence(track);
    if (!seq) return out;
    const GfcThumbnail& t = seq->getThumbnail(frameIndex);
    if (t.w <= 0 || t.h <= 0 || t.rgba.empty()) return out;
    out.present = true;
    out.w = t.w;
    out.h = t.h;
    out.rgba = t.rgba;   // copy out; widget must not hold seq memory
    return out;
}

bool getThumbnailsEnabled() {
    return sett.showThumbnails;
}

void setThumbnailsEnabled(bool on) {
    sett.showThumbnails = on;
    plateManager.setChanged();
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`.

- [ ] **Step 4: Commit**

```bash
git add src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp
git commit -m "qt bridge: thumbnail accessor + enabled toggle

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Filmstrip rendering in `TimelineTracks_Qt`

Draw thumbnails across the loaded extent (letterboxed, slot-sampled, `QPixmap`-cached) with a thin green loaded bar beneath; fall back to the plain green fill when disabled.

**Files:** Modify `src/qt/TimelinePanel_qt.h`, `src/qt/TimelinePanel_qt.cpp`

- [ ] **Step 1: Add cache members + helper decl (header)**

In `src/qt/TimelinePanel_qt.h`, add includes near the top:
```cpp
#include <QHash>
#include <QPixmap>
```
In `TimelineTracks_Qt`'s `private:` section add:
```cpp
    // Cached thumbnail pixmaps, keyed by (track << 24) | frameIndex.
    QHash<int, QPixmap> thumbCache_;
    bool lastThumbsEnabled_ = true;
    QPixmap thumbPixmap(int track, int frameIndex);   // fetch+cache
    void paintFilmstrip(QPainter& p, int track, int laneY, int laneH,
                        const jefe::qt::TrackTimelineState& s);
```

- [ ] **Step 2: Implement the cache fetch + filmstrip paint**

In `src/qt/TimelinePanel_qt.cpp`, add near the other `TimelineTracks_Qt` methods. First, includes at the top:
```cpp
#include <QImage>
```
Then:
```cpp
QPixmap TimelineTracks_Qt::thumbPixmap(int track, int frameIndex) {
    const int key = (track << 24) | (frameIndex & 0x00FFFFFF);
    auto it = thumbCache_.find(key);
    if (it != thumbCache_.end()) return it.value();
    jefe::qt::ThumbPixels t = jefe::qt::getTrackThumbnail(track, frameIndex);
    QPixmap pm;
    if (t.present) {
        // QImage needs the data to outlive it until copied; copy() forces
        // a deep copy so the temporary vector can go away.
        QImage img(t.rgba.data(), t.w, t.h, t.w * 4, QImage::Format_RGBA8888);
        pm = QPixmap::fromImage(img.copy());
        thumbCache_.insert(key, pm);   // only cache real thumbs
    }
    return pm;
}

void TimelineTracks_Qt::paintFilmstrip(QPainter& p, int track, int laneY,
                                       int laneH,
                                       const jefe::qt::TrackTimelineState& s) {
    // Lane splits into a thumbnail strip (top) and a thin loaded bar.
    const int barH = 4;
    const int gap = 1;
    const int stripTop = laneY + 2;
    const int stripH = laneH - barH - 4;
    if (stripH < 6) return;  // lane too short; caller drew the fallback bar

    // Loaded extent in pixels (frames rangeStart..rangeStart+loadedCount-1).
    const int loadedEndFrame =
        s.loadedCount > 0 ? s.firstLoadedFrame + s.loadedCount - 1 : s.firstLoadedFrame;
    const int xL0 = xFromFrameMapped(s.firstLoadedFrame, width(), from_, to_);
    const int xL1 = xFromFrameMapped(loadedEndFrame, width(), from_, to_);
    const int loadedW = std::max(xL1 - xL0, 1);

    // Slot width from a representative thumbnail's aspect; default 4:3-ish.
    int slotW = std::max(stripH * 16 / 9, 24);
    const int nSlots = std::max(loadedW / (slotW + gap), 1);

    for (int i = 0; i < nSlots; ++i) {
        const int slotX = xL0 + i * (slotW + gap);
        const int centerX = slotX + slotW / 2;
        const int frame = frameFromXMapped(centerX, width(), from_, to_);
        const int frameIndex = frame - s.rangeStart;  // 0-based seq index
        if (frameIndex < 0) continue;
        QPixmap pm = thumbPixmap(track, frameIndex);
        if (pm.isNull()) continue;
        // Letterbox: scale to fit the slot box, keep aspect, center.
        QPixmap scaled = pm.scaled(slotW, stripH, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
        const int dx = slotX + (slotW - scaled.width()) / 2;
        const int dy = stripTop + (stripH - scaled.height()) / 2;
        p.drawPixmap(dx, dy, scaled);
    }

    // Thin loaded bar beneath the strip.
    if (s.loadedCount > 0) {
        const QRect bar(xL0, laneY + laneH - barH - 1, loadedW, barH);
        p.fillRect(bar, QColor(150, 185, 150));
    }
}
```

- [ ] **Step 3: Branch the lane paint on the toggle**

In `TimelineTracks_Qt::paintEvent`, inside the per-track loop, replace the present-track drawing block (the dark bar + green loaded fill + label) so it calls the filmstrip when enabled. Locate the existing block that starts after `if (!s.present) { ... continue; }` (the `// Sequence bar ...` through the label `drawText`). Wrap it:

```cpp
        if (jefe::qt::getThumbnailsEnabled()) {
            // Filmstrip: thumbnails + thin loaded bar.
            paintFilmstrip(p, i, y, lh, s);
            // Label over the strip (kept for identification).
            p.setPen(QColor(230, 230, 230));
            p.drawText(xFromFrameMapped(s.rangeStart, width(), from_, to_) + 4,
                       y + 12,
                       QString("%1  %2").arg(letter)
                           .arg(QString::fromStdString(s.label)));
        } else {
            // ... existing PR-#97 dark-bar + green-fill + label drawing ...
        }
```
(Keep the existing dark-bar/green-fill/label code verbatim inside the `else`.)

- [ ] **Step 4: Refresh handling — drop cache on toggle / reload**

In `TimelineTracks_Qt::refresh()`, after computing `next` and before the cache-compare early return, add toggle-change handling and per-track reload invalidation:
```cpp
    const bool thumbsEnabled = jefe::qt::getThumbnailsEnabled();
    bool toggleChanged = (thumbsEnabled != lastThumbsEnabled_);
    if (toggleChanged) { thumbCache_.clear(); lastThumbsEnabled_ = thumbsEnabled; }

    // Drop cached pixmaps for a track whose sequence changed (reload/unload).
    for (int i = 0; i < 4; ++i) {
        if (states_[i].numFrames != next[i].numFrames
            || states_[i].label != next[i].label) {
            const int lo = i << 24, hi = (i << 24) | 0x00FFFFFF;
            for (auto it = thumbCache_.begin(); it != thumbCache_.end(); ) {
                it = (it.key() >= lo && it.key() <= hi) ? thumbCache_.erase(it)
                                                        : ++it;
            }
        }
    }
```
Then include `toggleChanged` in the existing `changed` decision so a toggle forces a repaint:
```cpp
    bool changed = toggleChanged || (from != from_ || to != to_ || cur != current_);
```
(Leave the rest of the cache-compare loop and the `if (!changed) return;` as-is.)

- [ ] **Step 5: Lane-height bump when thumbnails on**

In the `TimelineTracks_Qt` constructor, after `setSizePolicy(...)`, the min height is `setMinimumHeight(96)`. Make it depend on the toggle by re-setting it in `refresh()` when the toggle changes. In the `if (toggleChanged)` block from Step 4, add:
```cpp
        setMinimumHeight(thumbsEnabled ? 168 : 96);  // taller lanes for thumbs
```

- [ ] **Step 6: Build + manual verify**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`. Launch, load a sequence. Expected: as frames decode, the lane fills with small letterboxed thumbnails; a thin green bar sits beneath; the label shows over the strip. Drag the offset — thumbnails move with the bar and stay aligned. (Toggle button comes in Task 6; for now thumbnails are always on by default.)

- [ ] **Step 7: Commit**

```bash
git add src/qt/TimelinePanel_qt.h src/qt/TimelinePanel_qt.cpp
git commit -m "qt timeline: filmstrip thumbnail rendering + pixmap cache

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Thumbnails toggle on the timeline panel

A checkable button in the transport bar + a right-click menu item, both routed to `setThumbnailsEnabled`.

**Files:** Modify `src/qt/TimelinePanel_qt.h`, `src/qt/TimelinePanel_qt.cpp`

- [ ] **Step 1: Add the button member (header)**

In `TimelinePanel_Qt`'s `private:` members, add:
```cpp
    QPushButton* thumbsBtn_ = nullptr;
```

- [ ] **Step 2: Create + wire the transport-bar button**

In `TimelinePanel_Qt`'s constructor, in the transport layout (near where `fpsSpin_` is added), add a checkable button:
```cpp
    thumbsBtn_ = new QPushButton("🎞", this);
    thumbsBtn_->setCheckable(true);
    thumbsBtn_->setChecked(jefe::qt::getThumbnailsEnabled());
    thumbsBtn_->setToolTip("Show frame thumbnails on the timeline");
    thumbsBtn_->setObjectName("transport.thumbnails.toggle");
    thumbsBtn_->setAccessibleName("Show frame thumbnails");
    thumbsBtn_->setFixedWidth(32);
    transport->addWidget(thumbsBtn_);
    connect(thumbsBtn_, &QPushButton::toggled, this, [this](bool on) {
        jefe::qt::setThumbnailsEnabled(on);
        if (tracks_) tracks_->refresh();
    });
```
Place this `addWidget` where you want it in the transport row (e.g. just before the `transport->addStretch(1)` that precedes the FPS group, or right after the FPS field — match the existing ordering style).

- [ ] **Step 3: Right-click menu item in the tracks widget**

In `TimelineTracks_Qt::contextMenuEvent` (added in PR #97), add a thumbnails toggle to the existing `QMenu`, before `menu.exec`:
```cpp
    QAction* thumbs = menu.addAction("Show frame thumbnails");
    thumbs->setCheckable(true);
    thumbs->setChecked(jefe::qt::getThumbnailsEnabled());
```
and handle it in the result dispatch (alongside the existing `hold`/`setOff` branches):
```cpp
    } else if (chosen == thumbs) {
        jefe::qt::setThumbnailsEnabled(thumbs->isChecked());
        refresh();
    }
```

- [ ] **Step 4: Build + manual verify**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`. Launch, load a sequence. Toggle the 🎞 button: thumbnails hide (falls back to the green fill, lanes shrink) and show (lanes grow, filmstrip returns). The right-click item mirrors the button state.

- [ ] **Step 5: Commit**

```bash
git add src/qt/TimelinePanel_qt.h src/qt/TimelinePanel_qt.cpp
git commit -m "qt timeline: thumbnails toggle (transport button + right-click)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Locator + smoke test + docs

**Files:** Modify `tests/ui/jefecheck/locators.py`, `tests/ui/test_track_timeline.py`, `developer_notes.md`

- [ ] **Step 1: Add the locator**

In `tests/ui/jefecheck/locators.py`, near the timeline locators added in PR #97:
```python
TIMELINE_THUMBS_TOGGLE = "transport.thumbnails.toggle"
```

- [ ] **Step 2: Add a smoke assertion**

Append to `tests/ui/test_track_timeline.py`:
```python
def test_thumbnails_toggle_present(app):
    """The timeline thumbnails toggle button is present and addressable."""
    btn = app.by_object_name(locators.TIMELINE_THUMBS_TOGGLE)
    assert btn is not None
```

- [ ] **Step 3: Validate it parses**

Run: `python3 -c "import ast; ast.parse(open('tests/ui/test_track_timeline.py').read()); ast.parse(open('tests/ui/jefecheck/locators.py').read()); print('parse OK')"`
Expected: `parse OK`.

- [ ] **Step 4: Document**

In `developer_notes.md`, add a short section after §14 covering: thumbnails are captured in `gfcFrame::generateTexture(bool)` from the decoder's BGRA buffer (8/16-bit) before `releaseMemory`, ride on the `gfcFrame` into `frames[]`, are capped per sequence (stride), exposed via `jefe::qt::getTrackThumbnail`, and blitted by `TimelineTracks_Qt` as cached letterboxed `QPixmap`s; the toggle is `sett.showThumbnails` driven by the transport button; cache is keyed `(track<<24)|frameIndex` and dropped on toggle/reload.

- [ ] **Step 5: Commit**

```bash
git add tests/ui/jefecheck/locators.py tests/ui/test_track_timeline.py developer_notes.md
git commit -m "qt timeline: thumbnails locator + smoke test + developer notes

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Final review (after all tasks)

- Build green; all task commits present.
- Manual pass: filmstrip fills during decode; letterbox correct; thin green bar beneath; toggle hides/shows (lane height adjusts); offset drag keeps thumbs aligned; a long (>2000-frame) sequence stays responsive (stride cap engaged).
- Open a PR against `qt-experimental`; squash-merge after review.
- Update `memory/project_qt_plate_polish.md`: mark #4b done.
