# Qt Session Save / Restore (+ CC Favorites) — Design Spec

**Status:** Approved (2026-06-21)
**Branch target:** `qt-experimental`

## Goal

Wire the existing (FLTK-era, GUI-free) `gfcSessionManager` into the Qt build so users can **save / open named `.jcs` sessions**, see **recent sessions**, and **recover** the previous session on launch (configurable). Also fold in the **color-correction favorites** (5 slots) feature, which shares the same persistence theme. This is almost entirely *wiring* — the serialization/restore machinery already works.

## Background / current state

- **`gfcSessionManager`** (`src/gfcsessionmanager.{h,cpp}`) is fully functional and **GUI-free** (no FLTK/Qt, no file chooser — the caller passes the path):
  - `saveSession(std::string filename)` — serializes the whole state to `.jcs` XML (auto-appends `.jcs`); maintains `sett.recentSessions` (cap `sett.maxRecentSessions = 5`).
  - `loadSession(std::string filename)` — parses the XML, stops in-flight loads, restores settings/plates/tracks/FX/playlist, **and kicks off the actual sequence reload** through the managers.
  - `writeCrashSession()` (→ `saveSession(crashSessionName)` when `sett.enableCrashRecoverySession`), `checkCrashedSession()` (→ `fileExists`), `loadCrashedSession()` (→ load + remove), `removeCrashSession()`.
  - `crashSessionName = getApplicationDataPath() + "recoverySession.jcs"`.
  - `rebuildRecentSessionsMenu()` is a stub (was FLTK menu code).
- **What a `.jcs` persists** (from `saveSession`/`loadSession` + the plate/track serializers): settings (`framingMode`, `filtering`, `loopMode`, `loopPriority`, `targetFPS`, `from`, `to`); per-plate (`trackID`, `tX/tY/rZ/scale`, channel mask `r/g/b/a`, `gamma/exposure/brightness/contrast/saturation`, `flip/flop/crop`, `aspect`, `lut` name, FX `<stack>`); per-track load params (`filename`, `from/to`, `scale`, `filter`, `crop`, AOI, `depth`, `offset`, `holdMode/holdFrame`); the playlist. **`loadSession` re-decodes preview frames → it needs a current GL context.**
- **GL-context implication:** restoring (and any `loadSession`) uploads textures, so the Qt caller must run it with the viewport's context current (`makeCurrent`/`doneCurrent`) — exactly like `MainWindow_Qt::autoloadStep` / `initializeInstallLUTs`.
- **Qt today:** no session bridge accessors; File menu = Quick Load / Load Sequence Manager / Render / Remote Session / Preferences / Quit (no Save/Open Session); `closeEvent` saves *layout* only (`saveLayout`), not session; `main_qt.cpp` never checks for a crashed session; `sett.recentSessions` is runtime-only (not persisted).
- **CC favorites** (`gfcPlateManager`): `saveFavoriteColorCorrectionFromPlate(slot[,plate])`, `setFavoriteColorCorrectionOnPlate(slot[,plate])`, `getAllFavoriteColorCorrections()`, `saveFavoriteColorCorrectionsToNode` / `loadFavoriteColorCorrectionsFromNode` (XML). Functional but **runtime-only** — `saveSession`/`loadSession` do *not* serialize them, and no Qt UI reaches them.

## Decisions (from brainstorming)

1. **Launch behavior is a Preference** (`On launch:` Start empty / Reopen last session / Ask). Default **Ask**.
2. **Full explicit UI** in v1: Save Session, Save Session As…, Open Session…, **Recent Sessions** submenu.
3. **Fold in CC favorites**: a View → Color Correction Favorites submenu (Save/Load 5 slots), **app-global persistence** (survive restart like FLTK), and included in session save/load. **Menu-only** (no `Ctrl+1–5` shortcuts — they'd collide with the shipped `Ctrl+1–4` layout shortcuts).
4. **Open Session = `Cmd+Shift+O`** (the reserved convention). **Save = `Cmd+S`**.

## Architecture

### Bridge accessors (`src/qt/SequenceLoadBridge_qt.{h,cpp}`)

```cpp
// Session save/load. loadSession decodes preview frames, so the CALLER must
// make the viewport's GL context current first (the bridge is context-free).
bool saveSession(const std::string& path);          // → sessionManager.saveSession
bool loadSession(const std::string& path);           // → sessionManager.loadSession; setChanged
// Recovery (last-session) file.
bool   hasRecoverableSession();                       // → checkCrashedSession
bool   loadRecoverySession();                         // load crashSessionName (GL-current; does NOT delete)
void   writeRecoverySession();                        // → writeCrashSession
void   removeRecoverySession();                       // → removeCrashSession
// Recent sessions (manager maintains sett.recentSessions; these expose/seed it).
std::vector<std::string> getRecentSessions();
void   setRecentSessions(const std::vector<std::string>& paths);  // seed sett at startup
// CC favorites.
void saveCCFavoriteFromActive(int slot);              // slot 0..4 ← active plate
void applyCCFavoriteToActive(int slot);               // slot 0..4 → active plate; setChanged
bool saveCCFavoritesFile(const std::string& path);    // saveFavoriteColorCorrectionsToNode → file
bool loadCCFavoritesFile(const std::string& path);    // loadFavoriteColorCorrectionsFromNode ← file
```

`loadRecoverySession` deliberately does **not** delete the file (unlike `loadCrashedSession`) — the MainWindow controls deletion so the file can also serve "reopen last session". `writeRecoverySession` writes the recovery file **unconditionally** (it must not gate on the legacy `enableCrashRecoverySession`, or "Reopen last session" would break) — the `On launch` preference governs whether the file is *consumed*, not whether it's written.

### File menu + session state (`src/qt/MainWindow_qt.{h,cpp}`)

- New File-menu items after "Load Sequence Manager…": **Save Session** (`Cmd+S`), **Save Session As…**, **Open Session…** (`Cmd+Shift+O`), and a **Recent Sessions ▸** submenu; a separator before Preferences.
- A `QString currentSessionPath_`. **Save** → if empty, fall through to Save As; else `saveSession(currentSessionPath_)`. **Save As / Open** → `QFileDialog` with a `*.jcs` filter; Open wraps the bridge call in `viewport_->makeCurrent()/doneCurrent()`. Window title reflects the session name.
- **Recent Sessions** submenu rebuilt from `getRecentSessions()` each time the File menu opens (or after save/open); each entry opens that path (GL-current); non-existent paths are pruned. Persisted in `QSettings` (`Session/recent`, cap 5), seeded into `sett` at startup via `setRecentSessions`.

### Launch / recovery flow

- **Clean-exit flag** in `QSettings` (`Session/cleanExit`): read at startup → tells crash vs clean; then immediately set to `false` (so a crash this run leaves it `false`); set to `true` in `closeEvent`.
- **`closeEvent`** also calls `writeRecoverySession()` (so "reopen last" has content) in addition to the existing `saveLayout()`.
- **Startup** (after the GL context is alive — same place as `initializeInstallLUTs`, wrapped in `makeCurrent`): read the `On launch` preference + clean-exit flag, and if `hasRecoverableSession()`:
  - **Start empty:** load nothing; only if the last exit was *unclean*, prompt "Recover previous session?" → on Yes, `loadRecoverySession()`.
  - **Reopen last session:** `loadRecoverySession()` unconditionally.
  - **Ask:** prompt "Reopen last session?" (Yes/No) → on Yes, `loadRecoverySession()`. (Phrase as "Recover" when the exit was unclean.)

### Preference (`On launch:`)

- New `gfcSettings` field (e.g. `int startupSessionBehavior`; 0=Empty, 1=Reopen, 2=Ask, default 2) + a Preferences widget (a combo in the General/Engine area, matching the existing prefs pattern), persisted in `QSettings` and seeded into `sett` at startup. Folds in the old `enableCrashRecoverySession` intent (Empty ≈ crash-recovery-only).

### CC favorites

- **Menu**: View → **Color Correction Favorites ▸** with "Save to Slot 1–5" → `saveCCFavoriteFromActive(n)` and "Load Slot 1–5" → `applyCCFavoriteToActive(n)` (then refresh the active plate card via the existing `refreshAllCards`/propagate path so the spinboxes update). Menu-only (no `Ctrl`-number shortcuts).
- **App-global persistence**: favorites live in `getApplicationDataPath() + "favorites.jcs"`; `loadCCFavoritesFile` at startup, `saveCCFavoritesFile` whenever a slot is saved — so favorites persist across launches independent of sessions (FLTK behavior).
- **In sessions**: extend `gfcSessionManager::saveSession`/`loadSession` to also write/read the favorites node (`saveFavoriteColorCorrectionsToNode`/`loadFavoriteColorCorrectionsFromNode`) so a named `.jcs` captures its favorites too. (Small, additive change in the shared session code.)

## Error handling

- **Missing footage** on restore: the load path already tolerates bad filenames per track (that track just fails to load); no crash. Surface a status-bar note.
- **Corrupt / unreadable `.jcs`**: report via status bar / a `QMessageBox` and abort the load — don't half-apply. `saveSession` failures (bad path/permissions) report and leave `currentSessionPath_` unchanged.
- **No active plate** for CC-favorite apply: no-op.

## Out of scope (v1)

- Standalone **Playlist Manager `.jpl`** save/load (sessions already embed playlist items).
- **Save Chat Log**.
- Per-favorite keyboard shortcuts (menu-only for now, to avoid the layout-shortcut clash).

## Files

- **Modify:** `src/qt/SequenceLoadBridge_qt.{h,cpp}` — session + recovery + recent + CC-favorite accessors.
- **Modify:** `src/qt/MainWindow_qt.{h,cpp}` — File menu (Save/Save As/Open/Recent), `currentSessionPath_`, title, `closeEvent` recovery write + clean-exit flag, startup recovery flow, View → CC Favorites submenu, recent/favorites QSettings persistence + startup seeding.
- **Modify:** `src/qt/PreferencesWindow_qt.cpp` (+ `gfcStructures.h`) — `On launch` preference field.
- **Modify:** `src/gfcsessionmanager.cpp`, `src/gfcStructures.h` — serialize CC favorites in `saveSession`/`loadSession`; `startupSessionBehavior` default.
- **Modify:** `tests/ui/jefecheck/locators.py` + new `tests/ui/test_session.py`.

## Testing

- **Appium smoke:** File menu Save/Open Session items + Recent submenu resolve; View → CC Favorites resolves; toggling doesn't crash.
- **Round-trip (behavioral):** load a sequence into a plate with a transform/color tweak → Save Session → unload/clear → Open Session → assert the status-bar "Loaded:" + track readouts match (and a CC favorite saved then loaded changes the plate card values).
- **Manual:** save a multi-plate setup, relaunch, Open it (footage + plates restore); each `On launch` mode (empty / reopen / ask); simulate an unclean exit (kill) → recovery prompt; Recent submenu prunes a moved file; favorites persist across a clean restart.
