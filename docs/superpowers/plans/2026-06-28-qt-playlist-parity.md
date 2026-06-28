# Qt Playlist Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the Qt6 playlist dock to FLTK parity — snapshot the current setup, rich collapsible per-item cards, drag-drop + keyboard, scale override — plus net-new auto-advance/loop.

**Architecture:** Pure Qt UI + glad-free `jefe::qt::*` bridge accessors; the playlist engine (`gfcPlaylistManager` / `gfcTrackManager`) already captures and restores full item state. UI TUs (`PlaylistPanel_qt`, the new card widget) never include rendering-chain/manager/glad headers (developer_notes §1); per-item detail crosses the boundary as a plain POD. Auto-advance end-of-playback detection lives in the bridge TU as a one-shot latch polled by MainWindow's existing idle tick.

**Tech Stack:** C++20, Qt6 Widgets, CMake. Build dir `build_qt`. Headless smoke tests via CLI flags.

## Global Constraints

- **TU separation (developer_notes §1):** only `SequenceLoadBridge_qt.cpp` includes managers/glad. `PlaylistPanel_qt.{h,cpp}` and `PlaylistItemCard` include only Qt + `SequenceLoadBridge_qt.h`. No `gfc*` headers in UI TUs.
- **No engine source changes** (`src/gfc*`). All new logic is in `src/qt/*` and `src/main_qt.cpp`. Auto-advance uses only existing public `gfcPlaybackManager` accessors.
- **Branch:** `qt/playlist-parity` (already created). Base for PR: `qt-experimental` (never `main`).
- **Bash:** no newlines in commands (`&&`/`;`). Commit trailer on every commit: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
- **Object names** follow the dotted-leaf scheme in `tests/ui/jefecheck/locators.py`.
- **Build:** `cmake --build build_qt -j8`. App binary: `build_qt/jefecheck.app/Contents/MacOS/jefecheck`.
- **Existing bridge facts:** `movePlaylistItem(index, +1)` = up, `-1` = down (already translated). `getSelectedPlaylistItem()` returns `playlistManager.selectedItem` (init `-1`). Compression enum order: `GFC_8BPC, GFC_16BPC, GFC_16HALF, GFC_4BPC, GFC_S3TCDX1`. `gfcLoadParams.scale` is a float (fraction ~1.0 = 100% or percent ~100; convert defensively).

---

### Task 1: Bridge — item creation + per-track detail accessors

Adds the "snapshot current", "add multiple files", "append tracks", and "detail" bridge calls, and extends the headless `--playlist-test` to prove add-current round-trips through `.jpl`.

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.h` (after line 397, in the playlist block)
- Modify: `src/qt/SequenceLoadBridge_qt.cpp` (after line 897)
- Modify: `src/main_qt.cpp:201-220` (extend the `--playlist-test` block)

**Interfaces:**
- Consumes: existing `playlistManager`, `trackManager` globals (already in this TU); existing `getPlaylistItemNames`, `clearPlaylist`, `savePlaylistFile`, `loadPlaylistFile`.
- Produces (other tasks rely on these exact names/types):
  - `struct jefe::qt::PlaylistTrackDetail { std::string letter, path; int fromFrame, toFrame, totalFrames, scalePct; std::string filter; bool crop; std::string bitDepth; };`
  - `void jefe::qt::addCurrentAsPlaylistItem();`
  - `void jefe::qt::addPlaylistFiles(const std::vector<std::string>& paths);`
  - `void jefe::qt::appendTracksToPlaylistItem(int index, const std::vector<std::string>& paths);`
  - `std::vector<jefe::qt::PlaylistTrackDetail> jefe::qt::getPlaylistItemDetail(int index);`

- [ ] **Step 1: Declare the POD + accessors in the header**

In `src/qt/SequenceLoadBridge_qt.h`, immediately after line 397 (`void loadPlaylistFile(const std::string& path);`), add:

```cpp
// Build a playlist item from the CURRENT live setup — every track's load
// params, the per-plate FX stacks, and the full program state (layout,
// playback mode/FPS, in/out, per-plate CC/flip/flop/crop/RGBA, per-track
// offset/hold). Mirrors gfcTrackManager::getPlaylistItem().
void addCurrentAsPlaylistItem();

// Build one multi-track item (A..D) from the given files and append it.
void addPlaylistFiles(const std::vector<std::string>& paths);

// Append more tracks to an existing item (drop-media-on-card).
void appendTracksToPlaylistItem(int index, const std::vector<std::string>& paths);

// Per-track detail for one item, for the card's collapsible body. Plain
// POD so the card TU stays glad-free (no gfc* headers). All fields derive
// from gfcLoadParams.
struct PlaylistTrackDetail {
    std::string letter;       // "A".."D"
    std::string path;         // full path; the panel shortens per toggle
    int  fromFrame = 0;
    int  toFrame = 0;
    int  totalFrames = 0;
    int  scalePct = 100;
    std::string filter;       // "linear" / "bilinear"
    bool crop = false;
    std::string bitDepth;     // "8"/"16"/"16f"/"32f"/"dxt1"
};
std::vector<PlaylistTrackDetail> getPlaylistItemDetail(int index);
```

- [ ] **Step 2: Implement the accessors in the .cpp**

In `src/qt/SequenceLoadBridge_qt.cpp`, immediately after line 897 (the closing brace of `loadPlaylistFile`), add (still inside `namespace jefe { namespace qt {` — confirm by checking the surrounding functions are not namespace-qualified, matching `addPlaylistFile`):

```cpp
void addCurrentAsPlaylistItem() {
    playlistManager.addItemlist(trackManager.getPlaylistItem());
}

void addPlaylistFiles(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    playlistManager.addItemlist(playlistManager.createPlaylistItemFrom(paths));
}

void appendTracksToPlaylistItem(int index, const std::vector<std::string>& paths) {
    auto* entries = playlistManager.getPlaylist();
    if (!entries || index < 0 || index >= (int)entries->size()) return;
    if (paths.empty()) return;
    playlistManager.appendTracksToItem(paths, index);
}

std::vector<PlaylistTrackDetail> getPlaylistItemDetail(int index) {
    std::vector<PlaylistTrackDetail> out;
    auto* entries = playlistManager.getPlaylist();
    if (!entries || index < 0 || index >= (int)entries->size()) return out;
    const gfcPlaylistItem& item = (*entries)[index];
    for (size_t i = 0; i < item.loadParams.size(); ++i) {
        const gfcLoadParams& lp = item.loadParams[i];
        PlaylistTrackDetail d;
        d.letter = std::string(1, char('A' + (int)i));
        d.path = lp.fileName;
        d.fromFrame = lp.fromFrame;
        d.toFrame = lp.toFrame;
        d.totalFrames = (lp.toFrame >= lp.fromFrame)
                        ? (lp.toFrame - lp.fromFrame + 1) : 0;
        // scale is a float: a fraction (1.0 == 100%) or already a percent.
        // Upsampling isn't supported, so anything > 1.5 is treated as a
        // percent value; otherwise it's a 0..1 fraction.
        d.scalePct = (lp.scale <= 1.5f)
                     ? int(lp.scale * 100.0f + 0.5f)
                     : int(lp.scale + 0.5f);
        d.filter = (lp.filterType == 0) ? "linear" : "bilinear";
        d.crop = lp.crop;
        switch (lp.compressed) {
            case GFC_8BPC:    d.bitDepth = "8";    break;
            case GFC_16BPC:   d.bitDepth = "16";   break;
            case GFC_16HALF:  d.bitDepth = "16f";  break;
            case GFC_4BPC:    d.bitDepth = "32f";  break;
            case GFC_S3TCDX1: d.bitDepth = "dxt1"; break;
            default:          d.bitDepth = "?";    break;
        }
        out.push_back(d);
    }
    return out;
}
```

- [ ] **Step 3: Extend `--playlist-test` to cover add-current**

In `src/main_qt.cpp`, replace the body of the `if (!playlistTestFile.isEmpty())` block (lines 204-219) with:

```cpp
        jefe::qt::clearPlaylist();
        jefe::qt::addPlaylistFile(playlistTestFile.toStdString());
        jefe::qt::addCurrentAsPlaylistItem();   // snapshot path (empty setup OK)
        const int added = int(jefe::qt::getPlaylistItemNames().size());
        const auto detail0 = jefe::qt::getPlaylistItemDetail(0);
        const std::string jpl =
            (QDir::tempPath() + "/jefecheck_playlist_test.jpl").toStdString();
        jefe::qt::savePlaylistFile(jpl);
        jefe::qt::clearPlaylist();
        const int afterClear = int(jefe::qt::getPlaylistItemNames().size());
        jefe::qt::loadPlaylistFile(jpl);
        const int afterLoad = int(jefe::qt::getPlaylistItemNames().size());
        const auto detailAfter = jefe::qt::getPlaylistItemDetail(0);
        const bool detailOk =
            !detail0.empty() && !detailAfter.empty() &&
            detailAfter[0].path == detail0[0].path &&
            detailAfter[0].fromFrame == detail0[0].fromFrame &&
            detailAfter[0].toFrame == detail0[0].toFrame;
        printf("PLAYLIST-TEST: added=%d afterClear=%d afterLoad=%d detailOk=%d file=%s\n",
               added, afterClear, afterLoad, detailOk ? 1 : 0, jpl.c_str());
        fflush(stdout);
        const bool ok = (added == 2 && afterClear == 0 && afterLoad == 2 && detailOk);
        std::_Exit(ok ? 0 : 2);
```

- [ ] **Step 4: Build**

Run: `cmake --build build_qt -j8 2>&1 | tail -2`
Expected: `[100%] Built target jefecheck` with no `error:` lines.

- [ ] **Step 5: Run the headless test to verify it passes**

Run: `build_qt/jefecheck.app/Contents/MacOS/jefecheck --playlist-test docs/manual-images/grayScaleLUT.png 2>&1 | grep PLAYLIST-TEST`
Expected: `PLAYLIST-TEST: added=2 afterClear=0 afterLoad=2 detailOk=1 file=...` and exit code 0 (`echo $?` → 0).

- [ ] **Step 6: Commit**

```bash
git add src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp src/main_qt.cpp
git commit -m "qt: playlist bridge — add-current, add-files, append, per-track detail

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Bridge — scale override, auto-advance latch, advance/play helpers

Adds the scale-override pass-through, the once-shot end-of-playback latch (updated inside `tickPlaybackTiming`), and the load-and-play / pause helpers the panel needs to advance.

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.h` (after the Task 1 additions)
- Modify: `src/qt/SequenceLoadBridge_qt.cpp` (inside `tickPlaybackTiming` at ~line 261-272, and new functions after the Task 1 additions)

**Interfaces:**
- Consumes: `playbackManager.{getCurrentFrame,getEndLimit,getPlaybackMode,isPlaying,startPlayFwd,getFromFrame,seekToFrame/setCurrentFrame}`, `LOOPMODEONCE_ID` (UIConstants.h, already included in this TU), existing `loadPlaylistItem`, `seekToFrame`, `togglePlayFwd`, `getSelectedPlaylistItem`.
- Produces:
  - `void jefe::qt::setPlaylistScaleOverride(int pct);` (0 = no override)
  - `bool jefe::qt::consumePlaylistAdvanceSignal();` (one-shot, edge-detected)
  - `bool jefe::qt::isPlaylistItemPlayingOnce();`
  - `void jefe::qt::loadPlaylistItemAndPlay(int index);`
  - `void jefe::qt::pausePlaybackIfPlaying();`

- [ ] **Step 1: Declare in the header**

In `src/qt/SequenceLoadBridge_qt.h`, after the Task 1 `getPlaylistItemDetail` declaration, add:

```cpp
// Scale override applied before each playlist load (RAM-limited / remote).
// pct in {25,50,100}; 0 clears the override. Maps to setScaleOverride.
void setPlaylistScaleOverride(int pct);

// Auto-advance support. consumePlaylistAdvanceSignal() returns true exactly
// once when forward playback reaches the end in ONCE mode (edge-detected in
// the playback tick); reading it clears the latch. isPlaylistItemPlayingOnce
// reports whether the current playback mode is ONCE.
bool consumePlaylistAdvanceSignal();
bool isPlaylistItemPlayingOnce();

// Load a playlist item and (re)start forward playback from its first frame —
// used by auto-advance. Normal double-click load stays loadPlaylistItem().
void loadPlaylistItemAndPlay(int index);

// Stop playback if currently playing (used to halt at the end of a non-looping
// playlist so the once-mode end-clamp doesn't spin the idle tick).
void pausePlaybackIfPlaying();
```

- [ ] **Step 2: Add the latch update inside `tickPlaybackTiming`**

In `src/qt/SequenceLoadBridge_qt.cpp`, find `tickPlaybackTiming()` (~line 261). It currently is:

```cpp
bool tickPlaybackTiming() {
    playbackManager.update();
    plateManager.updateAnimations();
    trackManager.updateTrackWidgets();
    return plateManager.getChanged();
}
```

Add a file-scope latch above the function and an edge check after `playbackManager.update();`. Replace the function with:

```cpp
namespace {
// Auto-advance edge state. ONCE mode clamps currentFrame at endLimit and
// leaves isPlaying() true (the playback manager never stops itself), so the
// "reached end" event is the transition prevFrame != endLimit -> currentFrame
// == endLimit while playing forward in ONCE mode. Latched here, consumed by
// the idle tick via consumePlaylistAdvanceSignal().
int  gPrevPlaybackFrame = -1;
bool gPlaylistAdvanceLatch = false;
}  // namespace

bool tickPlaybackTiming() {
    playbackManager.update();
    // Edge-detect once-mode end-of-playback for playlist auto-advance.
    if (playbackManager.isPlaying() &&
        playbackManager.getPlaybackMode() == LOOPMODEONCE_ID) {
        const int cur = playbackManager.getCurrentFrame();
        const int end = playbackManager.getEndLimit();
        if (cur == end && gPrevPlaybackFrame != end && gPrevPlaybackFrame >= 0) {
            gPlaylistAdvanceLatch = true;
        }
        gPrevPlaybackFrame = cur;
    } else {
        gPrevPlaybackFrame = playbackManager.getCurrentFrame();
    }
    plateManager.updateAnimations();
    trackManager.updateTrackWidgets();
    return plateManager.getChanged();
}
```

Note: `getEndLimit()` is the effective forward end boundary (public accessor, gfcplaybackmanager.h). The `gPrevPlaybackFrame >= 0` guard suppresses a spurious latch on the very first tick.

- [ ] **Step 3: Implement the remaining functions**

After the Task 1 additions in `src/qt/SequenceLoadBridge_qt.cpp`, add:

```cpp
void setPlaylistScaleOverride(int pct) {
    trackManager.setScaleOverride(pct);  // 0 = no override
}

bool consumePlaylistAdvanceSignal() {
    const bool v = gPlaylistAdvanceLatch;
    gPlaylistAdvanceLatch = false;
    return v;
}

bool isPlaylistItemPlayingOnce() {
    return playbackManager.getPlaybackMode() == LOOPMODEONCE_ID;
}

void loadPlaylistItemAndPlay(int index) {
    auto* entries = playlistManager.getPlaylist();
    if (!entries || index < 0 || index >= (int)entries->size()) return;
    trackManager.setPlaylistItem(playlistManager.getItem(index));
    playlistManager.setSelectedItem(index);
    playbackManager.setCurrentFrame(playbackManager.getFromFrame());
    plateManager.setChanged();
    gPrevPlaybackFrame = playbackManager.getCurrentFrame();
    playbackManager.startPlayFwd();
}

void pausePlaybackIfPlaying() {
    if (playbackManager.isPlaying()) playbackManager.pause();
}
```

Confirmed signatures (`src/gfcplaybackmanager.h`, all public): `setCurrentFrame(int)` (the seek; the `seekToFrame` bridge wrapper uses exactly this), `getFromFrame()`, `startPlayFwd()`, `pause()`, `isPlaying()`, `getCurrentFrame()`, `getPlaybackMode()`, and `getEndLimit()` (line 105).

- [ ] **Step 4: Build**

Run: `cmake --build build_qt -j8 2>&1 | tail -2`
Expected: `[100%] Built target jefecheck`, no `error:` lines.

- [ ] **Step 5: Re-run the headless playlist test (regression)**

Run: `build_qt/jefecheck.app/Contents/MacOS/jefecheck --playlist-test docs/manual-images/grayScaleLUT.png 2>&1 | grep PLAYLIST-TEST`
Expected: `added=2 afterClear=0 afterLoad=2 detailOk=1`, exit 0.

- [ ] **Step 6: Commit**

```bash
git add src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp
git commit -m "qt: playlist bridge — scale override + auto-advance latch + advance/play

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: `PlaylistItemCard` widget

A self-contained card widget (header with drag handle / index / name / track chips / collapse chevron, plus a collapsible per-track detail body). Lives in the panel's files. Glad-free — Qt + `SequenceLoadBridge_qt.h` only.

**Files:**
- Modify: `src/qt/PlaylistPanel_qt.h` (add the `PlaylistItemCard` class declaration above `PlaylistPanel_Qt`)
- Modify: `src/qt/PlaylistPanel_qt.cpp` (implement it; full rewrite happens in Task 4 — here add the card type and its impl)

**Interfaces:**
- Consumes: `jefe::qt::PlaylistTrackDetail`, `jefe::qt::getPlaylistItemDetail(int)` (Task 1).
- Produces:
  - `class PlaylistItemCard : public QWidget` with:
    - ctor `PlaylistItemCard(int index, const QString& name, bool expanded, bool fullPaths, QWidget* parent=nullptr)`
    - `void setExpanded(bool)` / `bool isExpanded() const`
    - `void setSelectedHighlight(bool)`
    - signals: `void loadRequested(int index)`, `void removeRequested(int index)`, `void toggleExpandRequested(int index)`

- [ ] **Step 1: Declare the card in the header**

In `src/qt/PlaylistPanel_qt.h`, replace the forward-declares block (lines 16-19) and add the card class. After `#include <QWidget>` add `#include <QString>`, keep the forward declarations, and before `class PlaylistPanel_Qt` insert:

```cpp
class QLabel;
class QToolButton;
class QVBoxLayout;

// One playlist row: header (drag handle, index, name, track chips, collapse
// chevron) + a collapsible per-track detail body. Dumb widget — it emits
// intent signals; PlaylistPanel_Qt does the bridge calls.
class PlaylistItemCard : public QWidget {
    Q_OBJECT
public:
    PlaylistItemCard(int index, const QString& name, bool expanded,
                     bool fullPaths, QWidget* parent = nullptr);
    void setExpanded(bool on);
    bool isExpanded() const { return expanded_; }
    void setSelectedHighlight(bool on);

signals:
    void loadRequested(int index);
    void removeRequested(int index);
    void toggleExpandRequested(int index);

private:
    void rebuildDetail();
    int index_;
    bool expanded_;
    bool fullPaths_;
    QToolButton* chevron_ = nullptr;
    QWidget* detail_ = nullptr;
    QVBoxLayout* detailLayout_ = nullptr;
};
```

- [ ] **Step 2: Implement the card in the .cpp**

At the top of `src/qt/PlaylistPanel_qt.cpp`, ensure these includes are present (add any missing): `#include <QToolButton>`, `#include <QFileInfo>`, `#include <QFrame>`. Then add the implementation (place it after the anonymous namespace, before the `PlaylistPanel_Qt` ctor):

```cpp
PlaylistItemCard::PlaylistItemCard(int index, const QString& name, bool expanded,
                                   bool fullPaths, QWidget* parent)
    : QWidget(parent), index_(index), expanded_(expanded), fullPaths_(fullPaths) {
    setObjectName(QString("playlist.card.%1").arg(index));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 2, 4, 2);
    outer->setSpacing(2);

    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(6);

    auto* handle = new QLabel("☰", this);  // ☰ drag affordance
    handle->setToolTip("Drag to reorder");
    handle->setStyleSheet("color:#888;");
    header->addWidget(handle);

    auto* idx = new QLabel(QString::number(index + 1), this);
    idx->setMinimumWidth(18);
    header->addWidget(idx);

    auto* nameLab = new QLabel(name, this);
    nameLab->setObjectName(QString("playlist.card.%1.name").arg(index));
    nameLab->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    nameLab->setToolTip(name);
    header->addWidget(nameLab, /*stretch*/ 1);

    chevron_ = new QToolButton(this);
    chevron_->setObjectName(QString("playlist.card.%1.chevron").arg(index));
    chevron_->setAutoRaise(true);
    chevron_->setText(expanded_ ? "▾" : "▸");  // ▾ / ▸
    connect(chevron_, &QToolButton::clicked, this,
            [this]() { emit toggleExpandRequested(index_); });
    header->addWidget(chevron_);

    auto* removeBtn = new QToolButton(this);
    removeBtn->setObjectName(QString("playlist.card.%1.remove").arg(index));
    removeBtn->setText("✕");  // ✕
    removeBtn->setAutoRaise(true);
    removeBtn->setToolTip("Remove from playlist");
    connect(removeBtn, &QToolButton::clicked, this,
            [this]() { emit removeRequested(index_); });
    header->addWidget(removeBtn);

    outer->addLayout(header);

    detail_ = new QWidget(this);
    detailLayout_ = new QVBoxLayout(detail_);
    detailLayout_->setContentsMargins(28, 0, 0, 0);
    detailLayout_->setSpacing(0);
    outer->addWidget(detail_);

    rebuildDetail();
    detail_->setVisible(expanded_);
}

void PlaylistItemCard::rebuildDetail() {
    // Clear existing rows.
    QLayoutItem* it;
    while ((it = detailLayout_->takeAt(0)) != nullptr) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    for (const auto& d : jefe::qt::getPlaylistItemDetail(index_)) {
        QString path = QString::fromStdString(d.path);
        if (!fullPaths_) path = QFileInfo(path).fileName();
        QString line = QString("%1  %2  %3-%4 (%5)  %6%%  %7%8  %9")
            .arg(QString::fromStdString(d.letter))
            .arg(path)
            .arg(d.fromFrame).arg(d.toFrame).arg(d.totalFrames)
            .arg(d.scalePct)
            .arg(QString::fromStdString(d.filter))
            .arg(d.crop ? "  crop" : "")
            .arg(QString::fromStdString(d.bitDepth));
        auto* row = new QLabel(line, detail_);
        row->setStyleSheet("color:#aaa; font-size:11px;");
        detailLayout_->addWidget(row);
    }
}

void PlaylistItemCard::setExpanded(bool on) {
    expanded_ = on;
    if (chevron_) chevron_->setText(on ? "▾" : "▸");
    if (detail_) detail_->setVisible(on);
    updateGeometry();
}

void PlaylistItemCard::setSelectedHighlight(bool on) {
    setStyleSheet(on ? "background:#33405a; border-radius:3px;" : "");
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build_qt -j8 2>&1 | tail -2`
Expected: builds clean. (Task 4 wires the card into the panel; for now it compiles as an unused type if the panel still uses the old list — that's fine, but if the linker prunes nothing this still must compile. If MOC complains about the new Q_OBJECT, ensure the file is in the Qt build with AUTOMOC — it already is.)

- [ ] **Step 4: Commit**

```bash
git add src/qt/PlaylistPanel_qt.h src/qt/PlaylistPanel_qt.cpp
git commit -m "qt: PlaylistItemCard widget (collapsible per-track detail + drag handle)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: `PlaylistPanel_Qt` rewrite — cards, toolbar, toggles, scale override, drag-drop, keyboard, context menu

Replaces the basename list with the card list and wires every interaction. This is the largest task; it ends with a build-green gate and a manual GUI checklist.

**Files:**
- Modify: `src/qt/PlaylistPanel_qt.h` (extend `PlaylistPanel_Qt`)
- Modify: `src/qt/PlaylistPanel_qt.cpp` (rewrite the panel; keep the Task 3 card impl)

**Interfaces:**
- Consumes: all Task 1/2 bridge calls + existing (`getPlaylistItemNames`, `removePlaylistItem`, `movePlaylistItem`, `clearPlaylist`, `loadPlaylistItem`, `getSelectedPlaylistItem`, `savePlaylistFile`, `loadPlaylistFile`); `PlaylistItemCard` (Task 3).
- Produces: `void PlaylistPanel_Qt::advanceToNext();` (public slot — called by MainWindow in Task 5); `void refreshList();` (existing, now rebuilds cards).

- [ ] **Step 1: Extend the panel header**

In `src/qt/PlaylistPanel_qt.h`, replace the `PlaylistPanel_Qt` class body (lines 21-48) with:

```cpp
class QListWidget;
class QCheckBox;
class QComboBox;

class PlaylistPanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistPanel_Qt(QWidget* parent = nullptr);

public slots:
    void refreshList();
    void advanceToNext();   // called by the idle tick when auto-advance fires

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;  // list keyboard + drop

private:
    void onAddCurrent();
    void onAddFiles();
    void onRemoveClicked();
    void onUpClicked();
    void onDownClicked();
    void onClearClicked();
    void onSaveClicked();
    void onLoadClicked();
    void onRowsMoved();                 // InternalMove -> movePlaylistItem
    void loadRow(int row);
    void applyScaleOverride();
    void showContextMenu(const QPoint& pos);
    int  selectedRow() const;

    QListWidget* list_ = nullptr;
    QCheckBox* compactCheck_ = nullptr;
    QCheckBox* fullPathsCheck_ = nullptr;
    QCheckBox* autoAdvanceCheck_ = nullptr;
    QCheckBox* loopCheck_ = nullptr;
    QCheckBox* scaleOverrideCheck_ = nullptr;
    QComboBox* scaleCombo_ = nullptr;
    class QLabel* status_ = nullptr;
    bool reordering_ = false;           // guard re-entrant refresh during move
};
```

- [ ] **Step 2: Rewrite the panel ctor + helpers**

In `src/qt/PlaylistPanel_qt.cpp`, add includes: `#include <QCheckBox>`, `#include <QComboBox>`, `#include <QMenu>`, `#include <QKeyEvent>`, `#include <QDropEvent>`, `#include <QMimeData>`, `#include <QDir>`. Replace the entire `PlaylistPanel_Qt::PlaylistPanel_Qt(...)` ctor and the `refreshList()`/`onAddClicked()`/`onItemDoubleClicked()` methods (everything from the old ctor through end of file, but KEEP the `PlaylistItemCard` impl and the anonymous namespace) with:

```cpp
namespace {
constexpr const char* kCompactKey   = "Playlist/compactView";
constexpr const char* kFullPathsKey = "Playlist/showFullPaths";
constexpr const char* kAutoAdvKey   = "Playlist/autoAdvance";
constexpr const char* kLoopKey      = "Playlist/loop";
constexpr const char* kScaleOnKey   = "Playlist/scaleOverrideOn";
constexpr const char* kScaleValKey  = "Playlist/scaleOverridePct";
}

PlaylistPanel_Qt::PlaylistPanel_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("playlist.panel");
    QSettings s;

    list_ = new QListWidget(this);
    list_->setObjectName("playlist.list");
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setAcceptDrops(true);
    list_->viewport()->setAcceptDrops(true);
    list_->installEventFilter(this);
    list_->viewport()->installEventFilter(this);
    connect(list_, &QListWidget::customContextMenuRequested,
            this, &PlaylistPanel_Qt::showContextMenu);
    connect(list_->model(), &QAbstractItemModel::rowsMoved,
            this, [this](const QModelIndex&, int, int, const QModelIndex&, int) {
                onRowsMoved();
            });

    auto mkBtn = [this](const char* text, const char* obj, const char* tip,
                        void (PlaylistPanel_Qt::*slot)()) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(obj);
        if (tip) b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, slot);
        return b;
    };
    auto* addCurBtn = mkBtn("Add Current", "playlist.button.addcurrent",
        "Snapshot the current setup as a playlist item",
        &PlaylistPanel_Qt::onAddCurrent);
    auto* addFilesBtn = mkBtn("Add Files…", "playlist.button.addfiles",
        "Build an item from one or more files", &PlaylistPanel_Qt::onAddFiles);
    auto* removeBtn = mkBtn("Remove", "playlist.button.remove", nullptr,
        &PlaylistPanel_Qt::onRemoveClicked);
    auto* upBtn = mkBtn("↑", "playlist.button.up", "Move selected up",
        &PlaylistPanel_Qt::onUpClicked);
    auto* downBtn = mkBtn("↓", "playlist.button.down", "Move selected down",
        &PlaylistPanel_Qt::onDownClicked);
    auto* clearBtn = mkBtn("Clear", "playlist.button.clear", nullptr,
        &PlaylistPanel_Qt::onClearClicked);
    auto* loadBtn = mkBtn("Load…", "playlist.button.load",
        "Load a .jpl playlist", &PlaylistPanel_Qt::onLoadClicked);
    auto* saveBtn = mkBtn("Save…", "playlist.button.save",
        "Save the playlist to a .jpl", &PlaylistPanel_Qt::onSaveClicked);

    compactCheck_ = new QCheckBox("Compact", this);
    compactCheck_->setObjectName("playlist.check.compact");
    compactCheck_->setChecked(s.value(kCompactKey, true).toBool());
    connect(compactCheck_, &QCheckBox::toggled, this, [this](bool on) {
        QSettings st; st.setValue(kCompactKey, on); refreshList();
    });

    fullPathsCheck_ = new QCheckBox("Full paths", this);
    fullPathsCheck_->setObjectName("playlist.check.fullpaths");
    fullPathsCheck_->setChecked(s.value(kFullPathsKey, false).toBool());
    connect(fullPathsCheck_, &QCheckBox::toggled, this, [this](bool on) {
        QSettings st; st.setValue(kFullPathsKey, on); refreshList();
    });

    autoAdvanceCheck_ = new QCheckBox("Auto-advance", this);
    autoAdvanceCheck_->setObjectName("playlist.check.autoadvance");
    autoAdvanceCheck_->setChecked(s.value(kAutoAdvKey, false).toBool());
    connect(autoAdvanceCheck_, &QCheckBox::toggled, this, [](bool on) {
        QSettings st; st.setValue(kAutoAdvKey, on);
    });

    loopCheck_ = new QCheckBox("Loop playlist", this);
    loopCheck_->setObjectName("playlist.check.loop");
    loopCheck_->setChecked(s.value(kLoopKey, false).toBool());
    connect(loopCheck_, &QCheckBox::toggled, this, [](bool on) {
        QSettings st; st.setValue(kLoopKey, on);
    });

    scaleOverrideCheck_ = new QCheckBox("Scale override", this);
    scaleOverrideCheck_->setObjectName("playlist.check.scaleoverride");
    scaleOverrideCheck_->setChecked(s.value(kScaleOnKey, false).toBool());
    scaleCombo_ = new QComboBox(this);
    scaleCombo_->setObjectName("playlist.combo.scale");
    scaleCombo_->addItem("100", 100);
    scaleCombo_->addItem("50", 50);
    scaleCombo_->addItem("25", 25);
    {
        int idx = scaleCombo_->findData(s.value(kScaleValKey, 100).toInt());
        scaleCombo_->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    connect(scaleOverrideCheck_, &QCheckBox::toggled, this, [this](bool on) {
        QSettings st; st.setValue(kScaleOnKey, on); applyScaleOverride();
    });
    connect(scaleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        QSettings st; st.setValue(kScaleValKey, scaleCombo_->currentData().toInt());
        applyScaleOverride();
    });

    status_ = new QLabel(this);
    status_->setObjectName("playlist.status.label");
    status_->setStyleSheet("color:#888; font-style:italic;");

    auto* row1 = new QHBoxLayout();
    row1->setContentsMargins(0, 0, 0, 0);
    row1->addWidget(addCurBtn);
    row1->addWidget(addFilesBtn);
    row1->addWidget(removeBtn);
    row1->addWidget(upBtn);
    row1->addWidget(downBtn);
    row1->addWidget(clearBtn);
    row1->addStretch(1);
    row1->addWidget(loadBtn);
    row1->addWidget(saveBtn);

    auto* row2 = new QHBoxLayout();
    row2->setContentsMargins(0, 0, 0, 0);
    row2->addWidget(compactCheck_);
    row2->addWidget(fullPathsCheck_);
    row2->addStretch(1);
    row2->addWidget(scaleOverrideCheck_);
    row2->addWidget(scaleCombo_);

    auto* row3 = new QHBoxLayout();
    row3->setContentsMargins(0, 0, 0, 0);
    row3->addWidget(autoAdvanceCheck_);
    row3->addWidget(loopCheck_);
    row3->addStretch(1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);
    outer->addWidget(list_, 1);
    outer->addLayout(row1);
    outer->addLayout(row2);
    outer->addLayout(row3);
    outer->addWidget(status_);

    applyScaleOverride();   // seed override from restored state
    refreshList();
}

void PlaylistPanel_Qt::applyScaleOverride() {
    const int pct = scaleOverrideCheck_->isChecked()
                    ? scaleCombo_->currentData().toInt() : 0;
    jefe::qt::setPlaylistScaleOverride(pct);
}

int PlaylistPanel_Qt::selectedRow() const {
    return list_ ? list_->currentRow() : -1;
}

void PlaylistPanel_Qt::refreshList() {
    if (reordering_) return;
    const int prev = list_->currentRow();
    list_->clear();
    const auto names = jefe::qt::getPlaylistItemNames();
    const bool expanded = !compactCheck_->isChecked();
    const bool fullPaths = fullPathsCheck_->isChecked();
    const int selected = jefe::qt::getSelectedPlaylistItem();
    for (int i = 0; i < (int)names.size(); ++i) {
        auto* card = new PlaylistItemCard(
            i, QString::fromStdString(names[i]), expanded, fullPaths, list_);
        card->setSelectedHighlight(i == selected);
        connect(card, &PlaylistItemCard::loadRequested,
                this, &PlaylistPanel_Qt::loadRow);
        connect(card, &PlaylistItemCard::removeRequested, this, [this](int r) {
            jefe::qt::removePlaylistItem(r); refreshList();
        });
        connect(card, &PlaylistItemCard::toggleExpandRequested, this,
                [this](int r) {
            if (auto* it = list_->item(r))
                if (auto* c = qobject_cast<PlaylistItemCard*>(
                        list_->itemWidget(it))) {
                    c->setExpanded(!c->isExpanded());
                    it->setSizeHint(QSize(0, c->sizeHint().height()));
                }
        });
        auto* it = new QListWidgetItem(list_);
        it->setSizeHint(QSize(0, card->sizeHint().height()));
        list_->setItemWidget(it, card);
    }
    status_->setText(names.empty()
        ? QString("Playlist is empty.")
        : QString("%1 items").arg(names.size()));
    if (prev >= 0 && prev < list_->count()) list_->setCurrentRow(prev);
}

void PlaylistPanel_Qt::onAddCurrent() {
    jefe::qt::addCurrentAsPlaylistItem();
    refreshList();
    list_->setCurrentRow(list_->count() - 1);
}

void PlaylistPanel_Qt::onAddFiles() {
    QSettings s;
    const QString seed = s.value("Playlist/lastAddDir", QDir::homePath()).toString();
    const QStringList chosen = QFileDialog::getOpenFileNames(
        this, "Add files to playlist", seed,
        "Image sequences (*.dpx *.exr *.jpg *.jpeg *.png *.tif *.tiff *.tga *.bmp);;"
        "All files (*)");
    if (chosen.isEmpty()) return;
    s.setValue("Playlist/lastAddDir", QFileInfo(chosen.first()).absolutePath());
    std::vector<std::string> paths;
    for (const auto& p : chosen) paths.push_back(p.toStdString());
    jefe::qt::addPlaylistFiles(paths);
    refreshList();
    list_->setCurrentRow(list_->count() - 1);
}

void PlaylistPanel_Qt::onRemoveClicked() {
    const int r = selectedRow();
    if (r < 0) return;
    jefe::qt::removePlaylistItem(r);
    refreshList();
}

void PlaylistPanel_Qt::onUpClicked() {
    const int r = selectedRow();
    if (r <= 0) return;
    jefe::qt::movePlaylistItem(r, -1);
    refreshList();
    list_->setCurrentRow(r - 1);
}

void PlaylistPanel_Qt::onDownClicked() {
    const int r = selectedRow();
    if (r < 0 || r >= list_->count() - 1) return;
    jefe::qt::movePlaylistItem(r, +1);
    refreshList();
    list_->setCurrentRow(r + 1);
}

void PlaylistPanel_Qt::onClearClicked() {
    jefe::qt::clearPlaylist();
    refreshList();
}

void PlaylistPanel_Qt::onSaveClicked() {
    QSettings s;
    const QString seed = s.value("Playlist/lastAddDir", QDir::homePath()).toString();
    QString chosen = QFileDialog::getSaveFileName(
        this, "Save playlist", seed, "JefeCheck playlist (*.jpl)");
    if (chosen.isEmpty()) return;
    s.setValue("Playlist/lastAddDir", QFileInfo(chosen).absolutePath());
    jefe::qt::savePlaylistFile(chosen.toStdString());
    status_->setText(QString("Saved %1 items").arg(list_->count()));
}

void PlaylistPanel_Qt::onLoadClicked() {
    QSettings s;
    const QString seed = s.value("Playlist/lastAddDir", QDir::homePath()).toString();
    const QString chosen = QFileDialog::getOpenFileName(
        this, "Load playlist", seed, "JefeCheck playlist (*.jpl);;All files (*)");
    if (chosen.isEmpty()) return;
    s.setValue("Playlist/lastAddDir", QFileInfo(chosen).absolutePath());
    jefe::qt::loadPlaylistFile(chosen.toStdString());
    refreshList();
}

void PlaylistPanel_Qt::onRowsMoved() {
    // InternalMove already reordered the view; mirror it into the manager.
    // QListWidget InternalMove is a remove+insert; translate to repeated
    // single-step moves so the manager's vector matches the view order.
    // Simplest correct approach: rebuild manager order from current view by
    // moving one step at a time is unnecessary — instead capture the moved
    // row via selection and apply the net move.
    // We use a guard so the rebuild from refreshList doesn't re-enter.
    reordering_ = true;
    // Reconstruct intended order: the view is authoritative. Walk the view
    // and bubble each item into place in the manager with movePlaylistItem.
    // Because cards are recreated on refresh, we re-derive order from the
    // manager after applying moves, then refresh.
    reordering_ = false;
    refreshList();
}

void PlaylistPanel_Qt::loadRow(int row) {
    if (row < 0 || row >= list_->count()) return;
    jefe::qt::loadPlaylistItem(row);
    refreshList();   // refresh highlight
}

void PlaylistPanel_Qt::advanceToNext() {
    const int cur = jefe::qt::getSelectedPlaylistItem();
    const int count = (int)jefe::qt::getPlaylistItemNames().size();
    if (count <= 0) { jefe::qt::pausePlaybackIfPlaying(); return; }
    int next = cur + 1;
    if (next >= count) {
        if (loopCheck_->isChecked()) next = 0;
        else { jefe::qt::pausePlaybackIfPlaying(); return; }
    }
    if (next < 0) next = 0;
    jefe::qt::loadPlaylistItemAndPlay(next);
    refreshList();
}

void PlaylistPanel_Qt::showContextMenu(const QPoint& pos) {
    const int r = list_->currentRow();
    QMenu menu(this);
    QAction* load = menu.addAction("Load");
    QAction* append = menu.addAction("Append tracks…");
    QAction* remove = menu.addAction("Remove");
    menu.addSeparator();
    QAction* up = menu.addAction("Move up");
    QAction* down = menu.addAction("Move down");
    menu.addSeparator();
    QAction* full = menu.addAction("Show full paths");
    full->setCheckable(true); full->setChecked(fullPathsCheck_->isChecked());
    QAction* compact = menu.addAction("Compact view");
    compact->setCheckable(true); compact->setChecked(compactCheck_->isChecked());
    QAction* picked = menu.exec(list_->viewport()->mapToGlobal(pos));
    if (!picked) return;
    if (picked == load && r >= 0) loadRow(r);
    else if (picked == remove && r >= 0) { jefe::qt::removePlaylistItem(r); refreshList(); }
    else if (picked == up) onUpClicked();
    else if (picked == down) onDownClicked();
    else if (picked == full) fullPathsCheck_->setChecked(full->isChecked());
    else if (picked == compact) compactCheck_->setChecked(compact->isChecked());
    else if (picked == append && r >= 0) {
        QSettings s;
        const QString seed = s.value("Playlist/lastAddDir", QDir::homePath()).toString();
        const QStringList files = QFileDialog::getOpenFileNames(
            this, "Append tracks", seed, "All files (*)");
        if (files.isEmpty()) return;
        std::vector<std::string> paths;
        for (const auto& p : files) paths.push_back(p.toStdString());
        jefe::qt::appendTracksToPlaylistItem(r, paths);
        refreshList();
    }
}

bool PlaylistPanel_Qt::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::KeyPress &&
        (obj == list_ || obj == list_->viewport())) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        const int r = selectedRow();
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (r >= 0) { loadRow(r); return true; }
        } else if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
            if (r >= 0) { jefe::qt::removePlaylistItem(r); refreshList(); return true; }
        } else if ((ke->modifiers() & Qt::ShiftModifier) &&
                   ke->key() == Qt::Key_Up) {
            onUpClicked(); return true;
        } else if ((ke->modifiers() & Qt::ShiftModifier) &&
                   ke->key() == Qt::Key_Down) {
            onDownClicked(); return true;
        }
    }
    if (ev->type() == QEvent::Drop &&
        (obj == list_ || obj == list_->viewport())) {
        auto* de = static_cast<QDropEvent*>(ev);
        if (de->mimeData()->hasUrls()) {
            QStringList media;
            QString jpl;
            for (const auto& u : de->mimeData()->urls()) {
                const QString lf = u.toLocalFile();
                if (lf.endsWith(".jpl", Qt::CaseInsensitive)) jpl = lf;
                else if (!lf.isEmpty()) media << lf;
            }
            if (!jpl.isEmpty()) {
                jefe::qt::loadPlaylistFile(jpl.toStdString());
                refreshList(); de->acceptProposedAction(); return true;
            }
            if (!media.isEmpty()) {
                // Drop on a card -> append to that item; else new item.
                QListWidgetItem* it = list_->itemAt(
                    de->position().toPoint());
                std::vector<std::string> paths;
                for (const auto& p : media) paths.push_back(p.toStdString());
                if (it) jefe::qt::appendTracksToPlaylistItem(
                            list_->row(it), paths);
                else jefe::qt::addPlaylistFiles(paths);
                refreshList(); de->acceptProposedAction(); return true;
            }
        }
    }
    return QWidget::eventFilter(obj, ev);
}
```

> **Reorder note:** `QListWidget` `InternalMove` reorders the *view* but the manager vector must follow. Because deriving the exact net move from `rowsMoved` indices for a remove+insert model is error-prone, this task implements drag-reorder as **view-then-resync**: after a drag, `onRowsMoved` does nothing destructive and calls `refreshList()`, which **rebuilds the view from the manager** — so the visual drop is reverted to manager order. To make drag-reorder actually persist, disable view-only DnD and rely on the ↑/↓ buttons + Shift+↑/↓ for reordering (which call `movePlaylistItem`). Set `list_->setDragDropMode(QAbstractItemView::DropOnly)` instead of `InternalMove` (keeps the `☰` handle as affordance for buttons, accepts external file drops, and avoids the view/manager divergence). **Implement DropOnly.** Drag-to-reorder-by-handle is deferred; reordering is via buttons/keyboard. Update the `setDragDropMode` line accordingly and delete the `rowsMoved` connect + `onRowsMoved`.

- [ ] **Step 3: Apply the reorder decision**

Per the note above, in the ctor change `list_->setDragDropMode(QAbstractItemView::InternalMove);` to `list_->setDragDropMode(QAbstractItemView::DropOnly);`, remove the `connect(list_->model(), &QAbstractItemModel::rowsMoved, ...)` block, and delete the `onRowsMoved()` method and its declaration in the header (and the `reordering_` guard usage in `refreshList` may stay as a harmless no-op or be removed).

- [ ] **Step 4: Build**

Run: `cmake --build build_qt -j8 2>&1 | tail -3`
Expected: `[100%] Built target jefecheck`, no `error:` lines.

- [ ] **Step 5: Headless regression**

Run: `build_qt/jefecheck.app/Contents/MacOS/jefecheck --playlist-test docs/manual-images/grayScaleLUT.png 2>&1 | grep PLAYLIST-TEST; echo exit=$?`
Expected: `added=2 afterClear=0 afterLoad=2 detailOk=1`, `exit=0`.

- [ ] **Step 6: Manual GUI smoke (launch, then quit)**

Run: `open build_qt/jefecheck.app`
Verify by hand: Add Current adds a card; Add Files… builds a multi-track card; Compact toggles all detail blocks; per-card chevron toggles one; Full paths switches filename rendering; ↑/↓ + Shift+↑/↓ reorder; right-click menu works; drop a file on the list adds an item / on a card appends; drop a `.jpl` loads it; Scale override + combo persist after relaunch.
Then: `pkill -f jefecheck.app`

- [ ] **Step 7: Commit**

```bash
git add src/qt/PlaylistPanel_qt.h src/qt/PlaylistPanel_qt.cpp
git commit -m "qt: playlist panel — card list, toolbar, toggles, scale override, drag-drop, keyboard

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: MainWindow idle-tick auto-advance poll

Polls the bridge's one-shot advance signal on the existing throttled (~60 Hz) section of the playback timer and drives the panel when armed.

**Files:**
- Modify: `src/qt/MainWindow_qt.cpp` (inside the `playbackTimer_` lambda, in the throttled block after line 333)

**Interfaces:**
- Consumes: `jefe::qt::consumePlaylistAdvanceSignal()` (Task 2), `PlaylistPanel_Qt::advanceToNext()` (Task 4), the existing `playlistPanelWidget_` member, the `autoAdvanceCheck_` state (read inside `advanceToNext`).

- [ ] **Step 1: Add the poll**

In `src/qt/MainWindow_qt.cpp`, inside the `playbackTimer_` timeout lambda, after `uiRefreshCounter_ = 0;` (line 334) and before the `if (timelinePanelWidget_)` block, add:

```cpp
        // Playlist auto-advance: the bridge latches a one-shot when forward
        // ONCE-mode playback hits the end; the panel decides whether it's
        // armed and what "next" is. Cheap: a bool read on most ticks.
        if (jefe::qt::consumePlaylistAdvanceSignal() && playlistPanelWidget_) {
            playlistPanelWidget_->advanceToNext();
        }
```

Note: `advanceToNext()` itself early-returns when auto-advance is unchecked? It does not — gate here on the checkbox by having `advanceToNext` consult it. To keep the check in one place, modify `advanceToNext` (Task 4) to early-return if `!autoAdvanceCheck_->isChecked()`. Add that guard as the first line of `advanceToNext`:

```cpp
    if (!autoAdvanceCheck_ || !autoAdvanceCheck_->isChecked()) return;
```

- [ ] **Step 2: Confirm the include/member exists**

`playlistPanelWidget_` is already a `PlaylistPanel_Qt*` member used at MainWindow_qt.cpp:811. `PlaylistPanel_qt.h` is already included by MainWindow. No new include needed.

- [ ] **Step 3: Build**

Run: `cmake --build build_qt -j8 2>&1 | tail -2`
Expected: `[100%] Built target jefecheck`.

- [ ] **Step 4: Manual auto-advance check**

Run: `open build_qt/jefecheck.app`
By hand: add two items (Add Current twice with different footage, or Add Files…), enable **Auto-advance**, set loop mode to **Once**, double-click item 1, press play; at end it should load item 2 and continue. With **Loop playlist** on, item 2's end wraps to item 1. With it off, it stops after the last. Then `pkill -f jefecheck.app`.

- [ ] **Step 5: Commit**

```bash
git add src/qt/MainWindow_qt.cpp src/qt/PlaylistPanel_qt.cpp
git commit -m "qt: idle-tick polls playlist auto-advance signal; gate in advanceToNext

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Locators + docs

Update the UI test locators and the two doc files.

**Files:**
- Modify: `tests/ui/jefecheck/locators.py`
- Modify: `developer_notes.md` (new section)
- Modify: `CLAUDE.md` (UI/playlist description)

- [ ] **Step 1: Update locators**

In `tests/ui/jefecheck/locators.py`, find the playlist locators (the old `playlist.add.button` / `playlist.list` etc.) and replace/extend them with the new object names:

```python
PLAYLIST_PANEL          = "playlist.panel"
PLAYLIST_LIST           = "playlist.list"
PLAYLIST_ADD_CURRENT    = "playlist.button.addcurrent"
PLAYLIST_ADD_FILES      = "playlist.button.addfiles"
PLAYLIST_REMOVE         = "playlist.button.remove"
PLAYLIST_UP             = "playlist.button.up"
PLAYLIST_DOWN           = "playlist.button.down"
PLAYLIST_CLEAR          = "playlist.button.clear"
PLAYLIST_LOAD           = "playlist.button.load"
PLAYLIST_SAVE           = "playlist.button.save"
PLAYLIST_COMPACT        = "playlist.check.compact"
PLAYLIST_FULLPATHS      = "playlist.check.fullpaths"
PLAYLIST_AUTOADVANCE    = "playlist.check.autoadvance"
PLAYLIST_LOOP           = "playlist.check.loop"
PLAYLIST_SCALEOVERRIDE  = "playlist.check.scaleoverride"
PLAYLIST_SCALECOMBO     = "playlist.combo.scale"
PLAYLIST_STATUS         = "playlist.status.label"
# Per-card leaves: playlist.card.<i>, .name, .chevron, .remove
```

- [ ] **Step 2: developer_notes.md — add a section**

Append a new section to `developer_notes.md` documenting: the playlist panel card pattern (mirrors the FX panel), the glad-free `PlaylistTrackDetail` POD boundary, the auto-advance latch in `tickPlaybackTiming` + `consumePlaylistAdvanceSignal` one-shot, the DropOnly reorder decision (buttons/keyboard reorder; handle is affordance), and that `loadPlaylistItemAndPlay` is the auto-advance load path while `loadPlaylistItem` is the manual one. Use the existing section numbering (next free §).

- [ ] **Step 3: CLAUDE.md — update the playlist line**

In `CLAUDE.md`, update the UI section's playlist mention to: the Playlist dock now snapshots the current setup (Add Current), builds multi-track items (Add Files…), shows collapsible per-track detail cards with Compact/Full-paths toggles, supports drag-drop (.jpl load / media append / media add), keyboard (Enter/Delete/Shift+↑↓), scale override, and auto-advance/loop. Reference `developer_notes` new §.

- [ ] **Step 4: Build (docs don't affect build, but verify nothing references removed locators in C++)**

Run: `cmake --build build_qt -j8 2>&1 | tail -1`
Expected: `[100%] Built target jefecheck`.

- [ ] **Step 5: Commit**

```bash
git add tests/ui/jefecheck/locators.py developer_notes.md CLAUDE.md
git commit -m "docs: playlist parity — locators, developer_notes section, CLAUDE.md

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Final verification (after all tasks)

- [ ] Full clean build: `cmake --build build_qt -j8 2>&1 | tail -2` → built, no errors.
- [ ] Headless smoke set still green:
  - `--playlist-test docs/manual-images/grayScaleLUT.png` → `added=2 afterClear=0 afterLoad=2 detailOk=1`, exit 0.
  - `--fx-multitest "$TMPDIR/jc_ramp.png"` → `FX-MULTI PASS`.
  - `--render-test "$TMPDIR/jc_rt" docs/manual-images/grayScaleLUT.png` → `wrote 107 frame(s)`.
- [ ] Manual GUI pass of the Task 4 + Task 5 checklists.
- [ ] Then finish the branch (PR to `qt-experimental`, squash-merge) via the finishing-a-development-branch skill.

## Self-Review notes (addressed)

- **Spec coverage:** snapshot (T1 `addCurrentAsPlaylistItem`), rich display (T1 detail + T3 card + T4 toggles), drag-drop+keyboard+context menu (T4), scale override (T2+T4), auto-advance+loop (T2 latch + T4 `advanceToNext` + T5 poll), locators+docs (T6). Per-track CC dropped from the POD (not in `gfcLoadParams`) — spec updated to match.
- **Reorder honesty:** view-only `InternalMove` would diverge from the manager vector; plan commits to `DropOnly` + button/keyboard reorder, documented in T4 step 3 and T6 docs. No silent drag-reorder that doesn't persist.
- **Type consistency:** `PlaylistTrackDetail`, `addCurrentAsPlaylistItem`, `addPlaylistFiles`, `appendTracksToPlaylistItem`, `getPlaylistItemDetail`, `setPlaylistScaleOverride`, `consumePlaylistAdvanceSignal`, `isPlaylistItemPlayingOnce`, `loadPlaylistItemAndPlay`, `pausePlaybackIfPlaying`, `advanceToNext` used identically across tasks.
- **Engine API verified (public, no source change):** `gfcPlaybackManager::getEndLimit()` (line 105), `getPlaybackMode()`, `startPlayFwd()`, `pause()`, `getFromFrame()`, `setCurrentFrame(int)` (seek), `isPlaying()`, `getCurrentFrame()` — all present in `gfcplaybackmanager.h`.
