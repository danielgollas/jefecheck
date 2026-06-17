# Qt Track Timeline Widget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the stub `TimelineTracks_Qt` with a real, interactive per-track timeline (4 rows showing range/offset/loaded-fill + drag-to-offset, alt-click load-from-frame, drop-to-load, and a right-click offset/hold-frame popup).

**Architecture:** One multi-row `QWidget` (`TimelineTracks_Qt`) paints all four lanes and owns all interaction; it reaches sequence/track state only through new `jefe::qt::*` bridge accessors (TU separation — the widget must not include `gfcSequence.h`/`gfctrackmanager.h`). It shares the scrubber's pixel↔frame mapping so rows stay aligned beneath the scrubber.

**Tech Stack:** C++20, Qt6 Widgets, the existing `SequenceLoadBridge_qt` bridge layer, the dark-VFX QSS theme. UI tests are Appium/Python under `tests/ui/`.

**Spec:** `docs/superpowers/specs/2026-06-16-qt-track-timeline-widget-design.md`

**Reference (read-only, from git `9a1c605`):** `src/trackwidget.cpp`, `UICallbacks.cpp::tracksBarCB`, `gfcSequence.cpp` lines ~900–915 (loaded-fill semantics).

---

## Conventions for every task

- Build: `cmake --build build_qt -j` from the repo root. Expect `[100%] Built target jefecheck`. Ignore clangd diagnostics about missing Qt headers — the language server has no Qt include paths; only the CMake build result matters.
- Manual launch: `pkill -f jefecheck.app/Contents/MacOS/jefecheck; sleep 0.5; open build_qt/jefecheck.app`.
- There is **no C++ unit-test harness** in this project (per CLAUDE.md). C++ behavior is verified by build + manual launch with explicit expected outcomes. Automated tests are Appium/Python (`tests/ui/`), used where a widget is observable.
- Commit each task separately. Stage explicit file lists — never `git add -A` (the build tree is untracked and huge).
- End every commit message with:
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`

---

## File Structure

- **`src/gfcSequence.{h,cpp}`** — add one public accessor `getLoadedFrameCount()` (count of decoded frames). The only rendering-chain change.
- **`src/qt/SequenceLoadBridge_qt.{h,cpp}`** — new `TrackTimelineState` struct + accessors/mutators (`getTrackTimelineState`, `getTrackOffset`, `setTrackOffset`, `startLoadingTrackAt`, `getTrackHoldMode`, `setTrackHoldMode`). Drop-to-load reuses the existing `loadFileIntoPlate`.
- **`src/qt/TimelinePanel_qt.{h,cpp}`** — shared pixel↔frame mapping helpers; flesh out `TimelineTracks_Qt` (paint, refresh+cache, mouse, context menu, drop); call `tracks_->refresh()` from `refreshFromPlayback`.
- **`tests/ui/jefecheck/locators.py`** + **`tests/ui/test_track_timeline.py`** — object names + a smoke/behavioral Appium test.

---

## Task 1: `gfcSequence::getLoadedFrameCount()`

The bridge needs the count of decoded frames to draw the loaded fill. `loadedFrames` (a `std::queue<int>`) is private; expose its size. (`isEmpty()` already reads `loadedFrames.empty()`, confirming this is the canonical "how much is loaded" signal — FLTK fed the track widget's progress bar from `loadedFrames.size()`, see `gfcSequence.cpp@9a1c605:795`.)

**Files:**
- Modify: `src/gfcSequence.h` (public method group near `getNumFrames()` / `isEmpty()`, around line 105–111)
- Modify: `src/gfcSequence.cpp` (near `getNumFrames()` impl)

- [ ] **Step 1: Declare the accessor**

In `src/gfcSequence.h`, immediately after the `bool isEmpty();` declaration, add:

```cpp
    // Number of frames decoded into this sequence so far. Mirrors the
    // value FLTK fed the track-widget progress bar (loadedFrames.size()).
    // Used by the Qt track timeline to draw the loaded-vs-unloaded fill.
    int getLoadedFrameCount();
```

- [ ] **Step 2: Implement it**

In `src/gfcSequence.cpp`, immediately after the `gfcSequence::getNumFrames()` implementation, add:

```cpp
int gfcSequence::getLoadedFrameCount() {
	return (int)loadedFrames.size();
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build_qt -j`
Expected: `[100%] Built target jefecheck` (no errors).

- [ ] **Step 4: Commit**

```bash
git add src/gfcSequence.h src/gfcSequence.cpp
git commit -m "gfcSequence: expose getLoadedFrameCount() for track timeline fill

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Bridge — track timeline state + mutators

Add the read struct and the mutators the widget will call. All live in `namespace jefe::qt`. The `.cpp` already includes `gfctrackmanager.h`, `gfcSequence.h`, `gfcplatemanager.h` and has `extern gfcTrackManager trackManager;` / `extern gfcPlateManager plateManager;`.

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.h` (inside `namespace jefe::qt`, before the closing `}` at line ~580)
- Modify: `src/qt/SequenceLoadBridge_qt.cpp` (inside `namespace jefe::qt`)

- [ ] **Step 1: Declare the struct + accessors in the header**

In `src/qt/SequenceLoadBridge_qt.h`, just before `}  // namespace jefe::qt`, add:

```cpp
// Per-track snapshot for the timeline track rows. All frame values are
// in global-timeline coordinates (offset already folded in), so the
// widget can map them straight through the same x<->frame transform the
// scrubber uses. `present` is false for an empty track (draw a
// placeholder lane). `loadedCount` frames starting at `firstLoadedFrame`
// are decoded; the rest of [rangeStart, rangeEnd] is not-yet-loaded.
struct TrackTimelineState {
    bool        present          = false;
    int         rangeStart       = 1;
    int         rangeEnd         = 1;
    int         offset           = 0;
    int         numFrames        = 0;
    int         firstLoadedFrame = 1;
    int         loadedCount      = 0;
    std::string label;
};

TrackTimelineState getTrackTimelineState(int trackIdx);

// Frame offset of a track on the global timeline. setTrackOffset shifts
// the whole sequence left/right (drag / "Set offset..." popup) and flags
// the viewport dirty so it repaints at the new frame mapping.
int  getTrackOffset(int trackIdx);
void setTrackOffset(int trackIdx, int offset);

// Begin decoding the track's assigned sequence starting at `frame`
// (timeline coordinates). Wraps trackManager.startLoadingSequenceAt;
// the loader thread fills rawFrames and the playback tick uploads them,
// so no GL context is required here. No-op for an empty track.
void startLoadingTrackAt(int trackIdx, int frame);

// Hold-last-frame mode (right-click popup). gfcSequence::getHoldMode()
// is an int; the widget only needs on/off, so the bridge translates:
// getTrackHoldMode == (getHoldMode() != 0); setTrackHoldMode(true) ->
// setHoldMode(1), (false) -> setHoldMode(0).
bool getTrackHoldMode(int trackIdx);
void setTrackHoldMode(int trackIdx, bool hold);
```

- [ ] **Step 2: Implement in the .cpp**

In `src/qt/SequenceLoadBridge_qt.cpp`, inside `namespace jefe::qt` (e.g. just before the `getTrackParams` implementation), add:

```cpp
TrackTimelineState getTrackTimelineState(int trackIdx) {
    TrackTimelineState s;
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq) return s;
    s.present = !seq->isEmpty();
    s.offset  = seq->getOffset();
    s.rangeStart = seq->getRangeStart();   // offset already folded in
    s.rangeEnd   = seq->getRangeEnd();
    s.numFrames  = seq->getNumFrames();
    s.loadedCount = seq->getLoadedFrameCount();
    // v1: anchor the loaded fill at the sequence's range start. Loading
    // is sequential, so [rangeStart, rangeStart + loadedCount) is the
    // decoded run. (Precise fill positioning for alt-click load-from-X
    // is a deferred refinement noted in the spec.)
    s.firstLoadedFrame = s.rangeStart;
    if (seq->myGUI && !seq->myGUI->getFilename().empty()) {
        namespace fs = std::filesystem;
        s.label = fs::path(seq->filenameGeneric.empty()
                               ? seq->myGUI->getFilename()
                               : seq->filenameGeneric).filename().string();
    }
    return s;
}

int getTrackOffset(int trackIdx) {
    auto* seq = trackManager.getSequence(trackIdx);
    return seq ? seq->getOffset() : 0;
}

void setTrackOffset(int trackIdx, int offset) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq) return;
    seq->setOffset(offset);
    plateManager.setChanged();
}

void startLoadingTrackAt(int trackIdx, int frame) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq || seq->isEmpty()) return;
    trackManager.startLoadingSequenceAt(trackIdx, frame);
}

bool getTrackHoldMode(int trackIdx) {
    auto* seq = trackManager.getSequence(trackIdx);
    return seq ? (seq->getHoldMode() != 0) : false;
}

void setTrackHoldMode(int trackIdx, bool hold) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq) return;
    seq->setHoldMode(hold ? 1 : 0);
    plateManager.setChanged();
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build_qt -j`
Expected: `[100%] Built target jefecheck`.

If `getHoldMode()`/`setHoldMode(int)` produce a signature error, check `src/gfcSequence.h` (declarations are `int getHoldMode();` and `void setHoldMode(int holdMode, int holdFrame=0);`) and adjust the call to `seq->setHoldMode(hold ? 1 : 0)` (the default `holdFrame=0` applies).

- [ ] **Step 4: Commit**

```bash
git add src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp
git commit -m "qt bridge: track timeline state + offset/hold/load-at accessors

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Shared pixel↔frame mapping

`TimelineScrubber_Qt` has `frameFromX`/`xFromFrame` (`TimelinePanel_qt.cpp:54-68`). The tracks widget needs the identical transform so its bars line up under the scrubber. Factor the math into file-local free functions and have the scrubber call them; the tracks widget will use them in Task 4.

**Files:**
- Modify: `src/qt/TimelinePanel_qt.cpp` (the anonymous namespace at lines 19-21, and the scrubber methods at 54-68)

- [ ] **Step 1: Add the shared helpers**

In `src/qt/TimelinePanel_qt.cpp`, replace the anonymous namespace block:

```cpp
namespace {
constexpr int kScrubberPad = 4;  // horizontal padding inside the scrubber
}  // namespace
```

with:

```cpp
namespace {
constexpr int kScrubberPad = 4;  // horizontal padding inside the lane area

// Shared pixel<->frame mapping used by both the scrubber and the track
// rows so their frames line up vertically. `width` is the widget width;
// the playable strip [from, to] sits inside `kScrubberPad` on each side.
int frameFromXMapped(int x, int width, int from, int to) {
    const int span = to - from;
    if (span <= 0) return from;
    const int usable = std::max(width - 2 * kScrubberPad, 1);
    const int rel = std::clamp(x - kScrubberPad, 0, usable);
    return from + (rel * span + usable / 2) / usable;
}

int xFromFrameMapped(int frame, int width, int from, int to) {
    const int span = to - from;
    if (span <= 0) return kScrubberPad;
    const int usable = std::max(width - 2 * kScrubberPad, 1);
    const int rel = std::clamp(frame - from, 0, span);
    return kScrubberPad + (rel * usable) / span;
}
}  // namespace
```

- [ ] **Step 2: Route the scrubber through the helpers**

Replace the scrubber's two methods (`TimelinePanel_qt.cpp:54-68`):

```cpp
int TimelineScrubber_Qt::frameFromX(int x) const {
    const int span = to_ - from_;
    if (span <= 0) return from_;
    const int usable = std::max(width() - 2 * kScrubberPad, 1);
    const int rel = std::clamp(x - kScrubberPad, 0, usable);
    return from_ + (rel * span + usable / 2) / usable;
}

int TimelineScrubber_Qt::xFromFrame(int frame) const {
    const int span = to_ - from_;
    if (span <= 0) return kScrubberPad;
    const int usable = std::max(width() - 2 * kScrubberPad, 1);
    const int rel = std::clamp(frame - from_, 0, span);
    return kScrubberPad + (rel * usable) / span;
}
```

with:

```cpp
int TimelineScrubber_Qt::frameFromX(int x) const {
    return frameFromXMapped(x, width(), from_, to_);
}

int TimelineScrubber_Qt::xFromFrame(int frame) const {
    return xFromFrameMapped(frame, width(), from_, to_);
}
```

- [ ] **Step 3: Build + manual sanity check**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`.
Launch and confirm the scrubber playhead/in-out still track correctly (pure refactor; behavior must be unchanged): load a sequence, scrub, confirm the playhead follows the cursor exactly as before.

- [ ] **Step 4: Commit**

```bash
git add src/qt/TimelinePanel_qt.cpp
git commit -m "qt timeline: factor pixel<->frame mapping into shared helpers

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: `TimelineTracks_Qt` painting + live refresh

Render the 4 rows from `TrackTimelineState`, with a cache-gated `refresh()` driven from the panel tick. No interaction yet.

**Files:**
- Modify: `src/qt/TimelinePanel_qt.h` (the `TimelineTracks_Qt` class, lines 55-68)
- Modify: `src/qt/TimelinePanel_qt.cpp` (`TimelineTracks_Qt` ctor/paint, lines 112-145; and `refreshFromPlayback`, lines 292-354)

- [ ] **Step 1: Expand the class declaration**

In `src/qt/TimelinePanel_qt.h`, replace the whole `TimelineTracks_Qt` class (lines 55-68):

```cpp
class TimelineTracks_Qt : public QWidget {
    Q_OBJECT
public:
    explicit TimelineTracks_Qt(QWidget* parent = nullptr);

    void setTimelineRange(int from, int to);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    int from_ = 1;
    int to_ = 1;
};
```

with:

```cpp
#include "SequenceLoadBridge_qt.h"   // jefe::qt::TrackTimelineState
#include <array>

class TimelineTracks_Qt : public QWidget {
    Q_OBJECT
public:
    explicit TimelineTracks_Qt(QWidget* parent = nullptr);

    // Pull global range, current frame, and the 4 per-track states from
    // the bridge; cache-compare and repaint only on change. Driven from
    // TimelinePanel_Qt::refreshFromPlayback. Cheap when idle.
    void refresh();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    int laneTopY(int track) const;   // y of lane `track`'s top edge
    int laneHeight() const;          // per-lane height

    int from_ = 1;
    int to_ = 1;
    int current_ = 1;
    std::array<jefe::qt::TrackTimelineState, 4> states_{};
};
```

(Note: `TimelinePanel_qt.h` already forward-declares Qt classes; adding the bridge include here is fine — the bridge header is Qt-safe and does not pull glad.)

- [ ] **Step 2: Replace the ctor + paint, add helpers + refresh**

In `src/qt/TimelinePanel_qt.cpp`, replace the `TimelineTracks_Qt` ctor + `setTimelineRange` + `paintEvent` (lines 114-145) with:

```cpp
TimelineTracks_Qt::TimelineTracks_Qt(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(96);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setObjectName("timeline.tracks");
    setAccessibleName("Timeline tracks");
}

int TimelineTracks_Qt::laneHeight() const {
    return std::max(height() / 4, 16);
}

int TimelineTracks_Qt::laneTopY(int track) const {
    return track * laneHeight();
}

void TimelineTracks_Qt::refresh() {
    const int from = jefe::qt::getFromFrame();
    const int to   = std::max(jefe::qt::getToFrame(), from);
    const int cur  = jefe::qt::getCurrentFrame();

    std::array<jefe::qt::TrackTimelineState, 4> next{};
    for (int i = 0; i < 4; ++i) next[i] = jefe::qt::getTrackTimelineState(i);

    // Cache-compare: skip the repaint when nothing a row draws has
    // changed (matches the AppKit-cascade-avoidance pattern elsewhere).
    bool changed = (from != from_ || to != to_ || cur != current_);
    for (int i = 0; i < 4 && !changed; ++i) {
        const auto& a = states_[i];
        const auto& b = next[i];
        changed = a.present != b.present || a.rangeStart != b.rangeStart
               || a.rangeEnd != b.rangeEnd || a.loadedCount != b.loadedCount
               || a.offset != b.offset || a.label != b.label;
    }
    if (!changed) return;

    from_ = from; to_ = to; current_ = cur; states_ = next;
    update();
}

void TimelineTracks_Qt::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRect r = rect();
    p.fillRect(r, QColor(28, 28, 28));

    const int lh = laneHeight();
    for (int i = 0; i < 4; ++i) {
        const int y = laneTopY(i);
        const QRect lane(0, y, r.width(), lh - 2);
        p.fillRect(lane, i % 2 ? QColor(36, 36, 36) : QColor(32, 32, 32));

        const auto& s = states_[i];
        const QChar letter('A' + i);

        if (!s.present) {
            // Empty lane: faint placeholder, doubles as drop / alt-click target.
            p.setPen(QColor(90, 90, 90));
            p.drawText(8, y + lh / 2 + 4,
                       QString("%1  (empty — drop or alt-click to load)")
                           .arg(letter));
            continue;
        }

        // Sequence bar across [rangeStart, rangeEnd] in dark gray.
        const int xStart = xFromFrameMapped(s.rangeStart, width(), from_, to_);
        const int xEnd   = xFromFrameMapped(s.rangeEnd,   width(), from_, to_);
        const int barTop = y + 3;
        const int barH   = lh - 8;
        const QRect bar(xStart, barTop, std::max(xEnd - xStart, 1), barH);
        p.fillRect(bar, QColor(85, 85, 85));

        // Loaded portion (lighter), starting at firstLoadedFrame.
        if (s.loadedCount > 0) {
            const int loadedEndFrame = s.firstLoadedFrame + s.loadedCount - 1;
            const int xL0 = xFromFrameMapped(s.firstLoadedFrame, width(), from_, to_);
            const int xL1 = xFromFrameMapped(loadedEndFrame,     width(), from_, to_);
            const QRect loaded(xL0, barTop, std::max(xL1 - xL0, 1), barH);
            p.fillRect(loaded, QColor(160, 160, 160));
        }

        // Label (track letter + filename) over the bar.
        p.setPen(QColor(230, 230, 230));
        p.drawText(xStart + 4, y + lh / 2 + 4,
                   QString("%1  %2").arg(letter).arg(QString::fromStdString(s.label)));
    }

    // Read-only playhead across all lanes (alignment with the scrubber;
    // not draggable — drag is reassigned to offset).
    const int xCur = xFromFrameMapped(current_, width(), from_, to_);
    p.setPen(QPen(QColor(0xd4, 0x77, 0x1e), 1));
    p.drawLine(xCur, 0, xCur, r.height());
}
```

- [ ] **Step 3: Drive `refresh()` from the panel tick**

In `src/qt/TimelinePanel_qt.cpp`, in `refreshFromPlayback()` (lines 292-354): the function early-returns when playback values are unchanged, but track loaded-state grows during decode even when the frame/range don't move. So refresh the tracks **before** the fast-path return. Replace the opening of `refreshFromPlayback` (lines 292-310):

```cpp
void TimelinePanel_Qt::refreshFromPlayback() {
    const int from = jefe::qt::getFromFrame();
    const int to   = jefe::qt::getToFrame();
    const int cur  = jefe::qt::getCurrentFrame();
    const int in   = jefe::qt::getInPoint();
    const int out  = jefe::qt::getOutPoint();
    const int loop = jefe::qt::getLoopMode();
    const float fps = jefe::qt::getTargetFPS();
    const bool playing = jefe::qt::isPlaying();

    // Fast-path: nothing changed since the last tick, so every widget
    // setter below would be a value-identical no-op that still walks
    // QAccessible / AppKit / AttributeGraph. Skip the whole pass.
    if (lastCacheValid_
        && from == lastFrom_ && to == lastTo_ && cur == lastCur_
        && in == lastIn_ && out == lastOut_ && loop == lastLoop_
        && fps == lastFps_ && playing == lastPlaying_) {
        return;
    }
```

with:

```cpp
void TimelinePanel_Qt::refreshFromPlayback() {
    // Track rows update independently of the transport values (their
    // loaded fill grows during decode even while frame/range hold), and
    // refresh() is itself cache-gated, so do it before the fast-path.
    tracks_->refresh();

    const int from = jefe::qt::getFromFrame();
    const int to   = jefe::qt::getToFrame();
    const int cur  = jefe::qt::getCurrentFrame();
    const int in   = jefe::qt::getInPoint();
    const int out  = jefe::qt::getOutPoint();
    const int loop = jefe::qt::getLoopMode();
    const float fps = jefe::qt::getTargetFPS();
    const bool playing = jefe::qt::isPlaying();

    // Fast-path: nothing changed since the last tick, so every widget
    // setter below would be a value-identical no-op that still walks
    // QAccessible / AppKit / AttributeGraph. Skip the whole pass.
    if (lastCacheValid_
        && from == lastFrom_ && to == lastTo_ && cur == lastCur_
        && in == lastIn_ && out == lastOut_ && loop == lastLoop_
        && fps == lastFps_ && playing == lastPlaying_) {
        return;
    }
```

Then remove the now-dead `tracks_->setTimelineRange(from, to);` call (it was at line 342, inside the `if (from != lastFrom_ || to != lastTo_)` block). Delete just that one line; keep `scrubber_->setRange(from, to);`.

- [ ] **Step 4: Build + manual verify**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`.
Launch, load sequences into one or two tracks. Expected:
- Loaded tracks show a gray bar with a lighter "loaded" fill and the filename label; the fill grows as frames decode.
- Empty tracks show the faint "(empty — drop or alt-click to load)" placeholder.
- A thin orange playhead line spans the rows and sits directly under the scrubber's playhead.

- [ ] **Step 5: Commit**

```bash
git add src/qt/TimelinePanel_qt.h src/qt/TimelinePanel_qt.cpp
git commit -m "qt timeline: render real per-track rows (range, loaded fill, label, playhead)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: `TimelineTracks_Qt` interactions

Add drag-to-offset, alt-click load-from-frame, right-click popup (hold-frame + set offset), and drop-to-load.

**Files:**
- Modify: `src/qt/TimelinePanel_qt.h` (`TimelineTracks_Qt` — add event overrides + drag state)
- Modify: `src/qt/TimelinePanel_qt.cpp` (`TimelineTracks_Qt` ctor + new event handlers)

- [ ] **Step 1: Declare the event handlers + drag state**

In `src/qt/TimelinePanel_qt.h`, in the `TimelineTracks_Qt` class, add to the `protected:` section (after `paintEvent`):

```cpp
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
```

and to the `private:` section:

```cpp
    int trackAtY(int y) const;       // which lane (0..3) contains y
    double pxPerFrame() const;       // pixels per timeline frame

    // Drag-to-offset state. Left-drag accumulates dx (vs dragPrevX_);
    // each whole-frame worth of motion steps the dragged track's offset
    // by +/-1. dragTrack_ == -1 means no gesture in progress.
    int    dragTrack_ = -1;
    double dragAccumPx_ = 0.0;
    int    dragPrevX_ = 0;
```

Add the needed forward declarations near the top of `TimelinePanel_qt.h` (alongside the existing `class QMouseEvent;`):

```cpp
class QContextMenuEvent;
class QDragEnterEvent;
class QDropEvent;
```

- [ ] **Step 2: Enable mouse/drop in the ctor**

In `src/qt/TimelinePanel_qt.cpp`, in the `TimelineTracks_Qt` constructor, add after `setAccessibleName("Timeline tracks");`:

```cpp
    setAcceptDrops(true);
    setCursor(Qt::SizeHorCursor);  // hint that horizontal drag = offset
```

- [ ] **Step 3: Add includes**

At the top of `src/qt/TimelinePanel_qt.cpp`, add to the include block:

```cpp
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMimeData>
#include <QUrl>
```

- [ ] **Step 4: Implement the helpers + mouse handlers**

In `src/qt/TimelinePanel_qt.cpp`, after the `TimelineTracks_Qt::paintEvent` implementation, add these five functions. The drag accumulator tracks the *delta* from `dragPrevX_` (seeded on press), so each whole frame-width of horizontal motion steps the offset by ±1:

```cpp
int TimelineTracks_Qt::trackAtY(int y) const {
    const int t = y / std::max(laneHeight(), 1);
    return std::clamp(t, 0, 3);
}

double TimelineTracks_Qt::pxPerFrame() const {
    const int span = std::max(to_ - from_, 1);
    const int usable = std::max(width() - 2 * kScrubberPad, 1);
    return std::max((double)usable / span, 0.001);
}

void TimelineTracks_Qt::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const int track = trackAtY((int)e->position().y());

    if (e->modifiers() & Qt::AltModifier) {
        // Alt+click: load the track's sequence from the clicked frame on.
        const int frame = frameFromXMapped((int)e->position().x(),
                                           width(), from_, to_);
        jefe::qt::startLoadingTrackAt(track, frame);
        return;
    }

    // Begin a drag-to-offset gesture on this track.
    dragTrack_ = track;
    dragAccumPx_ = 0.0;
    dragPrevX_ = (int)e->position().x();
}

void TimelineTracks_Qt::mouseMoveEvent(QMouseEvent* e) {
    if (dragTrack_ < 0 || !(e->buttons() & Qt::LeftButton)) return;
    const int x = (int)e->position().x();
    dragAccumPx_ += (x - dragPrevX_);
    dragPrevX_ = x;

    const double frameW = pxPerFrame();
    while (dragAccumPx_ >= frameW || dragAccumPx_ <= -frameW) {
        const int step = dragAccumPx_ > 0 ? 1 : -1;
        jefe::qt::setTrackOffset(dragTrack_,
                                 jefe::qt::getTrackOffset(dragTrack_) + step);
        dragAccumPx_ -= step * frameW;
    }
    refresh();
}

void TimelineTracks_Qt::mouseReleaseEvent(QMouseEvent* e) {
    Q_UNUSED(e);
    dragTrack_ = -1;
    dragAccumPx_ = 0.0;
}
```

- [ ] **Step 5: Right-click context menu**

Add after the mouse handlers:

```cpp
void TimelineTracks_Qt::contextMenuEvent(QContextMenuEvent* e) {
    const int track = trackAtY(e->y());
    QMenu menu(this);

    QAction* hold = menu.addAction("Hold last frame");
    hold->setCheckable(true);
    hold->setChecked(jefe::qt::getTrackHoldMode(track));

    QAction* setOff = menu.addAction("Set offset…");

    QAction* chosen = menu.exec(e->globalPos());
    if (chosen == hold) {
        jefe::qt::setTrackHoldMode(track, hold->isChecked());
        refresh();
    } else if (chosen == setOff) {
        bool ok = false;
        const int cur = jefe::qt::getTrackOffset(track);
        const int v = QInputDialog::getInt(this, "Track offset",
                          QString("Offset for track %1 (frames):")
                              .arg(QChar('A' + track)),
                          cur, -100000, 100000, 1, &ok);
        if (ok) {
            jefe::qt::setTrackOffset(track, v);
            refresh();
        }
    }
}
```

- [ ] **Step 6: Drag-and-drop load**

Add after the context menu handler:

```cpp
void TimelineTracks_Qt::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void TimelineTracks_Qt::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasUrls()) return;
    const int track = trackAtY((int)e->position().y());
    const QList<QUrl> urls = e->mimeData()->urls();
    if (urls.isEmpty()) return;
    const QString path = urls.first().toLocalFile();
    if (path.isEmpty()) return;
    // Reuse the standard load path, targeting this row's sequence.
    jefe::qt::loadFileIntoPlate(path.toStdString(), track, true, 1.0f);
    e->acceptProposedAction();
    refresh();
}
```

- [ ] **Step 7: Build + manual verify**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`.
Launch and verify each interaction:
- **Drag** a loaded track's row left/right → the bar shifts frame-by-frame; the viewport's displayed frame for that track changes accordingly.
- **Alt-click** at a point on a loaded (but not fully decoded) track → decoding starts; the loaded fill begins growing from around the clicked region.
- **Right-click** a row → menu appears; "Hold last frame" toggles and re-opens checked/unchecked correctly; "Set offset…" opens a dialog seeded with the current offset and applies the typed value.
- **Drag-drop** an image/sequence file onto a row → it loads into that track (bar + label appear).

- [ ] **Step 8: Commit**

```bash
git add src/qt/TimelinePanel_qt.h src/qt/TimelinePanel_qt.cpp
git commit -m "qt timeline: interactive track rows (drag-offset, alt-load, popup, drop)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Object names + Appium smoke test + docs

The track rows are painted (not child widgets), so Appium can't address sub-regions. The realistic automated check is: the tracks widget is present and addressable. Add the locator and a smoke test; record the widget in developer notes.

**Files:**
- Modify: `tests/ui/jefecheck/locators.py`
- Create: `tests/ui/test_track_timeline.py`
- Modify: `developer_notes.md`

- [ ] **Step 1: Add the locator**

In `tests/ui/jefecheck/locators.py`, add a constant alongside the other timeline locators (match the existing dotted-leaf style; the widget's objectName is already `"timeline.tracks"`):

```python
TIMELINE_TRACKS = "timeline.tracks"
```

(If a `TIMELINE_SCRUBBER = "timeline.scrubber"` style constant already exists, place this next to it for consistency.)

- [ ] **Step 2: Write the smoke test**

Create `tests/ui/test_track_timeline.py`:

```python
"""Track timeline widget presence.

The per-track timeline rows (range / offset / loaded fill) are painted
directly on a single QWidget rather than child widgets, so Appium can only
assert the lane container is present and addressable. Behavior (drag-offset,
alt-load, popup, drop) is verified manually per the implementation plan.
"""
from jefecheck import locators


def test_track_timeline_widget_present(app):
    """The timeline tracks lane container exists and is addressable."""
    tracks = app.by_object_name(locators.TIMELINE_TRACKS)
    assert tracks is not None
```

- [ ] **Step 3: Run the test (best-effort)**

Run: `cd tests/ui && python -m pytest test_track_timeline.py -v` (requires the Appium harness/sim; if the harness isn't running in this environment, confirm the test file imports cleanly with `python -c "import ast; ast.parse(open('tests/ui/test_track_timeline.py').read())"` and rely on the manual verification from Task 5).
Expected: PASS (widget present) when the harness is available.

- [ ] **Step 4: Document the widget**

In `developer_notes.md`, add a short section (after §13) describing the track timeline widget: that it's a single painted `QWidget` (not child widgets), that all sequence access goes through the new `jefe::qt::getTrackTimelineState`/`setTrackOffset`/`startLoadingTrackAt`/`setTrackHoldMode` bridge accessors (TU separation), that it shares the scrubber's pixel↔frame mapping, that left-drag is reassigned to offset (so scrubbing stays on the scrubber), and that `refresh()` runs before `refreshFromPlayback`'s fast-path so the loaded fill animates during decode.

- [ ] **Step 5: Commit**

```bash
git add tests/ui/jefecheck/locators.py tests/ui/test_track_timeline.py developer_notes.md
git commit -m "qt timeline: track-row locator + smoke test + developer notes

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Final review (after all tasks)

- Build green; all six commits present.
- Manual pass of every interaction in Task 5 Step 7.
- Open a PR against `qt-experimental` summarizing the widget and the new bridge accessors; squash-merge after review (matches the project's PR pattern).
- Update `memory/project_qt_plate_polish.md`: mark backlog item #4 done.
