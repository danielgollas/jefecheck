# Preferences Audit + Redesign Implementation Plan (JEF-16)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every JefeCheck preference either actually affect the product (and persist across launches) or be removed, and rebuild the Preferences window in the JEF-13 discreet VFX-dark design language — executed section by section.

**Architecture:** The Preferences window (`src/qt/PreferencesWindow_qt.{h,cpp}`) is a modal `QDialog` whose widgets bind to the global `gfcSettings sett`. We (a) add a real `QSettings`-based persistence backbone (load at startup, save on Done, snapshot/restore on Cancel), (b) redesign the window with `CollapsibleSection_qt` + the global dark qss + a small property-hook stylesheet, and (c) go section by section wiring dead settings, adding UI for hidden-but-desired ones, and removing obsolete ones. A new checkerboard-background option is wired in the render bridge.

**Tech Stack:** C++20, Qt6 (Widgets + OpenGLWidgets), OpenGL (compatibility 3.3 via GLAD, ARB shader path on macOS), OpenImageIO, CMake. No automated test framework — **every task verifies by building and running the app and observing the effect.**

## Global Constraints

- **Branch:** `JEF-16-wire-up-preferences` (already on the merged JEF-13 tokens). Commit per task.
- **No unit-test framework exists.** Verification = `cmake --build build` (clean) + launch + observe. Build dir is `build/` (create once with `cmake -B build` if absent). Binary: `build/jefecheck` (macOS: may be `build/jefecheck.app/Contents/MacOS/jefecheck`).
- **Runtime resources:** ensure `FX`/`fonts` symlinks exist for dev runs: `ln -sf $(pwd)/src/FX FX && ln -sf $(pwd)/src/fonts fonts` (only if missing).
- **The global settings instance** is `gfcSettings sett;` defined in `src/qt/qt_globals.cpp:23`. Any TU reading it must add `extern gfcSettings sett;`.
- **macOS GL:** use ARB shader variants; never mix ARB and modern GL. Initialize all GL handles to 0. The viewport GL context must be current before GL calls from non-paint code.
- **TU separation (developer_notes §1):** only `SequenceLoadBridge_qt.cpp` includes the rendering-chain managers directly; other Qt code routes through `jefe::qt::*` accessors. Do not include glad + QOpenGLWidget in the same TU.
- **Object names:** dotted-leaf scheme `preferences.<section>.<field>.<role>`; update `tests/ui/jefecheck/locators.py` in lockstep with any rename. Menu action that opens the dialog: `menu.file.preferences`.
- **QSettings** is created with the app's default org/app (already configured — `QSettings s;` with no args works, as existing code does). Persisted-key namespace: reuse existing keys where present (`Engine/*`, `Session/startupBehavior`, `Playlist/*`), add new ones under a section prefix (`General/*`, `Formats/*`, `Search/*`, `Remote/*`, `Text/*`).
- **Persistence rule:** exactly one store per setting. `saveSettings()`/`readSettings()` (XML) are dead stubs — do **not** resurrect them; use `QSettings`.

---

## File Structure

**Modified:**
- `src/qt/PreferencesWindow_qt.{h,cpp}` — the redesign + all page rebuilds (primary file).
- `src/qt/qt_prefs_persist.{h,cpp}` *(new)* — `jefe::qt::loadPreferences()` / `writePreferences()` / snapshot helpers: the single place that maps `sett` ↔ `QSettings`.
- `src/qt/RenderBridge_qt.cpp` — wire `bgColor` + checkerboard background.
- `src/gfcimageloaderoiio.cpp` — wire the two EXR settings.
- `src/qt/MainWindow_qt.cpp` — call `loadPreferences()` at startup; wire `startFullscreen` / `openLoadWindowAtStartup`; `defaultBrowsePath` seed.
- `src/gfcStructures.h` / `.cpp` — remove 19 dead fields + their initializers/uses.
- `tests/ui/jefecheck/locators.py` — object-name updates.

**Why a new `qt_prefs_persist` TU:** persistence is a distinct responsibility used by both `MainWindow_qt` (load at startup) and `PreferencesWindow_qt` (save on Done); a focused file keeps the `sett`↔`QSettings` mapping in one auditable place instead of scattered `QSettings` calls.

---

## Task 0: Persistence backbone + window-shell redesign

Establishes the store and the new chrome. No setting *values* change yet; the three existing pages render in the new shell and now persist.

**Files:**
- Create: `src/qt/qt_prefs_persist.h`, `src/qt/qt_prefs_persist.cpp`
- Modify: `src/qt/PreferencesWindow_qt.{h,cpp}`, `src/qt/MainWindow_qt.cpp`
- (CMake auto-globs `src/qt/*.cpp`, so no CMake edit needed for the new TU.)

**Interfaces:**
- Produces:
  - `namespace jefe::qt { void loadPreferences(); void writePreferences(); }` — load reads every persisted `QSettings` key into `sett`; write persists every preference from `sett`.
  - Cancel-revert uses a plain copy: a `gfcSettings sett_backup_;` member on `PreferencesWindow_Qt`, set to `sett` on open and restored (`sett = sett_backup_`) on reject. (`gfcSettings` is copyable — no separate snapshot type needed.)
  - The redesigned `PreferencesWindow_Qt` with a dialog-scoped stylesheet and a `CollapsibleSection_qt` grouping helper `QWidget* section(const QString& title, QWidget* content)`.
- Consumes: `extern gfcSettings sett;` (from `qt_globals`), `CollapsibleSection_qt`.

- [ ] **Step 1: Create the persistence TU (`qt_prefs_persist.h`).**

```cpp
// src/qt/qt_prefs_persist.h
// Single source of truth for mapping the global gfcSettings `sett` to/from
// Qt QSettings. The legacy XML saveSettings()/readSettings() are dead stubs;
// this is the only real preferences persistence.
#ifndef JEFECHECK_QT_PREFS_PERSIST_H
#define JEFECHECK_QT_PREFS_PERSIST_H

namespace jefe { namespace qt {

// Read every persisted preference key from QSettings into the global `sett`.
// Call once at startup, after `sett` is default-constructed. Missing keys keep
// the constructor default.
void loadPreferences();

// Persist every preference from the global `sett` to QSettings. Call on
// Preferences "Done".
void writePreferences();

} }  // namespace jefe::qt
#endif
```

- [ ] **Step 2: Implement `qt_prefs_persist.cpp` with the initial (current-persisted) key set.**

Start by centralizing the keys that ALREADY persist elsewhere, so behavior is unchanged, then later tasks add their keys here. Read/write helpers keep it DRY.

```cpp
// src/qt/qt_prefs_persist.cpp
#include "qt_prefs_persist.h"
#include "../gfcStructures.h"
#include <QSettings>
#include <string>

extern gfcSettings sett;

namespace jefe { namespace qt {

void loadPreferences() {
    QSettings s;
    // Engine (already persisted in MainWindow_qt today — centralize here).
    sett.defaultDecodeFilter  = s.value("Engine/defaultDecodeFilter",  sett.defaultDecodeFilter).toInt();
    sett.defaultTextureFormat = s.value("Engine/defaultTextureFormat", sett.defaultTextureFormat).toInt();
    // Session behavior.
    sett.startupSessionBehavior = s.value("Session/startupBehavior", sett.startupSessionBehavior).toInt();
    // NOTE: later tasks append their sections' keys here (General/*, Formats/*, ...).
}

void writePreferences() {
    QSettings s;
    s.setValue("Engine/defaultDecodeFilter",  sett.defaultDecodeFilter);
    s.setValue("Engine/defaultTextureFormat", sett.defaultTextureFormat);
    s.setValue("Session/startupBehavior",     sett.startupSessionBehavior);
    // NOTE: later tasks append their sections' keys here.
}

} }  // namespace jefe::qt
```

- [ ] **Step 3: Call `loadPreferences()` at startup.** In `src/qt/MainWindow_qt.cpp`, find the existing QSettings-load block (~line 80, where `Engine/defaultDecodeFilter` is read). Replace that ad-hoc block with a single `jefe::qt::loadPreferences();` call (add `#include "qt_prefs_persist.h"`). Verify the block you remove only set the three keys now handled by `loadPreferences()`; if it did more, keep the extra lines.

- [ ] **Step 4: Redesign the shell + wire Cancel-revert + real save.** In `PreferencesWindow_qt.cpp`:
  - Add `#include "qt_prefs_persist.h"`, `#include "CollapsibleSection_qt.h"`.
  - In the constructor, after `setObjectName("preferences.dialog")`, apply a dialog-scoped stylesheet defining property hooks (mirror `RemotePanel_qt.cpp:27-89`), e.g.:

```cpp
setObjectName("preferences.dialog");
setStyleSheet(R"(
    QLabel[role="section"] { color:#9a9a9a; font-size:11px; font-weight:600; }
    QWidget[card="true"]   { background:#232327; border:1px solid #333; border-radius:8px; }
    QPushButton[accent="true"] { border-color:#4c6577; color:#a6c0d2; font-weight:600; }
)");
```

  - Snapshot on open: capture the persisted `sett` fields into a member before the user edits (simplest: `sett_backup_ = sett;` — `gfcSettings` is copyable). Add `gfcSettings sett_backup_;` to the header.
  - Change the Done handler from `saveSettings(&sett); accept();` to:

```cpp
connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
    jefe::qt::writePreferences();   // real persistence (was a no-op)
    accept();
});
```

  - Change Cancel to restore in-memory state so live edits don't leak:

```cpp
connect(buttons, &QDialogButtonBox::rejected, this, [this]() {
    sett = sett_backup_;            // revert live mutations
    reject();
});
```

  - Add a grouping helper and keep the sidebar+stack shell but let pages use sections:

```cpp
// Wraps `content` in a CollapsibleSection and returns it for adding to a page layout.
QWidget* PreferencesWindow_Qt::section(const QString& title, QWidget* content) {
    auto* sec = new CollapsibleSection(title, this);
    sec->setContentWidget(content);
    sec->setExpanded(true);
    return sec;
}
```

  - Leave the three existing `buildGeneralPage/EnginePage/FormatsPage` bodies working for now (they still mutate `sett`); they get restructured in their own tasks. Delete the `saveSettings` include/usage.

- [ ] **Step 5: Build.**

Run: `cmake --build build 2>&1 | tail -20`
Expected: builds clean (no errors). If `build/` doesn't exist: `cmake -B build && cmake --build build`.

- [ ] **Step 6: Run and verify persistence + Cancel.**

Run: `build/jefecheck` (or the `.app` path). Then:
1. Open Preferences (File menu / the `menu.file.preferences` action).
2. Change the decode filter, click **Done**. Quit. Relaunch, reopen Preferences → the filter change **persisted** (previously it would too, via the old block — this confirms no regression).
3. Reopen, change decode filter, click **Cancel**. Reopen → value is **unchanged** (Cancel reverted). Previously Cancel would have leaked the change into `sett` for the session.

Expected: both behaviors hold; the window still shows General/Engine/Formats and looks consistent with the dark theme.

- [ ] **Step 7: Commit.**

```bash
git add src/qt/qt_prefs_persist.h src/qt/qt_prefs_persist.cpp src/qt/PreferencesWindow_qt.h src/qt/PreferencesWindow_qt.cpp src/qt/MainWindow_qt.cpp
git commit -m "JEF-16: preferences persistence backbone + shell redesign + Cancel-revert"
```

---

## Task 1: General section — wire bgColor + checkerboard, browse path, fullscreen, load-at-startup; add thumbnails + feedback group

**Files:**
- Modify: `src/qt/RenderBridge_qt.cpp` (bgColor + checkerboard draw)
- Modify: `src/qt/PreferencesWindow_qt.cpp` (`buildGeneralPage`), `.h`
- Modify: `src/qt/qt_prefs_persist.cpp` (persist the General keys)
- Modify: `src/qt/MainWindow_qt.cpp` (apply `startFullscreen`, `openLoadWindowAtStartup` at startup; seed `defaultBrowsePath`)
- Add: `bgCheckerboard` field to `src/gfcStructures.h`

**Interfaces:**
- Consumes: `jefe::qt::loadPreferences/writePreferences` (Task 0).
- Produces: `sett.bgCheckerboard` (int, 0/1, default 0); persisted keys `General/bgColor`, `General/bgCheckerboard`, `General/defaultBrowsePath`, `General/startFullscreen`, `General/openLoadWindowAtStartup`, `General/showThumbnails`, `General/feedbackMessageSize`, `General/feedbackMessageFadeDelay`.

- [ ] **Step 1: Add the `bgCheckerboard` setting field.** In `src/gfcStructures.h`, next to `float bgColor;` (line ~305) add `int bgCheckerboard;`. In the constructor (near line 233 where `bgColor=0.149019;`) add `bgCheckerboard=0;`.

- [ ] **Step 2: Wire `bgColor` + checkerboard in the render bridge.** In `src/qt/RenderBridge_qt.cpp`: add `#include "../gfcSequence.h"` (declares `extern gfcSettings sett;`). Replace the flat clear at lines 36-37:

```cpp
void RenderBridge_Qt::onDraw() {
    if (width_ == 0 || height_ == 0) return;

    if (sett.bgCheckerboard) {
        drawCheckerboardBackground(width_, height_);   // fills the whole framebuffer
    } else {
        glClearColor(sett.bgColor, sett.bgColor, sett.bgColor, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    AppContext::instance().plates().draw(width_, height_, sizeDirty_);
    sizeDirty_ = false;
}
```

- [ ] **Step 3: Implement `drawCheckerboardBackground`.** Add a file-local helper in `RenderBridge_qt.cpp` (above `onDraw`). It must still clear depth, then paint a full-framebuffer checkerboard in a pixel-exact ortho using immediate-mode quads (the app uses the GL compatibility profile). Two shades derived from `bgColor`; `width_/height_` are already physical pixels.

```cpp
#include "../gfcTextRenderer.h"   // gfc_gl_dpiscale()
namespace {
void drawCheckerboardBackground(int wPx, int hPx) {
    glClear(GL_DEPTH_BUFFER_BIT);
    const float b = sett.bgColor;
    // Two shades: nudge away from bgColor by a fixed delta, clamped.
    const float d = 0.06f;
    float lo = b - d, hi = b + d;
    if (b < d)        { lo = b; hi = b + 2*d; }
    if (b > 1.0f - d) { hi = b; lo = b - 2*d; }
    lo = lo < 0 ? 0 : lo;  hi = hi > 1 ? 1 : hi;

    const int cell = int(24 * gfc_gl_dpiscale());   // ~24 logical px per cell
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, wPx, 0, hPx, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
    glDisable(GL_TEXTURE_2D); glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glDisable(GL_BLEND);
    glBegin(GL_QUADS);
    for (int y = 0; y < hPx; y += cell) {
        for (int x = 0; x < wPx; x += cell) {
            const bool even = ((x / cell) + (y / cell)) & 1;
            const float c = even ? hi : lo;
            glColor3f(c, c, c);
            const float x1 = float(x), y1 = float(y);
            const float x2 = float(x + cell), y2 = float(y + cell);
            glVertex2f(x1,y1); glVertex2f(x2,y1); glVertex2f(x2,y2); glVertex2f(x1,y2);
        }
    }
    glEnd();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
}
}  // namespace
```

- [ ] **Step 4: Rebuild the General page.** In `PreferencesWindow_qt.cpp` `buildGeneralPage`, keep the existing bg-color swatch button (it already reads/writes `sett.bgColor`), and add directly under it a **checkerboard** checkbox:

```cpp
auto* checker = new QCheckBox("Checkerboard background", page);
checker->setChecked(sett.bgCheckerboard != 0);
checker->setObjectName("preferences.general.checkerboard.check");
checker->setAccessibleName("Checkerboard background");
connect(checker, &QCheckBox::toggled, page, [](bool on){ sett.bgCheckerboard = on ? 1 : 0; });
form->addRow(QString(), checker);
```

  Add a **thumbnails** checkbox (wired field `showThumbnails`, currently hidden):

```cpp
auto* thumbs = new QCheckBox("Timeline thumbnails", page);
thumbs->setChecked(sett.showThumbnails);
thumbs->setObjectName("preferences.general.thumbnails.check");
thumbs->setAccessibleName("Timeline thumbnails");
connect(thumbs, &QCheckBox::toggled, page, [](bool on){ sett.showThumbnails = on; });
form->addRow(QString(), thumbs);
```

  Add a **feedback-message** group (fields `feedbackMessageSize` int, `feedbackMessageFadeDelay` float — both already consumed in `gfcplatemanager.cpp`):

```cpp
auto* fbSize = new QSpinBox(page);
fbSize->setRange(6, 72); fbSize->setValue(sett.feedbackMessageSize);
fbSize->setObjectName("preferences.general.feedbacksize.spin");
fbSize->setAccessibleName("Feedback message size");
connect(fbSize, QOverload<int>::of(&QSpinBox::valueChanged), page,
        [](int v){ sett.feedbackMessageSize = v; });
form->addRow("Feedback message size", fbSize);

auto* fbFade = new QDoubleSpinBox(page);
fbFade->setRange(0.0, 30.0); fbFade->setSingleStep(0.5);
fbFade->setValue(sett.feedbackMessageFadeDelay);
fbFade->setObjectName("preferences.general.feedbackfade.spin");
fbFade->setAccessibleName("Feedback message fade delay");
connect(fbFade, QOverload<double>::of(&QDoubleSpinBox::valueChanged), page,
        [](double v){ sett.feedbackMessageFadeDelay = float(v); });
form->addRow("Feedback fade delay (s)", fbFade);
```

  The existing `defaultBrowsePath`, `startFullscreen`, `openLoadWindowAtStartup`, `startupSessionBehavior`, `enableCrashRecoverySession`, `aspectBarsOpacity` rows stay (they already write `sett`); **remove** the `processorPriority` row (moved to Task 2 removal — or remove now if convenient, but it's owned by Task 2).

- [ ] **Step 5: Wire `startFullscreen` + `openLoadWindowAtStartup` at startup.** In `MainWindow_qt.cpp`, after the window is constructed/shown and `loadPreferences()` ran, add:

```cpp
if (sett.startFullscreen) showFullScreen();
if (sett.openLoadWindowAtStartup) openLoadWindow();   // existing slot, MainWindow_qt.cpp:1510
```

  Place these where the window is first shown (after `show()`), guarded so they run once. `openLoadWindow()` is an existing member.

- [ ] **Step 6: Seed `defaultBrowsePath` into file dialogs.** Find the Quick Load / open handlers in `MainWindow_qt.cpp` that call `QFileDialog` with `MainWindow/lastLoadDir`. Change the initial dir to prefer `lastLoadDir` if set, else `QString::fromStdString(sett.defaultBrowsePath)`. (One-line fallback per dialog call site.)

- [ ] **Step 7: Persist the General keys.** In `qt_prefs_persist.cpp`, add to `loadPreferences()`:

```cpp
sett.bgColor        = s.value("General/bgColor", sett.bgColor).toFloat();
sett.bgCheckerboard = s.value("General/bgCheckerboard", sett.bgCheckerboard).toInt();
sett.defaultBrowsePath = s.value("General/defaultBrowsePath",
                                 QString::fromStdString(sett.defaultBrowsePath)).toString().toStdString();
sett.startFullscreen = s.value("General/startFullscreen", sett.startFullscreen).toInt();
sett.openLoadWindowAtStartup = s.value("General/openLoadWindowAtStartup", sett.openLoadWindowAtStartup).toInt();
sett.showThumbnails  = s.value("General/showThumbnails", sett.showThumbnails).toBool();
sett.feedbackMessageSize = s.value("General/feedbackMessageSize", sett.feedbackMessageSize).toInt();
sett.feedbackMessageFadeDelay = s.value("General/feedbackMessageFadeDelay", sett.feedbackMessageFadeDelay).toFloat();
```

  And the mirror `setValue` calls in `writePreferences()`.

- [ ] **Step 8: Build.** Run: `cmake --build build 2>&1 | tail -20` — expect clean.

- [ ] **Step 9: Run and verify each wired setting.**
  - Load an image, open Preferences → toggle **Checkerboard background** → viewport background becomes a checkerboard around the image; adjust **Background color** → both flat fill and checker shades track it. Toggle off → flat fill of the chosen gray.
  - Set **Start in fullscreen** on, Done, quit, relaunch → app opens fullscreen. Turn off, relaunch → windowed.
  - Set **Open Load window at startup** on, relaunch → the Load window appears.
  - Feedback size/fade: trigger an on-screen feedback message (e.g. a load) → size/fade reflect the settings.
  All settings survive quit+relaunch (persisted).

- [ ] **Step 10: Commit.**

```bash
git add -A
git commit -m "JEF-16 General: wire bgColor+checkerboard, fullscreen, load-at-startup, browse path; add thumbnails+feedback"
```

---

## Task 2: Playback & Engine section — verify wired settings, remove processorPriority, drop mirrorPaths from UI

**Files:** `src/qt/PreferencesWindow_qt.cpp` (`buildEnginePage`), `src/qt/qt_prefs_persist.cpp`

**Interfaces:** Consumes Task 0/1. Produces persisted keys `Engine/vsync`, `Engine/maximumFramesInQueue`, `Engine/numOfPartitions`, `Engine/balanceReads`, `Engine/forcePBO`, `Engine/renderingEngine` (decode filter + bit depth already persisted).

- [ ] **Step 1: Remove `processorPriority` from the Engine (and General) UI.** Delete its `QSpinBox` row(s) in `PreferencesWindow_qt.cpp` (search `processorPriority` / objectName `preferences.general.priority.spin`). The struct field itself is removed in Task 7.

- [ ] **Step 2: Rename Engine page → "Playback & Engine" and group with sections.** Change the `addPage("Engine", page)` title to `addPage("Playback & Engine", page)` and update `sidebar_` build order comment. Wrap the render/engine controls with `section("Engine", ...)` / leave as a form if simpler; keep object names `preferences.engine.*`.

- [ ] **Step 3: Standardize the two mismatched Engine object names.** Rename `prefs.engine.defaultDecodeFilter` → `preferences.engine.decodefilter.combo` and `prefs.engine.defaultTextureFormat` → `preferences.engine.bitdepth.combo`. Update `tests/ui/jefecheck/locators.py` (`PREFS_DEFAULT_DECODE_FILTER` and add a bit-depth locator) to the new leaves.

- [ ] **Step 4: Persist the remaining Engine keys.** Add `Engine/vsync`, `Engine/maximumFramesInQueue`, `Engine/numOfPartitions`, `Engine/balanceReads`, `Engine/forcePBO`, `Engine/renderingEngine` to `loadPreferences()`/`writePreferences()`.

- [ ] **Step 5: Verify each Engine setting actually takes effect (read the consumer).** For each of `vsync` (`main_qt.cpp`, `MainWindow_qt.cpp`), `maximumFramesInQueue`/`numOfPartitions`/`forcePBO`/`renderingEngine` (`gfcSequence.cpp`), `balanceReads` (`gfcimageloaderdpx.cpp`): open the consumer, confirm the field is read at a point that affects behavior. Record any that are read but never actually gate anything as a follow-up note in the plan's Task 8 checklist. (`renderingEngine` is start-only — note it needs a relaunch to apply.)

- [ ] **Step 6: Build + run.** `cmake --build build 2>&1 | tail -20`. Launch, open Preferences → **Playback & Engine** page renders; toggle vsync → confirm it applies (frame pacing / no tearing) after the point it's read; values persist across relaunch.

- [ ] **Step 7: Commit.**

```bash
git add -A
git commit -m "JEF-16 Engine: remove processorPriority UI, standardize object names, persist engine keys"
```

---

## Task 3: Formats section — remove EXR tonemap floats, wire the two EXR toggles to OIIO

**Files:** `src/qt/PreferencesWindow_qt.cpp` (`buildFormatsPage`), `src/gfcimageloaderoiio.cpp`, `src/qt/qt_prefs_persist.cpp`

**Interfaces:** Produces persisted keys `Formats/exrIgnoreDisplayWindow`, `Formats/exrIgnoreHeadersAspectRatio`.

- [ ] **Step 1: Strip the 5 tonemap rows from the Formats page.** In `buildFormatsPage`, delete the `makeEXRSpin` rows for `exrExposure`, `exrDefog`, `exrGamma`, `exrKneeLow`, `exrKneeHigh` and the `makeEXRSpin` lambda. Keep only the two checkboxes (`exrIgnoreDisplayWindow`, `exrIgnoreHeadersAspectRatio`).

- [ ] **Step 2: Wire "ignore header aspect ratio" in the OIIO loader (trivial path first).** In `src/gfcimageloaderoiio.cpp`: add `extern gfcSettings sett;` after the `gfcStructures.h` include (line ~3). After the spec is obtained (line ~119), read the aspect: `const float par = spec.get_float_attribute("PixelAspectRatio", 1.0f);`. At the dimension block (lines ~298-299, after `quadSizeX = theBitmap->Width;`), apply:

```cpp
if (!sett.exrIgnoreHeadersAspectRatio) {
    quadSizeX = int(quadSizeX * par);   // stretch horizontally per header PAR
}
```

- [ ] **Step 3: Build + verify aspect toggle.** Build; load an EXR/image with a non-1.0 pixel aspect (or any EXR) → toggling **EXR: ignore header aspect ratio** changes horizontal stretch on reload. (Reload the frame after changing — the setting is read at load time.)

- [ ] **Step 4: Wire "ignore display window" (compositing path).** Default `0` must honor the EXR **display window**. Today the loader uses only the data window (`spec.width/height`). Implement: when `!sett.exrIgnoreDisplayWindow` and the display window differs from the data window, allocate the bitmap at display-window size and composite the data window into it at its offset (mirror `src/gfcimageloaderexr.cpp:884-918`). Read `spec.full_width/full_height/full_x/full_y` and `spec.x/y`. When `sett.exrIgnoreDisplayWindow` is set, keep the current data-window path. Guard the whole branch so non-EXR formats (where full==data) are unaffected. Apply the same to `peek()` (lines ~368-369) so reported size matches.

> If the compositing proves large, land Step 2 (aspect) + the checkbox persistence first as one commit, then the display-window compositing as a second commit within this task.

- [ ] **Step 5: Persist the two keys.** Add `Formats/exrIgnoreDisplayWindow` and `Formats/exrIgnoreHeadersAspectRatio` to `loadPreferences()`/`writePreferences()`.

- [ ] **Step 6: Build + verify display window.** Build; load an EXR whose data window is smaller than its display window (e.g. a render with overscan/crop). Default (toggle off) → image sits in the full display frame with correct padding/offset. Toggle **ignore display window** on, reload → only the data window is shown. Both persist across relaunch.

- [ ] **Step 7: Commit.**

```bash
git add -A
git commit -m "JEF-16 Formats: remove EXR tonemap floats, wire ignore-display-window + ignore-aspect to OIIO loader"
```

---

## Task 4: Search Paths section (new) — wire searchPaths + recursive + enable

**Files:** `src/qt/PreferencesWindow_qt.{h,cpp}` (new `buildSearchPathsPage`), `src/qt/qt_prefs_persist.cpp`. Verify consumer in `gfcSequence.cpp` (`useSearchPaths`).

**Interfaces:** Produces persisted keys `Search/useSearchPaths`, `Search/recursive`, `Search/paths` (QStringList). `sett.searchPaths` is `std::vector<std::string>`; `sett.searchPathsRecursive` bool; `sett.useSearchPaths` bool.

- [ ] **Step 1: Confirm how `useSearchPaths`/`searchPaths` are consumed.** Read `gfcSequence.cpp` around the `useSearchPaths` reference. Confirm whether `searchPaths` is iterated when resolving a missing/relative frame path. If `searchPaths` is read there, the wiring is UI→`sett`→that consumer. If only `useSearchPaths` is read (paths never iterated), add the iteration in the consumer as part of this task (mirror any existing path-resolution code). Record the exact consumer behavior in a comment.

- [ ] **Step 2: Add `buildSearchPathsPage()` (declare in `.h`).** A page with: an **enable** checkbox (`useSearchPaths`), a **recursive** checkbox (`searchPathsRecursive`), and a `QListWidget` of paths with **Add…** (QFileDialog getExistingDirectory) / **Remove** buttons that edit `sett.searchPaths`.

```cpp
void PreferencesWindow_Qt::buildSearchPathsPage() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);

    auto* enable = new QCheckBox("Use search paths", page);
    enable->setChecked(sett.useSearchPaths);
    enable->setObjectName("preferences.search.enable.check");
    connect(enable, &QCheckBox::toggled, page, [](bool on){ sett.useSearchPaths = on; });
    v->addWidget(enable);

    auto* recursive = new QCheckBox("Search recursively", page);
    recursive->setChecked(sett.searchPathsRecursive);
    recursive->setObjectName("preferences.search.recursive.check");
    connect(recursive, &QCheckBox::toggled, page, [](bool on){ sett.searchPathsRecursive = on; });
    v->addWidget(recursive);

    auto* list = new QListWidget(page);
    list->setObjectName("preferences.search.paths.list");
    for (const auto& p : sett.searchPaths) list->addItem(QString::fromStdString(p));
    v->addWidget(list, 1);

    auto* row = new QHBoxLayout();
    auto* add = new QPushButton("Add…", page);
    add->setObjectName("preferences.search.add.button");
    auto* rem = new QPushButton("Remove", page);
    rem->setObjectName("preferences.search.remove.button");
    row->addWidget(add); row->addWidget(rem); row->addStretch(1);
    v->addLayout(row);

    auto syncToSett = [list]() {
        sett.searchPaths.clear();
        for (int i = 0; i < list->count(); ++i)
            sett.searchPaths.push_back(list->item(i)->text().toStdString());
    };
    connect(add, &QPushButton::clicked, page, [page, list, syncToSett]() {
        const QString d = QFileDialog::getExistingDirectory(page, "Add search path");
        if (!d.isEmpty()) { list->addItem(d); syncToSett(); }
    });
    connect(rem, &QPushButton::clicked, page, [list, syncToSett]() {
        qDeleteAll(list->selectedItems()); syncToSett();
    });

    addPage("Search Paths", page);
}
```

  Call `buildSearchPathsPage()` in the constructor's page-build sequence (replace one of the placeholder calls / add a new sidebar entry). Add `#include <QFileDialog>` if not present.

- [ ] **Step 3: Persist search keys.** In `loadPreferences()`: read `Search/useSearchPaths`, `Search/recursive`, and `Search/paths` (a `QStringList` → fill `sett.searchPaths`). In `writePreferences()`: write them (convert `sett.searchPaths` → `QStringList`).

- [ ] **Step 4: Build + verify.** Build; add a directory containing a frame, enable search paths, load a sequence by a bare filename that only resolves via that path → it loads. Disable → it fails to resolve. Persists across relaunch.

- [ ] **Step 5: Commit.**

```bash
git add -A
git commit -m "JEF-16: Search Paths preferences section + wiring (useSearchPaths/recursive/paths)"
```

---

## Task 5: Remote section (new) — nickname, chat group, remote-pointer group, load-request toggles

**Files:** `src/qt/PreferencesWindow_qt.{h,cpp}` (new `buildRemotePage`), `src/qt/qt_prefs_persist.cpp`. Consumers: `gfcnetworkmanager.cpp`, `gfcnetworkclient.cpp`, `gfctrackmanager.cpp`, `gfcPlate.cpp`.

**Interfaces:** Produces persisted keys under `Remote/*` for: `nickName` (string), `chatFadeDelay` (float), `chatAutoFade` (int), `chatTextBG` (int), `chatFontSize` (int), `chatOpacity` (float), `chatDisplayLines` (int), `remotePointerFadeDelay` (float), `remotePointerColor` (int RGBA-packed), `sendRemoteLoadRequests` (int), `autoAcceptRemoteLoadRequests` (int).

- [ ] **Step 1: Add `buildRemotePage()`** replacing the Remote placeholder. Group with `section("Chat", ...)`, `section("Remote pointer", ...)`, and top-level nickname + load-request toggles. Widgets bind directly to the corresponding `sett` fields (all already consumed by the network managers). Use these object names: `preferences.remote.nickname.edit`, `preferences.remote.chatfade.spin`, `preferences.remote.chatautofade.check`, `preferences.remote.chattextbg.check`, `preferences.remote.chatfontsize.spin`, `preferences.remote.chatopacity.spin`, `preferences.remote.chatlines.spin`, `preferences.remote.pointerfade.spin`, `preferences.remote.pointercolor.button`, `preferences.remote.sendload.check`, `preferences.remote.autoaccept.check`. For `remotePointerColor` use a swatch `QPushButton` + `QColorDialog` (pack/unpack the int like the existing pointer-color code in `gfcnetworkclient.cpp`/`gfcPlate.cpp` — read that packing first and mirror it).

- [ ] **Step 2: Persist all `Remote/*` keys** in `loadPreferences()`/`writePreferences()`.

- [ ] **Step 3: Build + verify.** Build; set a nickname + chat font size + pointer color, Done. In a remote session (or via `--remote-test` two-process harness per developer_notes §26 if convenient) confirm the nickname/chat styling/pointer color reflect the settings. All persist across relaunch. (If a live session is impractical, at minimum verify the values persist and are read by the network manager at connect time by inspecting the consumer.)

- [ ] **Step 4: Commit.**

```bash
git add -A
git commit -m "JEF-16: Remote preferences section (nickname, chat, pointer, load-requests) + wiring"
```

---

## Task 6: Text section (new) — GfcTextRenderer preferences

**Files:** `src/qt/PreferencesWindow_qt.{h,cpp}` (new `buildTextPage`), `src/qt/qt_prefs_persist.cpp`. Consumer: `src/gfcTextRenderer.{h,cpp}` setter API.

**Interfaces:** Produces persisted keys under `Text/*`. The renderer's setters (from `gfcTextRenderer.h`): `setSize(float)`, `setColor(r,g,b,a)`, `setShadowEnabled(bool)`, `setShadowOffset(x,y)`, `setShadowColor(r,g,b,a)`, `setShadowBlur(float)`, `setHintMode(HintMode)`, `setFilterNearest(bool)`, `setGamma(float)`, plus font/bold via `loadFont()`/`loadBoldFont()`.

- [ ] **Step 1: Determine the current text-pref state.** `GfcTextRenderer` has no `gfcSettings` fields today (its prefs were FLTK-era, applied in `PreferencesCB` which the Qt build doesn't call). Decision: persist text prefs directly under `Text/*` in `QSettings` (no new `gfcSettings` fields needed) and apply them to `textRenderer()` via its setters both at startup (in `loadPreferences()` or a dedicated `applyTextPrefs()`) and live from the dialog. Read `gfcTextRenderer.h` to confirm the `HintMode` enum values.

- [ ] **Step 2: Add `applyTextPrefs()` to `qt_prefs_persist`** (reads `Text/*` from QSettings and calls the `textRenderer()` setters). Call it from `loadPreferences()` so text prefs apply at startup. Access the renderer via `textRenderer()` (declared in `gfcTextRenderer.h`).

- [ ] **Step 3: Build `buildTextPage()`** with controls: size (spin), color (swatch), hint mode (combo: Light/Normal/Force-autohint mapped to `HintMode`), filter (combo or checkbox Nearest/Linear → `setFilterNearest`), gamma (double spin 0.5–1.0), shadow enabled (check) + shadow offset/blur/color. Each control writes to `QSettings Text/*` **and** calls the corresponding `textRenderer()` setter live so the change is visible immediately (the renderer invalidates atlases on font/size change). Object names `preferences.text.*`.

- [ ] **Step 4: Verify live apply + persistence.** Build; open Preferences → Text; change size/hint/gamma/shadow → on-screen plate text (filename overlay, feedback message) updates. Values persist across relaunch and reapply at startup.

- [ ] **Step 5: Commit.**

```bash
git add -A
git commit -m "JEF-16: Text preferences section wired to GfcTextRenderer + QSettings persistence"
```

---

## Task 7: Struct cleanup — remove the 19 dead fields

Now that no page references them, delete the quadrant-④ dead fields (+ `processorPriority`, `mirrorPaths`, and the 5 EXR tonemap floats already unref'd by UI). Persistence agent confirmed **none are persisted**, so removal is save/load-safe; only in-code refs need clearing.

**Files:** `src/gfcStructures.h` (declarations + constructor initializers), `src/gfcStructures.cpp` (`addToRecentFXs` uses `maxRecentFXs`), and any residual refs.

- [ ] **Step 1: Remove field declarations + constructor initializers** in `gfcStructures.h` for: `processorPriority`, `exrExposure`, `exrDefog`, `exrGamma`, `exrKneeLow`, `exrKneeHigh`, `playbackOnLoad`, `textureCompression`, `maxRecentFXStacks`, `maxRecentFXs`, `defaultLUTName` (+`defaultLUTNameBackup`), `feedbackMessageOn`, `serverNickname`, `clientPort`, `serverPort`, `licensePath`, `playlistShowCompactView`, `playlistShowFullPaths`, `mirrorPaths` (+ its ctor loop at lines ~242-247).

- [ ] **Step 2: Fix the one live code ref to a removed field.** `gfcStructures.cpp:365` (`addToRecentFXs`) uses `maxRecentFXs`. Replace with a local constant `const size_t kMaxRecentFXs = 5;` (preserve the trimming behavior) or drop the trim if unused. Grep the tree for each removed field name to catch any straggler (`exrExposure` etc. live only in the excluded `gfcimageloaderexr.cpp`, which isn't built — safe to leave, but remove if trivial).

- [ ] **Step 3: Build clean.** `cmake --build build 2>&1 | tail -30` — expect no errors (the excluded EXR loader is not compiled, so its `exrExposure` refs don't break the build; confirm CMake still filters it).

- [ ] **Step 4: Run smoke test.** Launch, open every Preferences section, load an image, play — no crash; nothing references a removed field at runtime.

- [ ] **Step 5: Commit.**

```bash
git add -A
git commit -m "JEF-16: remove 19 dead gfcSettings fields (unused/obsolete/duplicated)"
```

---

## Task 8: Final verification + docs

**Files:** `CLAUDE.md` (update the Preferences description), `developer_notes.md` (add a Preferences section), `tests/ui/jefecheck/locators.py` (final object-name sweep).

- [ ] **Step 1: Full clean build.** `rm -rf build && cmake -B build && cmake --build build 2>&1 | tail -20` — clean.

- [ ] **Step 2: Walk every section in the running app** and confirm: General (bg color + checkerboard, browse path, fullscreen, load-at-startup, thumbnails, feedback, session behavior, crash recovery, aspect opacity), Playback & Engine, Formats, Search Paths, Remote, Text. Each control's value round-trips across quit+relaunch. Screenshot each section.

- [ ] **Step 3: Confirm object names** — grep the redesigned `PreferencesWindow_qt.cpp` for every `setObjectName` and reconcile with `tests/ui/jefecheck/locators.py`; add/rename constants so every control has a stable leaf. Remove the obsolete `prefs.engine.*` entries.

- [ ] **Step 4: Update docs.** In `CLAUDE.md`, replace the "Preferences System" note (XML/`saveSetting` description is now inaccurate — persistence is QSettings via `qt_prefs_persist`). Add a `developer_notes.md` section documenting the persistence backbone, the section structure, checkerboard, and the object-name scheme.

- [ ] **Step 5: Commit.**

```bash
git add -A
git commit -m "JEF-16: final verification, object-name sweep, docs (Preferences persistence + sections)"
```

---

## Self-review notes (coverage vs spec)

- Spec ① verify-wired → Task 2 Step 5 (Engine) + Task 8 Step 2 (General/others).
- Spec ② exposed-dead: `bgColor` T1, `defaultBrowsePath`/`startFullscreen`/`openLoadWindowAtStartup` T1, `processorPriority` removed T2/T7, EXR toggles T3, EXR tonemap removed T3/T7.
- Spec ③ hidden-wired UI: `showThumbnails`+feedback T1, chat/pointer/nickname/load-requests T5, `useSearchPaths` T4.
- Spec ④ hidden-dead removal → Task 7.
- Half-features: Search Paths completed T4; Mirror deferred (UI never added; struct field removed T7).
- Checkerboard feature → Task 1 Steps 1-3, 9.
- Persistence backbone (stubs finding) → Task 0; each section appends its keys.
- Cancel-revert → Task 0 Step 4.
- Object-name standardization → Task 2 Step 3 + Task 8 Step 3.
- Redesign chrome/tokens/CollapsibleSection → Task 0 Step 4, applied per section.

**Known execution-time unknowns (flagged, not placeholders):** exact `searchPaths` consumer behavior (T4 S1), `remotePointerColor` packing format (T5 S1), `HintMode` enum values (T6 S1) — each has an explicit "read the consumer first" step.
