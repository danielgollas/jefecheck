# Qt Session Save / Restore (+ CC Favorites) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the existing GUI-free `gfcSessionManager` into the Qt build — Save/Save As/Open/Recent `.jcs` sessions, configurable launch behavior, crash recovery — and fold in the color-correction favorites feature.

**Architecture:** New `jefe::qt::*` bridge accessors wrap `gfcSessionManager` (the bridge stays GL-context-free; MainWindow wraps loads in `makeCurrent`). MainWindow gains File-menu items, a current-session-path + window title, a `closeEvent` recovery write + clean-exit flag, and a preference-driven startup recovery step (alongside the LUT autoload). CC favorites get a View submenu, app-global persistence, and session serialization.

**Tech Stack:** C++20, Qt6 Widgets, `SequenceLoadBridge_qt` bridge, the shared `gfcSessionManager` (XML `.jcs`).

**Spec:** `docs/superpowers/specs/2026-06-21-qt-session-restore-design.md`

**Grounding (verified):**
- `gfcSessionManager sessionManager` is a global (`src/gfcsessionmanager.cpp:27`; `extern` in `AppContext.cpp:33`). Methods: `saveSession(std::string)`, `loadSession(std::string)`, `writeCrashSession()`, `bool checkCrashedSession()`, `loadCrashedSession()`, `removeCrashSession()` (`src/gfcsessionmanager.h:15-20`). `loadSession` calls `trackManager.stopLoadingAll()`, restores, and re-decodes preview frames (needs current GL context). `saveSession` writes `<root>` → `settings`/`plates`/`tracks`/`playlist` and maintains `sett.recentSessions` (cap `sett.maxRecentSessions=5`).
- `gfcStructures.h`: `sett.recentSessions` (vector<string>, line 362), `maxRecentSessions` (356), `enableCrashRecoverySession` (305).
- CC favorites (`gfcplatemanager.h:94-100`): `saveFavoriteColorCorrectionsToNode(XMLNode&)`, `loadFavoriteColorCorrectionsFromNode(XMLNode&)`, `setFavoriteColorCorrectionOnPlate(int)`, `saveFavoriteColorCorrectionFromPlate(int)` (slot is 0-based; act on active quad).
- MainWindow File menu built at `src/qt/MainWindow_qt.cpp:410-486`; `closeEvent` at 795 (calls `saveLayout`); startup autoload kicked at 304 (`startAutoload` → `autoloadStep`, which `makeCurrent`s); `restoreLayout`/`saveLayout` use `QSettings` (767/790).
- `gfcPlate.h`-style XML helpers (`saveSetting`, `readAttributeFromNode`) live in `gfcStructures.h`.

---

## Conventions for every task

- Build: `cmake --build build_qt -j` → `[100%] Built target jefecheck`. clangd "QWidget not found" diagnostics are false positives. Manual launch: `pkill -f jefecheck.app/Contents/MacOS/jefecheck; sleep 0.5; open build_qt/jefecheck.app`.
- No C++ unit harness; verify by build + manual launch. Automated tests are Appium/Python.
- Commit each task; stage explicit file lists. End commit messages with: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`

## File Structure

- **`src/gfcStructures.h`** — `int startupSessionBehavior` setting (0 Empty / 1 Reopen / 2 Ask).
- **`src/gfcsessionmanager.cpp`** — serialize CC favorites in `saveSession`/`loadSession`.
- **`src/qt/SequenceLoadBridge_qt.{h,cpp}`** — session/recovery/recent/CC-favorite accessors.
- **`src/qt/MainWindow_qt.{h,cpp}`** — File menu, current-session path/title, closeEvent + clean-exit flag, startup recovery, View → CC Favorites, QSettings persistence + startup seeding.
- **`src/qt/PreferencesWindow_qt.cpp`** — `On launch` combo.
- **`tests/ui/...`**, **`developer_notes.md`**.

---

## Task 1: `startupSessionBehavior` setting

**Files:** Modify `src/gfcStructures.h`

- [ ] **Step 1: Add field + default**

In `gfcStructures.h`, in the `gfcSettings` constructor defaults block (near `maxRecentSessions=5;`, line ~267) add:
```cpp
		startupSessionBehavior=2;  // 0=Empty, 1=Reopen last, 2=Ask
```
And in the member declarations (near `int maxRecentSessions;`, line ~356) add:
```cpp
    int startupSessionBehavior;
```

- [ ] **Step 2: Build**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`.

- [ ] **Step 3: Commit**

```bash
git add src/gfcStructures.h
git commit -m "settings: add startupSessionBehavior (empty/reopen/ask)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Serialize CC favorites in sessions

**Files:** Modify `src/gfcsessionmanager.cpp`

- [ ] **Step 1: Save favorites node**

In `gfcSessionManager::saveSession`, after the tracks node is added (`trackManager.saveTrackSessionParameters(tracksNode);`, ~line 173) and before the playlist save, add:
```cpp
	// Color-correction favorites (5 slots) ride along in the session.
	XMLNode favsNode=xRootNode.addChild("ccFavorites");
	plateManager.saveFavoriteColorCorrectionsToNode(favsNode);
```

- [ ] **Step 2: Load favorites node**

In `gfcSessionManager::loadSession`, after the plates are loaded (`plateManager.loadPlateSessionParameters(platesNode);` / `updateAllFromGUI()`, ~line 78-79) add:
```cpp
	// Restore CC favorites if the session carries them (older sessions won't).
	XMLNode favsNode=xRootNode.getChildNode("ccFavorites");
	if (!favsNode.isEmpty())
		plateManager.loadFavoriteColorCorrectionsFromNode(favsNode);
```
(If `XMLNode` has no `isEmpty()`, use `!favsNode.isEmpty()` → guard with `favsNode.getName()!=NULL` instead; check the xmlParser API the file already uses for other optional nodes.)

- [ ] **Step 3: Build**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`.

- [ ] **Step 4: Commit**

```bash
git add src/gfcsessionmanager.cpp
git commit -m "session: serialize CC favorites in save/loadSession

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Bridge accessors

**Files:** Modify `src/qt/SequenceLoadBridge_qt.h`, `src/qt/SequenceLoadBridge_qt.cpp`

- [ ] **Step 1: Declarations (header)**

Before `}  // namespace jefe::qt`, add:
```cpp
// Session save/load. loadSession decodes preview frames, so the CALLER must
// make the viewport's GL context current first (the bridge is context-free).
bool saveSession(const std::string& path);
bool loadSession(const std::string& path);
// Recovery / last-session file (getApplicationDataPath()+recoverySession.jcs).
bool getHasRecoverableSession();
bool loadRecoverySession();     // load it; does NOT delete (caller decides)
void writeRecoverySession();    // write it unconditionally
void removeRecoverySession();
// Recent sessions (manager keeps sett.recentSessions, cap 5).
std::vector<std::string> getRecentSessions();
void setRecentSessions(const std::vector<std::string>& paths);
// Startup-session preference (0 Empty / 1 Reopen / 2 Ask).
int  getStartupSessionBehavior();
void setStartupSessionBehavior(int mode);
// Color-correction favorites (slot 0..4, on the active plate).
void saveCCFavoriteFromActive(int slot);
void applyCCFavoriteToActive(int slot);
bool saveCCFavoritesFile(const std::string& path);
bool loadCCFavoritesFile(const std::string& path);
std::string getFavoritesFilePath();   // getApplicationDataPath()+favorites.jcs
```

- [ ] **Step 2: Implementation (cpp)**

The bridge `.cpp` already has `extern gfcSettings sett;` and includes the managers; add `extern gfcSessionManager sessionManager;` near the other externs, `#include "../gfcsessionmanager.h"` with the other manager includes, and `#include "../xmlParser.h"` (for the favorites file node). Add the functions inside `namespace jefe::qt`:
```cpp
bool saveSession(const std::string& path) {
    if (path.empty()) return false;
    sessionManager.saveSession(path);
    return true;
}

bool loadSession(const std::string& path) {
    if (path.empty()) return false;
    sessionManager.loadSession(path);   // caller made GL context current
    plateManager.setChanged();
    return true;
}

bool getHasRecoverableSession() { return sessionManager.checkCrashedSession(); }

bool loadRecoverySession() {
    if (!sessionManager.checkCrashedSession()) return false;
    // Load the recovery file but leave it in place (loadCrashedSession deletes;
    // we want it to also serve "reopen last session").
    sessionManager.loadSession(::getApplicationDataPath() + "recoverySession.jcs");
    plateManager.setChanged();
    return true;
}

void writeRecoverySession() {
    // Unconditional (not gated on enableCrashRecoverySession) so "reopen last"
    // always has content; the launch preference governs whether it's consumed.
    sessionManager.saveSession(::getApplicationDataPath() + "recoverySession.jcs");
}

void removeRecoverySession() { sessionManager.removeCrashSession(); }

std::vector<std::string> getRecentSessions() { return sett.recentSessions; }
void setRecentSessions(const std::vector<std::string>& paths) {
    sett.recentSessions = paths;
    if ((int)sett.recentSessions.size() > sett.maxRecentSessions)
        sett.recentSessions.resize(sett.maxRecentSessions);
}

int  getStartupSessionBehavior() { return sett.startupSessionBehavior; }
void setStartupSessionBehavior(int mode) { sett.startupSessionBehavior = mode; }

void saveCCFavoriteFromActive(int slot) {
    plateManager.saveFavoriteColorCorrectionFromPlate(slot);  // active quad
}
void applyCCFavoriteToActive(int slot) {
    plateManager.setFavoriteColorCorrectionOnPlate(slot);     // active quad
    plateManager.setChanged();
}

bool saveCCFavoritesFile(const std::string& path) {
    XMLNode root = XMLNode::createXMLTopNode("ccFavorites");
    plateManager.saveFavoriteColorCorrectionsToNode(root);
    return root.writeToFile(path.c_str()) == 0;
}
bool loadCCFavoritesFile(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(path, ec)) return false;
    XMLNode root = XMLNode::openFileHelper(path.c_str(), "ccFavorites");
    if (root.isEmpty()) return false;
    plateManager.loadFavoriteColorCorrectionsFromNode(root);
    return true;
}

std::string getFavoritesFilePath() {
    return ::getApplicationDataPath() + "favorites.jcs";
}
```
(If `XMLNode::createXMLTopNode`/`openFileHelper` arities differ, match the calls already in `gfcsessionmanager.cpp` — `createXMLTopNode("xml",TRUE)` then `addChild`, and `openFileHelper(file)` then `getChildNode`. Adjust the favorites-file helpers to that style if the single-arg forms don't exist.)

- [ ] **Step 3: Build**

Run: `cmake --build build_qt -j` → `[100%] Built target jefecheck`.

- [ ] **Step 4: Commit**

```bash
git add src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp
git commit -m "qt bridge: session save/load, recovery, recent, CC-favorite accessors

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: File menu — Save / Save As / Open / Recent

**Files:** Modify `src/qt/MainWindow_qt.h`, `src/qt/MainWindow_qt.cpp`

- [ ] **Step 1: Header members + methods**

In `MainWindow_Qt` (`MainWindow_qt.h`) private section add:
```cpp
    void doSaveSession(bool forceDialog);
    void doOpenSession();
    void openSessionPath(const QString& path);   // GL-current load + bookkeeping
    void rebuildRecentSessionsMenu();
    void updateSessionTitle();

    QString currentSessionPath_;
    QMenu*  recentMenu_ = nullptr;
```
Add `#include <QString>` if not present and forward-declare `class QMenu;` (likely already there).

- [ ] **Step 2: Menu items (cpp)**

In `buildMenuBar`'s File menu, after the `loadMgrAction` block and before `"&Render…"`, insert:
```cpp
    fileMenu->addSeparator();
    auto* saveSessAction = fileMenu->addAction(tr("&Save Session"),
        QKeySequence(QKeySequence::Save), this, [this]() { doSaveSession(false); });
    saveSessAction->setObjectName("menu.file.savesession");
    auto* saveAsAction = fileMenu->addAction(tr("Save Session &As…"),
        this, [this]() { doSaveSession(true); });
    saveAsAction->setObjectName("menu.file.savesessionas");
    auto* openSessAction = fileMenu->addAction(tr("&Open Session…"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O),
        this, [this]() { doOpenSession(); });
    openSessAction->setObjectName("menu.file.opensession");
    recentMenu_ = fileMenu->addMenu(tr("Recent Sessions"));
    recentMenu_->setObjectName("menu.file.recent");
    connect(fileMenu, &QMenu::aboutToShow, this, [this]() { rebuildRecentSessionsMenu(); });
```
Add includes `<QFileDialog>`, `<QMessageBox>` to the cpp if not present.

- [ ] **Step 3: Method implementations (cpp)**

Add near `saveLayout`:
```cpp
void MainWindow_Qt::doSaveSession(bool forceDialog) {
    QString path = currentSessionPath_;
    if (forceDialog || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("Save Session"),
                   path.isEmpty() ? QString() : path, tr("JefeCheck Session (*.jcs)"));
        if (path.isEmpty()) return;
    }
    if (jefe::qt::saveSession(path.toStdString())) {
        currentSessionPath_ = path;
        updateSessionTitle();
        statusBar()->showMessage(tr("Saved session: %1").arg(QFileInfo(path).fileName()), 4000);
    } else {
        QMessageBox::warning(this, tr("Save Session"), tr("Could not save the session."));
    }
}

void MainWindow_Qt::doOpenSession() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Session"),
                             QString(), tr("JefeCheck Session (*.jcs)"));
    if (path.isEmpty()) return;
    openSessionPath(path);
}

void MainWindow_Qt::openSessionPath(const QString& path) {
    if (!viewport_) return;
    viewport_->makeCurrent();                 // loadSession uploads preview textures
    const bool ok = jefe::qt::loadSession(path.toStdString());
    viewport_->doneCurrent();
    if (ok) {
        currentSessionPath_ = path;
        updateSessionTitle();
        if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();
        viewport_->update();
        statusBar()->showMessage(tr("Opened session: %1").arg(QFileInfo(path).fileName()), 4000);
    } else {
        QMessageBox::warning(this, tr("Open Session"), tr("Could not open the session."));
    }
}

void MainWindow_Qt::rebuildRecentSessionsMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    const auto recents = jefe::qt::getRecentSessions();
    bool any = false;
    for (auto it = recents.rbegin(); it != recents.rend(); ++it) {   // newest first
        const QString p = QString::fromStdString(*it);
        if (!QFileInfo::exists(p)) continue;                          // prune missing
        any = true;
        QAction* a = recentMenu_->addAction(QFileInfo(p).fileName());
        a->setToolTip(p);
        connect(a, &QAction::triggered, this, [this, p]() { openSessionPath(p); });
    }
    if (!any) recentMenu_->addAction(tr("(none)"))->setEnabled(false);
}

void MainWindow_Qt::updateSessionTitle() {
    if (currentSessionPath_.isEmpty()) setWindowTitle("JefeCheck");
    else setWindowTitle(QString("JefeCheck — %1").arg(QFileInfo(currentSessionPath_).fileName()));
}
```
Add `#include <QFileInfo>` to the cpp.

- [ ] **Step 4: Build + manual verify**

Run: `cmake --build build_qt -j`. Launch. File menu shows Save Session (Cmd+S) / Save Session As… / Open Session… (Cmd+Shift+O) / Recent Sessions. Load a clip, tweak a plate, **Save Session** to a path; unload (or relaunch fresh), **Open Session** → the setup restores; title shows the session name; Recent submenu lists it.

- [ ] **Step 5: Commit**

```bash
git add src/qt/MainWindow_qt.h src/qt/MainWindow_qt.cpp
git commit -m "qt: File-menu Save/Save As/Open/Recent sessions

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Recovery flow + persistence + startup seeding

**Files:** Modify `src/qt/MainWindow_qt.cpp` (+ `.h` if needed)

- [ ] **Step 1: Seed recent + favorites + preference at startup**

In the `MainWindow_Qt` constructor, near where other `QSettings`-backed values are restored (after `restoreLayout()` or alongside the depth-combo restore), add:
```cpp
    {
        QSettings s;
        // Recent sessions (stored as a QStringList).
        std::vector<std::string> recents;
        for (const QString& p : s.value("Session/recent").toStringList())
            recents.push_back(p.toStdString());
        jefe::qt::setRecentSessions(recents);
        // Launch preference.
        jefe::qt::setStartupSessionBehavior(
            s.value("Session/startupBehavior", 2).toInt());
    }
```

- [ ] **Step 2: Clean-exit flag — read at startup, write on close**

Still in the constructor (early, before the autoload kick), capture the previous run's clean-exit flag and immediately mark this run "unclean until proven otherwise":
```cpp
    {
        QSettings s;
        lastExitWasClean_ = s.value("Session/cleanExit", true).toBool();
        s.setValue("Session/cleanExit", false);
    }
```
Add `bool lastExitWasClean_ = true;` to the header. In `closeEvent`, before `saveLayout()`, add:
```cpp
    jefe::qt::writeRecoverySession();
    {
        QSettings s;
        s.setValue("Session/cleanExit", true);
        // Persist recent sessions (loadSession/saveSession updated sett).
        QStringList rs;
        for (const auto& p : jefe::qt::getRecentSessions())
            rs << QString::fromStdString(p);
        s.setValue("Session/recent", rs);
    }
```

- [ ] **Step 3: Load app-global CC favorites at startup; save on change**

In the constructor (after the GL bits / before autoload is fine — `loadCCFavoritesFile` is pure data, no GL), add:
```cpp
    jefe::qt::loadCCFavoritesFile(
        (::getApplicationDataPath() + "favorites.jcs"));
```
`getApplicationDataPath` isn't reachable from this Qt TU (rendering chain), so the path comes through the bridge accessor declared in Task 3 — replace the snippet above with:
```cpp
    jefe::qt::loadCCFavoritesFile(jefe::qt::getFavoritesFilePath());
```

- [ ] **Step 4: Startup recovery (preference-driven), after GL is alive**

The autoload runs via `startAutoload()` kicked at constructor end (`QTimer::singleShot(250, …)`) and makes the GL context current itself. Add a session-recovery step that runs **after** the GL context is alive. Simplest: at the end of `startAutoload()` (after `initializeTextRenderer` / before/after the LUT autoload kick — anywhere the viewport context is current), add a deferred recovery:
```cpp
    QTimer::singleShot(300, this, [this]() { maybeRestoreSessionAtStartup(); });
```
And implement (header decl `void maybeRestoreSessionAtStartup();`):
```cpp
void MainWindow_Qt::maybeRestoreSessionAtStartup() {
    if (!viewport_) return;
    if (!jefe::qt::getHasRecoverableSession()) return;
    const int mode = jefe::qt::getStartupSessionBehavior();  // 0 empty,1 reopen,2 ask
    auto doLoad = [this]() {
        viewport_->makeCurrent();
        jefe::qt::loadRecoverySession();
        viewport_->doneCurrent();
        if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();
        viewport_->update();
    };
    if (mode == 1) { doLoad(); return; }                      // Reopen
    if (mode == 0 && lastExitWasClean_) return;               // Empty + clean → nothing
    // Ask (mode 2), or Empty after an unclean exit → prompt.
    const QString msg = lastExitWasClean_
        ? tr("Reopen your last session?")
        : tr("JefeCheck didn't close normally last time. Recover the previous session?");
    if (QMessageBox::question(this, tr("Session"), msg) == QMessageBox::Yes) doLoad();
}
```

- [ ] **Step 5: Build + manual verify**

Run: `cmake --build build_qt -j`. Launch. Verify each mode (change the pref in Task 6, or temporarily hardcode): load a setup, quit cleanly, relaunch → "Reopen last session?" prompt (Ask mode); Yes restores it. `kill -9` the app mid-session, relaunch → "didn't close normally… Recover?" prompt. Recent sessions persist across a clean relaunch (File → Recent).

- [ ] **Step 6: Commit**

```bash
git add src/qt/MainWindow_qt.h src/qt/MainWindow_qt.cpp src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp
git commit -m "qt: session recovery flow + recent/favorites persistence + startup seeding

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: `On launch` preference

**Files:** Modify `src/qt/PreferencesWindow_qt.cpp` (read the file first to match its panel/wiring pattern — it mirrors the depth/decode-filter combos)

- [ ] **Step 1: Add the combo**

In the General/Engine section of the Preferences dialog, add a labeled `QComboBox` "On launch:" with items in order **Start empty** (0), **Reopen last session** (1), **Ask** (2). Object name `prefs.session.startup`. Initialize its index from `QSettings("Session/startupBehavior", 2)`. On change:
```cpp
    connect(startupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [startupCombo](int idx) {
        QSettings s;
        s.setValue("Session/startupBehavior", idx);
        jefe::qt::setStartupSessionBehavior(idx);
    });
```
(Match the include + `jefe::qt` usage already present for `setDefaultDecodeFilter` in this file.)

- [ ] **Step 2: Build + verify**

Run: `cmake --build build_qt -j`. Launch, open Preferences, find "On launch:", change it, relaunch, confirm the startup behavior matches the selection and the combo restores the saved value.

- [ ] **Step 3: Commit**

```bash
git add src/qt/PreferencesWindow_qt.cpp
git commit -m "qt prefs: On launch (empty/reopen/ask) session behavior

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: View → Color Correction Favorites submenu

**Files:** Modify `src/qt/MainWindow_qt.cpp`

- [ ] **Step 1: Build the submenu**

In `buildMenuBar`'s View menu, add:
```cpp
    viewMenu->addSeparator();
    auto* ccFavMenu = viewMenu->addMenu(tr("Color Correction Favorites"));
    ccFavMenu->setObjectName("menu.view.ccfavorites");
    for (int i = 0; i < 5; ++i) {
        QAction* save = ccFavMenu->addAction(tr("Save to Slot %1").arg(i + 1));
        save->setObjectName(QString("menu.view.ccfav.save.%1").arg(i));
        connect(save, &QAction::triggered, this, [this, i]() {
            jefe::qt::saveCCFavoriteFromActive(i);
            jefe::qt::saveCCFavoritesFile(jefe::qt::getFavoritesFilePath());
            statusBar()->showMessage(tr("Saved color correction to favorite %1").arg(i + 1), 3000);
        });
    }
    ccFavMenu->addSeparator();
    for (int i = 0; i < 5; ++i) {
        QAction* load = ccFavMenu->addAction(tr("Load Slot %1").arg(i + 1));
        load->setObjectName(QString("menu.view.ccfav.load.%1").arg(i));
        connect(load, &QAction::triggered, this, [this, i]() {
            jefe::qt::applyCCFavoriteToActive(i);
            if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();
            if (viewport_) viewport_->update();
            statusBar()->showMessage(tr("Loaded color correction favorite %1").arg(i + 1), 3000);
        });
    }
```

- [ ] **Step 2: Build + manual verify**

Run: `cmake --build build_qt -j`. Launch, load a clip, set a distinctive color correction on the active plate, **Save to Slot 1**. Reset/change the CC, then **Load Slot 1** → the plate's gamma/exposure/BCS spinboxes return to the saved values and the image reflects it. Quit cleanly, relaunch → Load Slot 1 still restores it (app-global `favorites.jcs`).

- [ ] **Step 3: Commit**

```bash
git add src/qt/MainWindow_qt.cpp
git commit -m "qt: View → Color Correction Favorites (save/load 5 slots)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Locators + smoke test + docs

**Files:** Modify `tests/ui/jefecheck/locators.py`, create `tests/ui/test_session.py`, modify `developer_notes.md`

- [ ] **Step 1: Locators**

In `tests/ui/jefecheck/locators.py`, near the menu locators:
```python
MENU_FILE_SAVE_SESSION = "menu.file.savesession"
MENU_FILE_OPEN_SESSION = "menu.file.opensession"
MENU_FILE_RECENT = "menu.file.recent"
MENU_VIEW_CCFAVORITES = "menu.view.ccfavorites"
```

- [ ] **Step 2: Smoke test**

Create `tests/ui/test_session.py`:
```python
"""Session menu presence.

macOS folds Qt's menu bar into the system menu bar (separate AX tree), so
menu QActions aren't reliably addressable by objectName — these constants
document the canonical names; functional save/open is verified manually per
the plan. This file keeps the locators importable and parse-checked.
"""
from jefecheck import locators


def test_session_locator_constants_exist():
    assert locators.MENU_FILE_SAVE_SESSION == "menu.file.savesession"
    assert locators.MENU_FILE_OPEN_SESSION == "menu.file.opensession"
    assert locators.MENU_VIEW_CCFAVORITES == "menu.view.ccfavorites"
```

- [ ] **Step 3: Validate parse**

Run: `python3 -c "import ast; ast.parse(open('tests/ui/test_session.py').read()); ast.parse(open('tests/ui/jefecheck/locators.py').read()); print('parse OK')"` → `parse OK`.

- [ ] **Step 4: Dev notes**

Add a `developer_notes.md` section (after §16): session save/restore wires the GUI-free `gfcSessionManager` through `jefe::qt::*`; **`loadSession`/`loadRecoverySession` must run with the viewport GL context current** (they upload preview textures) — MainWindow wraps them in `makeCurrent`; `writeRecoverySession` is unconditional (the `On launch` preference governs consumption, not writing); the clean-exit `QSettings` flag (`Session/cleanExit`, set on close / cleared at startup) distinguishes crash vs clean for the recovery prompt; recent sessions persist in `QSettings("Session/recent")` (cap 5); CC favorites persist app-globally in `favorites.jcs` *and* embed in each `.jcs` session. Note the `updateAllFromGUI` caveat (§2): if framing/active-quad don't restore correctly after Open Session, that's where to look.

- [ ] **Step 5: Commit**

```bash
git add tests/ui/jefecheck/locators.py tests/ui/test_session.py developer_notes.md
git commit -m "qt: session locators + smoke test + developer notes

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Final review (after all tasks)

- Build green; all commits present.
- Manual: Save/Open round-trips a multi-plate setup (footage + transforms + color + LUT/FX); each `On launch` mode; crash recovery prompt after a `kill -9`; Recent submenu prunes a moved file; CC favorites save/load + persist across restart.
- **Watch the `updateAllFromGUI` caveat** (developer_notes §2): verify framing mode + active quad restore correctly after Open Session; if not, re-apply `framingMode` post-load in the bridge `loadSession`.
- Open a PR against `qt-experimental`; squash-merge after review. Update `docs/fltk-parity-gaps.md` (mark Session A + CC favorites done) and the backlog memory.
