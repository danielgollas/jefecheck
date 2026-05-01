# Bit Depth + Scale Load Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a status-bar bit-depth combobox that selects the default texture format for new loads, and a Shift / Shift+Cmd modifier on drag-drop that downsamples loads to 50% / 25%.

**Architecture:** Bit depth lives as a new `int defaultTextureFormat` field on the existing `gfcSettings` struct, persisted to `QSettings` and exposed in the main-window status bar via a `QComboBox`. The bridge applies it on every load by calling `seq->myGUI->setCompression(...)` before `loadPreview()`. Scale is read from `event->keyboardModifiers()` in `GlViewport_Qt::dropEvent` and threaded through `MainWindow_Qt::loadFileIntoPlate` and the `jefe::qt::loadFileIntoPlate` bridge as a defaulted `float scale` parameter; the bridge converts it to the percentage string `gfcSequenceGUI::setScale` expects.

**Tech Stack:** C++20, Qt 6 (QMainWindow, QComboBox, QSettings, QStandardPaths, QStatusBar), CMake, Appium-Python-Client + Mac2 driver for behavioural tests.

**Spec:** [`docs/superpowers/specs/2026-04-30-bit-depth-scale-load-controls-design.md`](../specs/2026-04-30-bit-depth-scale-load-controls-design.md)

---

## File Map

- **Modify** `src/gfcStructures.h` — add `int defaultTextureFormat` field to `gfcSettings`; initialize in constructor.
- **Modify** `src/qt/SequenceLoadBridge_qt.h` — add `float scale` defaulted param to `loadFileIntoPlate`.
- **Modify** `src/qt/SequenceLoadBridge_qt.cpp` — read `sett.defaultTextureFormat`, set per-sequence compression and scale before `loadPreview()`.
- **Modify** `src/qt/GlViewport_qt.h` — add `fileDroppedWithScale(QString, float)` signal.
- **Modify** `src/qt/GlViewport_qt.cpp` — translate keyboard modifiers in `dropEvent` to scale factor; emit new signal alongside the legacy `fileDropped`.
- **Modify** `src/qt/MainWindow_qt.h` — declare `depthCombo_` member, new `onFileDropped(QString, float)` overload.
- **Modify** `src/qt/MainWindow_qt.cpp` — build the depth combo, restore from QSettings, persist on change, route through to `loadFileIntoPlate(plateIdx, path, scale)`; status-bar message on scaled load.
- **Modify** `tests/ui/jefecheck/locators.py` — add `STATUSBAR_DEPTH = "statusbar.depth.combo"`.
- **Modify** `tests/ui/test_load.py` — add depth-combo default + persistence tests.

The status bar is a permanent widget area that hosts five labels today; the new combo joins them as a sixth permanent widget. No new files. Each modification is small enough to read end-to-end.

---

### Task 0: Branch + working directory

**Files:** N/A — git only.

- [ ] **Step 1: Branch off the freshly-merged qt-migration**

```bash
cd /Users/dgollas/projects/jefecheck2
git checkout qt-migration
git pull --ff-only
git checkout -b qt/16-bit-depth-scale
```

- [ ] **Step 2: Verify clean build before starting**

```bash
cmake --build /Users/dgollas/projects/jefecheck2/build_qt --target jefecheck 2>&1 | tail -3
```

Expected: `Built target jefecheck` with no errors.

---

### Task 1: Add `defaultTextureFormat` to gfcSettings

**Files:**
- Modify: `src/gfcStructures.h:225-299` (the `gfcSettings` class) — add field declaration around the other render-engine flags (near `int fp16` at line 328) and initialize in the constructor.

The field defaults to `GFC_16HALF` (the current implicit default). The FLTK build's existing per-track `gfcSequenceGUI::setCompression` path is unaffected — only the Qt bridge consults this new field.

- [ ] **Step 1: Add the constructor initialization**

In `src/gfcStructures.h`, find the constructor body (starts at line 228, ends at line 299). Add this line near the bottom of the constructor, just before the closing brace at line 299:

```cpp
defaultTextureFormat=GFC_16HALF;
```

- [ ] **Step 2: Add the field declaration**

Find `int fp16;` (around line 328). Add directly after it:

```cpp
int defaultTextureFormat; // GFC_8BPC / GFC_16BPC / GFC_16HALF / GFC_4BPC.
                          // Default for the Qt bridge's drag-drop / Cmd+O
                          // load path. FLTK's per-track Load window writes
                          // straight to gfcSequenceGUI::setCompression and
                          // doesn't read this field.
```

- [ ] **Step 3: Build to confirm `GFC_16HALF` resolves and the field compiles**

```bash
cmake --build /Users/dgollas/projects/jefecheck2/build_qt --target jefecheck 2>&1 | tail -5
```

Expected: `Built target jefecheck`. `GFC_16HALF` is in `UIConstants.h` which `gfcStructures.h` already includes (verified: `gfcStructures.h:22` `#include "UIConstants.h"`).

- [ ] **Step 4: Commit**

```bash
git add src/gfcStructures.h
git commit -m "$(cat <<'EOF'
qt: add gfcSettings::defaultTextureFormat (default GFC_16HALF)

Per-load bit depth needs a global default the Qt drag-drop /
Cmd+O path can read. The existing fp16 / textureCompression
flags are different concepts; this is the explicit GFC_8BPC /
16BPC / 16HALF / 4BPC choice the FLTK Load window exposed
per-track.

FLTK build is unaffected — its loadWindow.cxx still writes
gfcSequenceGUI::setCompression directly per track. Only the
Qt SequenceLoadBridge reads this field.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Bridge applies depth + scale on every load

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.h:170-172` — add defaulted `float scale = 1.0f` parameter.
- Modify: `src/qt/SequenceLoadBridge_qt.cpp` — at the top of `loadFileIntoPlate` (line 619 area), set per-sequence compression and scale on the GUI before `loadPreview()`.

`gfcSequenceGUI::setScale` takes a `std::string` — that's the FLTK Choice widget's "100" / "50" / "25" string contract. We render the scale factor as a percentage int.

- [ ] **Step 1: Update the header signature**

In `src/qt/SequenceLoadBridge_qt.h`, replace lines 170-172:

```cpp
bool loadFileIntoPlate(const std::string& path,
                       int whichSequence,
                       bool kickOffSequenceLoad = true);
```

with:

```cpp
// `scale` is a 0..1 multiplier applied to the per-sequence load scale
// (the FLTK loadWindow's scale chooser stored "100", "50", "25").
// Drag-drop maps Shift = 0.5 and Shift+Cmd = 0.25; plain drop and
// Cmd+O pass 1.0. Out-of-range values clamp to (0, 1].
bool loadFileIntoPlate(const std::string& path,
                       int whichSequence,
                       bool kickOffSequenceLoad = true,
                       float scale = 1.0f);
```

- [ ] **Step 2: Update the implementation**

In `src/qt/SequenceLoadBridge_qt.cpp`, find the `loadFileIntoPlate` function (line 618 area):

```cpp
bool loadFileIntoPlate(const std::string& path,
                       int whichSequence,
                       bool kickOffSequenceLoad) {
    auto* seq = trackManager.getSequence(whichSequence);
    if (!seq || !seq->myGUI || path.empty()) {
        return false;
    }
    seq->myGUI->setFilename(path);

    const std::string loaded = seq->loadPreview();
```

Replace with:

```cpp
bool loadFileIntoPlate(const std::string& path,
                       int whichSequence,
                       bool kickOffSequenceLoad,
                       float scale) {
    auto* seq = trackManager.getSequence(whichSequence);
    if (!seq || !seq->myGUI || path.empty()) {
        return false;
    }
    seq->myGUI->setFilename(path);

    // Apply the global default bit depth before loadPreview reads it.
    // gfcSequenceGUI::setCompression takes one of the
    // GFC_*BPC / GFC_*HALF enum values; gfcSettings::defaultTextureFormat
    // stores that enum directly (default GFC_16HALF).
    seq->myGUI->setCompression(sett.defaultTextureFormat);

    // Translate the 0..1 scale factor to the percentage string the
    // FLTK Choice widget convention expects ("100", "50", "25"). Clamp
    // to (0, 1] so a stray 0 or negative doesn't get sent through and
    // a > 1.0 doesn't try to upsample (the loader doesn't support it).
    if (scale <= 0.0f) scale = 1.0f;
    if (scale > 1.0f) scale = 1.0f;
    char scaleBuf[8];
    std::snprintf(scaleBuf, sizeof(scaleBuf), "%d", int(scale * 100.0f + 0.5f));
    seq->myGUI->setScale(scaleBuf);

    const std::string loaded = seq->loadPreview();
```

- [ ] **Step 3: Build to confirm the new signature + sett field link cleanly**

```bash
cmake --build /Users/dgollas/projects/jefecheck2/build_qt --target jefecheck 2>&1 | tail -5
```

Expected: `Built target jefecheck`. `sett` is already an `extern gfcSettings` at line 28 of the .cpp; `setScale` and `setCompression` are pure-virtual methods on `gfcSequenceGUI` already implemented in `gfcSequenceGUI_Qt`.

- [ ] **Step 4: Commit**

```bash
git add src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp
git commit -m "$(cat <<'EOF'
qt: bridge applies defaultTextureFormat + scale on every load

loadFileIntoPlate now sets the per-sequence compression
(from sett.defaultTextureFormat) and scale (from a new
defaulted float param, 0..1, clamped) on the gfcSequenceGUI
before loadPreview() runs. Drag-drop and Cmd+O paths feed
the scale through; the existing two-arg callers default to
1.0 so multi-frame sequence loading is unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Viewport reads modifiers, emits scaled drop signal

**Files:**
- Modify: `src/qt/GlViewport_qt.h:34-36` — add `fileDroppedWithScale(QString, float)` signal.
- Modify: `src/qt/GlViewport_qt.cpp:288-301` — translate `event->keyboardModifiers()` to scale, emit both the legacy and new signal.

The legacy `fileDropped(QString)` signal is kept and emitted alongside the new one (with `scale=1.0` always for the legacy slot). MainWindow_Qt connects only to the new signal in Task 5; nothing in the codebase outside MainWindow_Qt connects to the old one (verified — only `MainWindow_qt.cpp:96` connects). We could delete the legacy signal, but keeping it is one line and one comment, and avoids a third file change in this task.

- [ ] **Step 1: Add the new signal**

In `src/qt/GlViewport_qt.h`, replace the `signals:` block (lines 34-42):

```cpp
signals:
    void fileDropped(const QString& path);

    // Emitted when the viewport mutates plate state outside the plate
    // cards — drag pan, wheel zoom, keyboard layout/fit/flip/flop, and
    // track-cycle. The Plate Manager dock listens and refreshes its
    // spinboxes so the user can read back the values they just edited.
    void plateStateChanged();
```

with:

```cpp
signals:
    // Legacy single-arg signal — kept so any existing connections that
    // don't care about scale (e.g. future logging hooks) still work.
    // Emitted alongside fileDroppedWithScale(path, 1.0) on plain drops.
    void fileDropped(const QString& path);

    // Scale is the load-time downsample factor read from
    // keyboardModifiers in dropEvent: plain = 1.0, Shift = 0.5,
    // Shift+Cmd = 0.25. MainWindow_Qt threads this through to
    // jefe::qt::loadFileIntoPlate.
    void fileDroppedWithScale(const QString& path, float scale);

    // Emitted when the viewport mutates plate state outside the plate
    // cards — drag pan, wheel zoom, keyboard layout/fit/flip/flop, and
    // track-cycle. The Plate Manager dock listens and refreshes its
    // spinboxes so the user can read back the values they just edited.
    void plateStateChanged();
```

- [ ] **Step 2: Translate modifiers and emit in dropEvent**

In `src/qt/GlViewport_qt.cpp`, find `dropEvent` (line 288):

```cpp
void GlViewport_Qt::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasUrls()) {
        e->ignore();
        return;
    }
    for (const QUrl& u : e->mimeData()->urls()) {
        if (u.isLocalFile()) {
            emit fileDropped(u.toLocalFile());
            e->acceptProposedAction();
            return;
        }
    }
    e->ignore();
}
```

Replace with:

```cpp
void GlViewport_Qt::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasUrls()) {
        e->ignore();
        return;
    }
    // Scale modifier mapping mirrors the spec:
    //   plain     -> 1.0
    //   Shift     -> 0.5
    //   Shift+Cmd -> 0.25
    // Any other modifier combo (Cmd-only, Alt-only, etc.) keeps the
    // default 1.0 — Cmd-only is reserved for future "load into a
    // specific plate" gestures, so no surprise behavior for users
    // who hit it accidentally.
    const auto mods = e->keyboardModifiers();
    const bool shift = mods.testFlag(Qt::ShiftModifier);
    const bool cmd   = mods.testFlag(Qt::ControlModifier);  // macOS: ControlModifier == Cmd
    float scale = 1.0f;
    if (shift && cmd) {
        scale = 0.25f;
    } else if (shift) {
        scale = 0.5f;
    }

    for (const QUrl& u : e->mimeData()->urls()) {
        if (u.isLocalFile()) {
            const QString path = u.toLocalFile();
            emit fileDroppedWithScale(path, scale);
            emit fileDropped(path);  // legacy, see header comment
            e->acceptProposedAction();
            return;
        }
    }
    e->ignore();
}
```

- [ ] **Step 3: Build**

```bash
cmake --build /Users/dgollas/projects/jefecheck2/build_qt --target jefecheck 2>&1 | tail -5
```

Expected: `Built target jefecheck`.

- [ ] **Step 4: Commit**

```bash
git add src/qt/GlViewport_qt.h src/qt/GlViewport_qt.cpp
git commit -m "$(cat <<'EOF'
qt: dropEvent reads Shift/Cmd modifiers, emits scale signal

GlViewport_Qt::dropEvent now translates keyboardModifiers()
to a load-time scale factor:
  plain      = 1.0
  Shift      = 0.5
  Shift+Cmd  = 0.25

Emits a new fileDroppedWithScale(path, scale) signal alongside
the legacy fileDropped(path) so existing single-arg slots keep
working. MainWindow connects to the scaled variant.

Cmd-only and Alt-only stay at 1.0 — those modifiers are
reserved for future per-plate-target gestures.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: MainWindow grows the depth combo on the status bar

**Files:**
- Modify: `src/qt/MainWindow_qt.h:50-89` — declare `QComboBox* depthCombo_` member; forward-declare `QComboBox`.
- Modify: `src/qt/MainWindow_qt.cpp` — add `<QComboBox>` include; build the combo and register it as the leftmost permanent status-bar widget; restore from QSettings on construct, persist on change, mirror into `sett.defaultTextureFormat`.

The status bar's existing layout has four permanent widgets (`layoutStatusLabel_`, `trackStatusLabel_`, `loadedStatusLabel_`, `startupStatusLabel_`). The new combo is added FIRST, before any of those, so it lands leftmost in the right-aligned permanent area.

- [ ] **Step 1: Forward-declare QComboBox in the header**

In `src/qt/MainWindow_qt.h`, find the existing forward declarations (lines 19-26):

```cpp
class QDockWidget;
class FXStackPanel_Qt;
class GlViewport_Qt;
class LUTPanel_Qt;
class PlateManager_Qt;
class QLabel;
class TimelinePanel_Qt;
class QTimer;
```

Replace with:

```cpp
class QDockWidget;
class FXStackPanel_Qt;
class GlViewport_Qt;
class LUTPanel_Qt;
class PlateManager_Qt;
class QComboBox;
class QLabel;
class TimelinePanel_Qt;
class QTimer;
```

- [ ] **Step 2: Add the depthCombo_ member**

In the same file, find the status-bar label members (lines 68-71):

```cpp
    QLabel* layoutStatusLabel_ = nullptr;
    QLabel* trackStatusLabel_ = nullptr;
    QLabel* loadedStatusLabel_ = nullptr;
    QLabel* startupStatusLabel_ = nullptr;
```

Replace with:

```cpp
    QComboBox* depthCombo_ = nullptr;
    QLabel* layoutStatusLabel_ = nullptr;
    QLabel* trackStatusLabel_ = nullptr;
    QLabel* loadedStatusLabel_ = nullptr;
    QLabel* startupStatusLabel_ = nullptr;
```

- [ ] **Step 3: Add the QComboBox include in the .cpp**

In `src/qt/MainWindow_qt.cpp`, find the Qt includes block (lines 14-29). Add `<QComboBox>` in alphabetical order between `<QCloseEvent>` and `<QDir>`:

```cpp
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
```

- [ ] **Step 4: Add UIConstants include if not already present**

Confirm `#include "../UIConstants.h"` is already in the file (it is — line 12). No edit needed; this step is a verification.

```bash
grep -n '#include "../UIConstants.h"' /Users/dgollas/projects/jefecheck2/src/qt/MainWindow_qt.cpp
```

Expected output: a single match on line 12.

- [ ] **Step 5: Add the gfcSettings extern**

`sett` lives in `main.cpp` as `gfcSettings sett;`. The Qt `MainWindow_qt.cpp` doesn't currently extern it; we need it for the depth combo wiring. Find the namespace block at line 31-34:

```cpp
namespace {
constexpr const char* kSettingsGeometry = "MainWindow/geometry";
constexpr const char* kSettingsState    = "MainWindow/state";
}
```

Add this directly above (line 30, between `#include <QTimer>` and the namespace):

```cpp
#include "../gfcStructures.h"
extern gfcSettings sett;

```

Wait — `gfcStructures.h` pulls glad on macOS and conflicts with Qt's QtGui in the same TU. Check whether this file already pulls a header that drags glad in.

```bash
grep -n '^#include' /Users/dgollas/projects/jefecheck2/src/qt/MainWindow_qt.cpp | head -30
```

Expected: includes "FXLutPanel_qt.h", "GlViewport_qt.h", "ImageLoadBridge_qt.h", etc. If any of those already pull `gfcStructures.h` transitively, we're already paying the cost and can include it directly.

If `gfcStructures.h` causes a glad conflict, fall back to the indirect path: extern `sett` directly without including the header, and forward-declare `gfcSettings`. Concretely, replace the addition above with:

```cpp
// gfcSettings lives in main.cpp; including gfcStructures.h here would
// pull glad into a TU that also has Qt's QtGui includes, which collide
// on macOS. Forward-declare and extern instead.
class gfcSettings;
extern gfcSettings sett;
```

But the combo wiring needs to read `sett.defaultTextureFormat` (an int field), which requires the full type. We can't get away with just a forward declaration.

The pragmatic resolution: instead of touching `sett` directly from `MainWindow_qt.cpp`, route through the bridge. The bridge already has `extern gfcSettings sett` and is glad-clean. Add two new bridge functions:

```cpp
// SequenceLoadBridge_qt.h
int  getDefaultTextureFormat();
void setDefaultTextureFormat(int format);
```

Implementation reads/writes `sett.defaultTextureFormat`. MainWindow_Qt calls these, never includes `gfcStructures.h`.

Replace step 5's edit to `MainWindow_qt.cpp` with: **no `gfcStructures.h` include, no extern**. Just call `jefe::qt::getDefaultTextureFormat()` / `jefe::qt::setDefaultTextureFormat(...)` (added in Task 4 step 7 below).

- [ ] **Step 6: Add the bridge accessors**

In `src/qt/SequenceLoadBridge_qt.h`, find the LUT browser block (around line 89, the `applyLUTToActivePlate` neighborhood) — actually add this near the top of the file after `initializeRenderingChain()` for visibility. Insert before line 32 (`// Walks the install-time LUT path` comment):

```cpp
// Default texture format (bit depth) used by the Qt drag-drop / Cmd+O
// load path. Mirrors gfcSettings::defaultTextureFormat. The Qt status
// bar's depth combo reads/writes through these accessors so
// MainWindow_qt.cpp doesn't need to include gfcStructures.h directly
// — that header pulls glad and won't share a TU with QtGui on macOS.
int  getDefaultTextureFormat();
void setDefaultTextureFormat(int format);

```

Then in `src/qt/SequenceLoadBridge_qt.cpp`, find `void initializeRenderingChain()` at line 32. Insert these two functions immediately before it:

```cpp
int getDefaultTextureFormat() {
    return sett.defaultTextureFormat;
}

void setDefaultTextureFormat(int format) {
    sett.defaultTextureFormat = format;
}

```

- [ ] **Step 7: Build the depth combo in MainWindow_Qt::MainWindow_Qt**

In `src/qt/MainWindow_qt.cpp`, find the line that creates the layout status label (around line 61):

```cpp
    layoutStatusLabel_ = new QLabel(this);
    layoutStatusLabel_->setObjectName("statusbar.layout.label");
    statusBar()->addPermanentWidget(layoutStatusLabel_);
```

Insert these lines DIRECTLY BEFORE that block, so the combo lands first / leftmost in the permanent-widget area:

```cpp
    // Bit depth combo — selects the texture format used for new loads.
    // Persists in QSettings; existing plates keep their depth until
    // reloaded. Routes through the SequenceLoadBridge accessors so
    // we don't pull gfcStructures.h (which drags glad) into this TU.
    depthCombo_ = new QComboBox(this);
    depthCombo_->setObjectName("statusbar.depth.combo");
    depthCombo_->setAccessibleName("Default bit depth for new loads");
    depthCombo_->setToolTip(tr(
        "Bit depth used when loading new sequences. Existing plates "
        "keep their current depth until reloaded."));
    // Pairs are <display label, GFC_*BPC enum value>. GFC_4BPC is a
    // historical misnomer in UIConstants.h — actually 4 bytes per
    // component = 32-bit float. We label it "32-float" and silently
    // use the misnamed enum. GFC_S3TCDX1 is intentionally omitted
    // (storage optimization, not a quality choice).
    depthCombo_->addItem("8",        QVariant::fromValue<int>(GFC_8BPC));
    depthCombo_->addItem("16",       QVariant::fromValue<int>(GFC_16BPC));
    depthCombo_->addItem("16-half",  QVariant::fromValue<int>(GFC_16HALF));
    depthCombo_->addItem("32-float", QVariant::fromValue<int>(GFC_4BPC));
    {
        QSettings settings;
        const int saved = settings.value("Engine/defaultTextureFormat",
                                         GFC_16HALF).toInt();
        const int idx = depthCombo_->findData(QVariant::fromValue<int>(saved));
        depthCombo_->setCurrentIndex(idx >= 0 ? idx : 2);  // 2 = 16-half
        jefe::qt::setDefaultTextureFormat(
            depthCombo_->currentData().toInt());
    }
    connect(depthCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        const int v = depthCombo_->currentData().toInt();
        jefe::qt::setDefaultTextureFormat(v);
        QSettings settings;
        settings.setValue("Engine/defaultTextureFormat", v);
    });
    statusBar()->addPermanentWidget(depthCombo_);

```

- [ ] **Step 8: Build to verify the combo compiles and links**

```bash
cmake --build /Users/dgollas/projects/jefecheck2/build_qt --target jefecheck 2>&1 | tail -10
```

Expected: `Built target jefecheck`.

- [ ] **Step 9: Commit**

```bash
git add src/qt/MainWindow_qt.h src/qt/MainWindow_qt.cpp src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp
git commit -m "$(cat <<'EOF'
qt: status-bar bit-depth combo, persisted in QSettings

Adds a QComboBox at the leftmost permanent slot of the status bar
with options 8 / 16 / 16-half / 32-float. Selection is mirrored
into gfcSettings::defaultTextureFormat (via two new bridge
accessors so MainWindow_qt.cpp doesn't have to include
gfcStructures.h, which pulls glad and won't share a TU with
QtGui on macOS) and persisted under QSettings key
Engine/defaultTextureFormat.

Tooltip notes the constraint that existing plates keep their
depth until reloaded — the combo affects the next load only.

GFC_4BPC is the historical misnomer for 32-bit float in
UIConstants.h; we label it 32-float and silently use the enum.
GFC_S3TCDX1 (DXT1) is intentionally omitted from the dropdown
— it's a storage choice, not a quality one.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: MainWindow threads scale through to the bridge

**Files:**
- Modify: `src/qt/MainWindow_qt.h:42-48` — overload `loadFileIntoPlate` to accept scale; replace single-arg `onFileDropped` with two-arg.
- Modify: `src/qt/MainWindow_qt.cpp` — implement the new `loadFileIntoPlate` overload, the new `onFileDropped` overload, connect to `fileDroppedWithScale`, drop the connection to legacy `fileDropped`, status-bar message reflects scale.

Drag-drop is now the only path that delivers a non-1.0 scale. Cmd+O / `--open-file` keep their existing one-arg call — both load at 100%.

- [ ] **Step 1: Update the header**

In `src/qt/MainWindow_qt.h`, replace the `loadFileIntoPlate` declaration (line 42) and the `onFileDropped` slot (line 48):

```cpp
    void loadFileIntoPlate(int plateIdx, const QString& path);
```

```cpp
    void onFileDropped(const QString& path);
```

with:

```cpp
    void loadFileIntoPlate(int plateIdx, const QString& path);
    void loadFileIntoPlate(int plateIdx, const QString& path, float scale);
```

```cpp
    void onFileDropped(const QString& path, float scale);
```

- [ ] **Step 2: Update the connect in the constructor**

In `src/qt/MainWindow_qt.cpp`, find this line (around line 96):

```cpp
    connect(viewport_, &GlViewport_Qt::fileDropped,
            this, &MainWindow_Qt::onFileDropped);
```

Replace with:

```cpp
    // Drag-drop reports a load-time scale (Shift = 0.5, Shift+Cmd = 0.25);
    // wire to the scale-aware slot. The legacy fileDropped signal is
    // still emitted by the viewport but we don't connect it — the
    // scale-aware handler covers all drag cases.
    connect(viewport_, &GlViewport_Qt::fileDroppedWithScale,
            this, &MainWindow_Qt::onFileDropped);
```

- [ ] **Step 3: Implement the new onFileDropped slot**

In `src/qt/MainWindow_qt.cpp`, find the existing `onFileDropped` (around line 540):

```cpp
void MainWindow_Qt::onFileDropped(const QString& path) {
    loadFileIntoPlate(0, path);
}
```

Replace with:

```cpp
void MainWindow_Qt::onFileDropped(const QString& path, float scale) {
    // Active-plate target preserved from the pre-scale behavior — drag
    // always goes to plate 0 today; PR-after-this can extend to "the
    // plate under the drop point" once we factor that out.
    loadFileIntoPlate(0, path, scale);
}
```

- [ ] **Step 4: Implement the scale-aware loadFileIntoPlate overload**

In `src/qt/MainWindow_qt.cpp`, find the existing `loadFileIntoPlate` (around line 485). The function ends around line 538. Replace the entire function:

```cpp
void MainWindow_Qt::loadFileIntoPlate(int plateIdx, const QString& path) {
    if (!viewport_ || path.isEmpty()) return;
    if (plateIdx < 0 || plateIdx > 3) return;

    QString resolved = path;

    // Folder drop → pick the first image-like file inside (alpha-sorted).
    // gfcSequence::findSequenceFiles will then discover the rest of the
    // numbered sequence from that one file. We accept anything OIIO
    // probably handles plus DPX/EXR explicitly; leave actually-loadable
    // checks to the loader so we don't have to keep this list in sync.
    if (QFileInfo(resolved).isDir()) {
        static const QStringList kImageFilters{
            "*.exr", "*.EXR",
            "*.dpx", "*.DPX",
            "*.png", "*.PNG",
            "*.jpg", "*.JPG", "*.jpeg", "*.JPEG",
            "*.tif", "*.TIF", "*.tiff", "*.TIFF",
            "*.tga", "*.TGA",
            "*.bmp", "*.BMP",
        };
        QDir dir(resolved);
        const QStringList entries =
            dir.entryList(kImageFilters, QDir::Files, QDir::Name);
        if (entries.isEmpty()) {
            statusBar()->showMessage(
                QString("No image files in %1").arg(resolved), 5000);
            return;
        }
        resolved = dir.absoluteFilePath(entries.first());
    }

    const QString name = QFileInfo(resolved).fileName();

    // GL texture uploads happen inside loadPreview, so the viewport's
    // context must be current on the calling thread.
    viewport_->makeCurrent();
    const bool ok =
        jefe::qt::loadFileIntoPlate(resolved.toStdString(), plateIdx);
    viewport_->doneCurrent();

    if (!ok) {
        statusBar()->showMessage(
            QString("Load failed: %1").arg(resolved), 5000);
        return;
    }

    viewport_->update();
    static const char kPlateNames[4] = {'A', 'B', 'C', 'D'};
    statusBar()->showMessage(
        QString("%1 loaded into Track %2")
            .arg(name)
            .arg(QChar(kPlateNames[plateIdx])));
}
```

with both the one-arg overload (forwarding to the new one) and the new scale-aware version:

```cpp
void MainWindow_Qt::loadFileIntoPlate(int plateIdx, const QString& path) {
    loadFileIntoPlate(plateIdx, path, 1.0f);
}

void MainWindow_Qt::loadFileIntoPlate(int plateIdx, const QString& path,
                                      float scale) {
    if (!viewport_ || path.isEmpty()) return;
    if (plateIdx < 0 || plateIdx > 3) return;

    QString resolved = path;

    // Folder drop → pick the first image-like file inside (alpha-sorted).
    // gfcSequence::findSequenceFiles will then discover the rest of the
    // numbered sequence from that one file. We accept anything OIIO
    // probably handles plus DPX/EXR explicitly; leave actually-loadable
    // checks to the loader so we don't have to keep this list in sync.
    if (QFileInfo(resolved).isDir()) {
        static const QStringList kImageFilters{
            "*.exr", "*.EXR",
            "*.dpx", "*.DPX",
            "*.png", "*.PNG",
            "*.jpg", "*.JPG", "*.jpeg", "*.JPEG",
            "*.tif", "*.TIF", "*.tiff", "*.TIFF",
            "*.tga", "*.TGA",
            "*.bmp", "*.BMP",
        };
        QDir dir(resolved);
        const QStringList entries =
            dir.entryList(kImageFilters, QDir::Files, QDir::Name);
        if (entries.isEmpty()) {
            statusBar()->showMessage(
                QString("No image files in %1").arg(resolved), 5000);
            return;
        }
        resolved = dir.absoluteFilePath(entries.first());
    }

    const QString name = QFileInfo(resolved).fileName();

    // GL texture uploads happen inside loadPreview, so the viewport's
    // context must be current on the calling thread.
    viewport_->makeCurrent();
    const bool ok =
        jefe::qt::loadFileIntoPlate(resolved.toStdString(), plateIdx,
                                    /*kickOffSequenceLoad=*/true,
                                    scale);
    viewport_->doneCurrent();

    if (!ok) {
        statusBar()->showMessage(
            QString("Load failed: %1").arg(resolved), 5000);
        return;
    }

    viewport_->update();
    static const char kPlateNames[4] = {'A', 'B', 'C', 'D'};
    if (scale < 0.999f) {
        // Flash a 3-second message so the Shift / Shift+Cmd modifier
        // isn't invisible — without this the user shift-drops and has
        // no idea why their image looks different.
        statusBar()->showMessage(
            QString("%1 loaded into Track %2 at %3% scale")
                .arg(name)
                .arg(QChar(kPlateNames[plateIdx]))
                .arg(int(scale * 100.0f + 0.5f)),
            3000);
    } else {
        statusBar()->showMessage(
            QString("%1 loaded into Track %2")
                .arg(name)
                .arg(QChar(kPlateNames[plateIdx])));
    }
}
```

- [ ] **Step 5: Build**

```bash
cmake --build /Users/dgollas/projects/jefecheck2/build_qt --target jefecheck 2>&1 | tail -5
```

Expected: `Built target jefecheck`.

- [ ] **Step 6: Manual smoke test**

Launch the app and verify:

```bash
/Users/dgollas/projects/jefecheck2/build_qt/jefecheck.app/Contents/MacOS/jefecheck &
```

Then:
- Look at the status bar — should see a `Depth: 16-half ▼` (or whatever was last persisted) combo at the leftmost permanent slot.
- Drop an image plain — status reads `<file> loaded into Track A` (no scale).
- Hold Shift while dropping — status reads `<file> loaded into Track A at 50% scale` and the message clears after 3s.
- Hold Shift+Cmd while dropping — status reads `at 25% scale`.
- Quit the app via Cmd+Q.

If anything is off, fix it before committing. If it passes, commit:

- [ ] **Step 7: Commit**

```bash
git add src/qt/MainWindow_qt.h src/qt/MainWindow_qt.cpp
git commit -m "$(cat <<'EOF'
qt: scale modifier threads through MainWindow to the bridge

GlViewport_Qt::fileDroppedWithScale wires to a new
onFileDropped(QString, float) slot which forwards into a new
MainWindow_Qt::loadFileIntoPlate overload that takes a scale
factor. The legacy single-arg overload still exists and just
delegates with scale=1.0 — Cmd+O and --open-file go through
that path unchanged.

Status bar flashes a 3-second "<file> loaded into Track A at
50% scale" message on shift-modified loads so the modifier
isn't invisible. Plain loads keep the existing permanent
status text.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Test — depth combo default + persistence

**Files:**
- Modify: `tests/ui/jefecheck/locators.py` — add `STATUSBAR_DEPTH`.
- Modify: `tests/ui/test_load.py` — add two tests.

The status-bar combo is reachable via Mac2 / XCUITest by `objectName` ("statusbar.depth.combo") because permanent status-bar widgets ARE in the app's AX tree (verified — the existing `STATUSBAR_LAYOUT`, `STATUSBAR_LOADED`, etc. resolve fine in `test_layouts.py` and `test_load.py`). This is in contrast to the menu items in `test_menu.py` (PR-32), which live in macOS's system menu bar and aren't reachable.

We test default-on-first-launch (no QSettings file) and persistence-across-launch (set value, quit, relaunch with same `--config-dir`, read).

- [ ] **Step 1: Add the locator**

In `tests/ui/jefecheck/locators.py`, find the status-bar block:

```python
# Status-bar widgets (permanent right-aligned indicators)
STATUSBAR_LAYOUT = "statusbar.layout.label"
STATUSBAR_TRACK = "statusbar.track.label"
STATUSBAR_LOADED = "statusbar.loaded.label"
STATUSBAR_STARTUP = "statusbar.startup.label"
```

Replace with:

```python
# Status-bar widgets (permanent right-aligned indicators)
STATUSBAR_LAYOUT = "statusbar.layout.label"
STATUSBAR_TRACK = "statusbar.track.label"
STATUSBAR_LOADED = "statusbar.loaded.label"
STATUSBAR_STARTUP = "statusbar.startup.label"
STATUSBAR_DEPTH = "statusbar.depth.combo"
```

- [ ] **Step 2: Write the failing default-value test**

In `tests/ui/test_load.py`, add at the end of the file:

```python


def test_depth_combo_default_is_16_half(app):
    """Fresh launch (per-module config_dir, no saved Engine/defaultTextureFormat)
    shows the spec's default of 16-half on the status-bar depth combo.

    QComboBox exposes its currently-selected text via `title`, not
    `value` — same convention as TRANSPORT_LOOP. Verified in PR-30's
    layer-combo tests.
    """
    combo = app.by_object_name(locators.STATUSBAR_DEPTH)
    assert combo.get_attribute("title") == "16-half"
```

- [ ] **Step 3: Run the test to verify it passes against the new build**

```bash
cd /Users/dgollas/projects/jefecheck2/tests/ui
JEFECHECK_BIN=/Users/dgollas/projects/jefecheck2/build_qt/jefecheck.app \
  .venv/bin/pytest test_load.py::test_depth_combo_default_is_16_half -v --timeout=120
```

Expected: PASS. (If WDA needs to rebuild on this machine, the test may time out the first run; re-run once and it should pass quickly.)

- [ ] **Step 4: Write the persistence test**

The persistence test needs two app launches sharing a config-dir. Module-scoped `app` fixture isn't usable (it tears down at module end, not test end). Use the function-scoped `visual_app` pattern (which calls `JefeCheckApp.launch` directly with a per-test `tmp_path`-backed config_dir) but with a shared dir between two launches.

Add this fixture and test to `tests/ui/test_load.py`:

```python
@pytest.fixture
def reusable_config_dir(tmp_path):
    """A config_dir that survives across two JefeCheckApp.launch calls
    inside one test. Returns a Path; both launches must pass it to
    JefeCheckApp.launch(config_dir=...).
    """
    d = tmp_path / "jefecheck-config"
    d.mkdir()
    return d


def test_depth_combo_persists_across_launch(
        appium_server, jefecheck_binary, reusable_config_dir):
    """Set depth to 8, quit, relaunch with the same config_dir, read it back."""
    from jefecheck import JefeCheckApp
    from selenium.webdriver.common.by import By

    # First launch — change the combo to "8" and quit.
    first = JefeCheckApp.launch(
        binary=jefecheck_binary,
        appium_url=appium_server,
        config_dir=reusable_config_dir,
    )
    try:
        combo = first.by_object_name(locators.STATUSBAR_DEPTH)
        # XCUITest exposes a QComboBox's options as descendant cells
        # accessible via the value. macOS native combos use a
        # AXMenuButton + AXMenu; the simplest cross-driver way is to
        # click the combo, then click the "8" option text.
        combo.click()
        # The popup is a transient overlay; grab the menu item by its
        # visible text and click it.
        opt_8 = first.driver.find_element(
            By.XPATH, "//XCUIElementTypeMenuItem[@title='8']")
        opt_8.click()
        # Confirm the change took before quitting.
        assert combo.get_attribute("title") == "8"
    finally:
        first.quit()

    # Second launch — same config_dir, depth should still be "8".
    second = JefeCheckApp.launch(
        binary=jefecheck_binary,
        appium_url=appium_server,
        config_dir=reusable_config_dir,
    )
    try:
        combo = second.by_object_name(locators.STATUSBAR_DEPTH)
        assert combo.get_attribute("title") == "8", (
            "Depth combo did not persist across launch — check that the "
            "QSettings key 'Engine/defaultTextureFormat' is being written "
            "on combo change and read on construction.")
    finally:
        second.quit()
```

- [ ] **Step 5: Run the persistence test**

```bash
cd /Users/dgollas/projects/jefecheck2/tests/ui
JEFECHECK_BIN=/Users/dgollas/projects/jefecheck2/build_qt/jefecheck.app \
  .venv/bin/pytest test_load.py::test_depth_combo_persists_across_launch -v --timeout=180
```

Expected: PASS. The 180s timeout accounts for two cold launches (~15s WDA each) plus the click sequence.

If the XPath selector for `XCUIElementTypeMenuItem` doesn't match (Mac2 may surface combo options differently per Qt version), debug interactively:

```bash
cd /Users/dgollas/projects/jefecheck2/tests/ui
JEFECHECK_BIN=/Users/dgollas/projects/jefecheck2/build_qt/jefecheck.app \
  .venv/bin/pytest test_load.py::test_depth_combo_persists_across_launch -v --timeout=180 -s --slow-mo=1.0
```

The `--slow-mo` pause + `-s` capture lets you watch the combo open and see what's actually in the AX tree. If `XCUIElementTypeMenuItem` doesn't match, try `XCUIElementTypeStaticText` or `XCUIElementTypeButton` for the option, narrowed by `@title='8'`.

- [ ] **Step 6: Commit**

```bash
cd /Users/dgollas/projects/jefecheck2
git add tests/ui/jefecheck/locators.py tests/ui/test_load.py
git commit -m "$(cat <<'EOF'
test: depth combo default + persistence across launch

Two new tests in tests/ui/test_load.py:

- test_depth_combo_default_is_16_half: fresh launch shows
  the spec's default value.
- test_depth_combo_persists_across_launch: set the combo to
  "8", quit, relaunch sharing the same --config-dir, assert
  the combo still shows "8". Verifies the
  QSettings("Engine/defaultTextureFormat") read/write loop.

Locator STATUSBAR_DEPTH = "statusbar.depth.combo".

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: Push + open PR

- [ ] **Step 1: Run the existing test suite once to confirm no regression**

```bash
cd /Users/dgollas/projects/jefecheck2/tests/ui
JEFECHECK_BIN=/Users/dgollas/projects/jefecheck2/build_qt/jefecheck.app \
  .venv/bin/pytest test_smoke.py test_layouts.py test_load.py test_plate_reset.py -v --timeout=120
```

Expected: all pass. (test_load.py now includes the two new tests from Task 6.)

If any failures appear, investigate and fix before pushing. Common issues:
- WDA cold rebuild → re-run.
- `STATUSBAR_DEPTH` not found → status bar's permanent-widget order may have shifted; verify the combo is `addPermanentWidget`-ed and the objectName matches.

- [ ] **Step 2: Push**

```bash
cd /Users/dgollas/projects/jefecheck2
git push -u origin qt/16-bit-depth-scale
```

- [ ] **Step 3: Open the PR**

```bash
gh pr create --base qt-migration --head qt/16-bit-depth-scale \
  --title "qt: bit-depth combo + scale drag modifier (PR-35)" \
  --body "$(cat <<'EOF'
## Summary

Closes the migration plan's deferred PR-LAST (Qt load window).
Per a 2026-04-30 brainstorming session, we determined that drag-drop,
Cmd+O, plate cards, and the EXR layer combo already cover the FLTK
loadWindow's UX surface — only **bit depth** and **scale** were
actually missing. This PR ships those two and skips the full Load
Manager port.

Spec: \`docs/superpowers/specs/2026-04-30-bit-depth-scale-load-controls-design.md\`

## Bit depth

New \`QComboBox\` on the status bar at the leftmost permanent slot.
Options 8 / 16 / 16-half / 32-float, default 16-half. Persists in
\`QSettings\` under \`Engine/defaultTextureFormat\`. Mirrored into a
new \`gfcSettings::defaultTextureFormat\` field; the bridge applies
it to every drag-drop / Cmd+O load before \`loadPreview\` runs.

Tooltip: *"Bit depth used when loading new sequences. Existing
plates keep their current depth until reloaded."*

## Scale modifier

\`GlViewport_Qt::dropEvent\` reads \`event->keyboardModifiers()\`:
- plain         = 100%
- Shift         = 50%
- Shift+Cmd     = 25%

Threaded through a new \`fileDroppedWithScale(QString, float)\`
signal → new \`MainWindow_Qt::loadFileIntoPlate(plateIdx, path, scale)\`
overload → new \`scale\` defaulted parameter on
\`jefe::qt::loadFileIntoPlate\`. Status bar flashes a 3-second
\"loaded at 50% scale\" message on modifier loads so the gesture
isn't invisible.

Cmd+O dialog stays at 100% (no scale picker).

## Tests

- \`test_depth_combo_default_is_16_half\` — first launch shows
  \"16-half\" on the combo title.
- \`test_depth_combo_persists_across_launch\` — set to \"8\",
  quit, relaunch with same \`--config-dir\`, value survives.

## What this closes

- Plan task #90 (PR-LAST) — replaced by this smaller surface.
- Plan Phase 2E item 2 (LoadWindow_qt) — same.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

Expected: returns a PR URL.

---

## Self-review

**Spec coverage check:**
- Bit-depth combo position (leftmost permanent) — Task 4 step 7 ✓
- Bit-depth options + GFC_4BPC misnomer — Task 4 step 7 ✓
- Bit-depth default = 16-half — Task 1 + Task 4 step 7 ✓
- QSettings key `Engine/defaultTextureFormat` — Task 4 step 7 ✓
- Tooltip text — Task 4 step 7 ✓
- `defaultTextureFormat` on gfcSettings — Task 1 ✓
- Bridge applies depth on every load — Task 2 ✓
- Modifier mapping (plain / Shift / Shift+Cmd) — Task 3 step 2 ✓
- Cmd-only / Alt-only stay at 1.0 — Task 3 step 2 ✓
- `fileDroppedWithScale` signal added, legacy kept — Task 3 step 1 ✓
- Status-bar 3-second flash on scaled load — Task 5 step 4 ✓
- Cmd+O unchanged — Task 5 (the existing menu callback already calls the one-arg overload, which delegates with scale=1.0) ✓
- `STATUSBAR_DEPTH` locator — Task 6 step 1 ✓
- Default + persistence tests — Task 6 ✓

No spec gaps.

**Placeholder scan:** searched for "TBD" / "TODO" (in plan text, not in source comments) / "fill in" — none found. All code blocks are complete.

**Type consistency:** `defaultTextureFormat` is `int` everywhere it appears (Task 1 declaration, Task 4's bridge accessors, Task 4 step 7's QSettings read). Combo uses `QVariant::fromValue<int>` consistently. The new `loadFileIntoPlate` overload signatures match between header (Task 5 step 1) and implementation (Task 5 step 4): `loadFileIntoPlate(int plateIdx, const QString& path, float scale)`. Bridge function is `bool loadFileIntoPlate(const std::string& path, int whichSequence, bool kickOffSequenceLoad, float scale)` — consistent in header (Task 2 step 1) and call site (Task 5 step 4).
