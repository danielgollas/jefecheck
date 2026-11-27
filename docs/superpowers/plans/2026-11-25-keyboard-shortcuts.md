# Customizable Keyboard Shortcuts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move every key binding in JefeCheck 2.0 into one registry of `QAction`s, add Editing and shipped-1.7 presets, and give Preferences a Shortcuts page with a searchable list, key capture, import/export and a keyboard map.

**Architecture:** `jefe::shortcuts::Registry` (Qt-side, `src/qt/`) owns one `QAction` per command and a small list of hold keys. Commands are declared as data in `ShortcutCommands_qt.cpp` and call existing `jefe::qt::*` bridge functions; window-level commands get their run function from `MainWindow_Qt` via `bindRun`. Global commands are `ApplicationShortcut` actions on the main window; Player commands are `WidgetShortcut` actions added to the viewport and the two painted timeline widgets.

**Tech Stack:** C++20, Qt6 Widgets (`QAction`, `QKeySequence`, `QKeySequenceEdit`, `QSettings`, `QJsonDocument`), CMake (`file(GLOB src/qt/*.cpp)` picks up new files after a re-configure), Appium/Mac2 UI tests in `tests/ui/`.

**Spec:** `docs/superpowers/specs/2026-11-25-keyboard-shortcuts-design.md` (approved 2026-11-25). Read it with this plan; the preset table there is the source of truth for every key.

## Global Constraints

- Base branch `qt-experimental`. Three PRs, one per phase: `feature/shortcuts-registry` (Tasks 1–6), `feature/shortcuts-presets` (Tasks 7–12, from phase 1 once merged), `feature/shortcuts-keymap` (Tasks 13–14). Never merge to `main`.
- §1 TU rule (`developer_notes.md` §1): no file under `src/qt/` except `SequenceLoadBridge_qt.cpp` includes rendering-chain headers. New backend calls go in `SequenceLoadBridge_qt.{h,cpp}`.
- §41 density: 13 px text, 24 px controls, 4/8/12 spacing from `UiMetrics_qt.h`; no `setFixedHeight` on controls; `--ui-density-test` must pass with the new page.
- Object names: `prefs.shortcuts.<leaf>` (e.g. `prefs.shortcuts.search`, `prefs.shortcuts.preset`, `prefs.shortcuts.import`, `prefs.shortcuts.export`, `prefs.shortcuts.reset`, `prefs.shortcuts.list`, `prefs.shortcuts.conflict.replace`, `prefs.shortcuts.keymap`); registry actions are named `shortcut.<command id>`.
- Keys are stored and exported as `QKeySequence::PortableText`; shown with `QKeySequence::NativeText`.
- Command ids are `category.verb-noun`, lowercase, and never renamed once shipped.
- Every project-own new file starts with `// SPDX-License-Identifier: GPL-2.0-or-later`.
- Up to two keys per command per preset. Modifiers match exactly.
- QSettings keys: `Shortcuts/preset` (`editing` | `classic`), `Shortcuts/overrides/<id>`, `Shortcuts/holdOverrides/<id>`.
- Export file: `.jcshortcuts`, JSON `{format:"jefecheck-shortcuts", version:1, basedOn, keys, hold}`.
- Self-test output convention: one `SHORTCUTS-TEST <check> PASS|FAIL <detail>` line per check, final `SHORTCUTS-TEST PASS|FAIL`, exit 0 / 2, `fflush(stdout)` before `std::_Exit`.
- Build: `cmake -B build_qt && cmake --build build_qt -j` (re-run the configure step after adding files). Binary: `build_qt/jefecheck.app/Contents/MacOS/jefecheck` on macOS.

## Review Focus

1. **Floating docks.** A Player action added to `TimelineScrubber_Qt` must still fire when the Timeline dock is floated (its own top-level window). Test in Task 6.
2. **Key-repeat on held keys.** Holding W fires auto-repeat press/release pairs; the hold set must not flicker to empty mid-drag, and a held key must clear on focus-out (not only on leave). Test in Task 4.
3. **macOS Cmd/Ctrl.** `Qt::CTRL` is Cmd on macOS; a `.jcshortcuts` written with `Ctrl+L` must load as Cmd+L on macOS and Ctrl+L elsewhere, and `Meta+Ctrl+F` (fullscreen) must round-trip. Test in Task 2.
4. **Keys the menu bar also wants.** On macOS the native menu bar resolves a key before the focused widget's `ShortcutOverride` for menu actions; a menu-hosted Global letter (F, H, M, T, V) must not fire while typing in a `QLineEdit` in a floating dock. Test in Task 6.
5. **Import of a broken or foreign file.** Invalid JSON, wrong `format`, a newer `version`, a key text Qt can't parse, or an empty file must leave the current keys untouched and return a one-line error. Test in Task 2.

---

## Phase 1 — Registry, no behaviour change (`feature/shortcuts-registry`)

### Task 1: Registry core

**Files:**
- Create: `src/qt/Shortcuts_qt.h`, `src/qt/Shortcuts_qt.cpp`
- Create: `src/qt/ShortcutsSelfTest_qt.cpp` (declared in `Shortcuts_qt.h`)
- Modify: `src/main_qt.cpp` (add `--shortcuts-test`, run after `QApplication` exists, before the window, next to `--fx-ui-test`)

**Interfaces:**
- Produces, in `namespace jefe::shortcuts`: the `Scope`, `Category`, `Command`, `HoldKey` types exactly as in the spec's Registry block, and `class Registry : public QObject` with the spec's methods plus:
  - `void bindRun(const char* id, std::function<void()> fn);` (replaces `run` for a registered id)
  - `void reset();` (test-only: clears all commands, hold keys, overrides; preset back to `editing`)
  - `const QList<Command>& commands() const;` `const QList<HoldKey>& holdKeys() const;`
  - `QList<QKeySequence> keys(const char* id) const;` (all current keys; `key()` returns the first)
  - `QString commandForKey(Scope, const QKeySequence&) const;` (id or empty)
  - `void registerHoldKey(HoldKey h);`
  - `signals: void changed(); void commandRan(const QString& id, Category);`
- Produces: `int shortcutsSelfTest();` (returns failure count).

- [ ] **Step 1: Write the failing tests** in `ShortcutsSelfTest_qt.cpp`, each starting with `reg.reset()` and registering throwaway commands:
  - `ids-unique`: registering a second command with an existing id is rejected (returns and logs; `commands().size()` unchanged).
  - `action-key`: `registerCommand({"t.a","A",Category::Playback,Scope::Player,{QKeySequence("Space")},{QKeySequence("Space")},fn})` → `action("t.a")->shortcut() == QKeySequence("Space")`, `objectName() == "shortcut.t.a"`, `text() == "A"`.
  - `two-keys`: a command with `{",", "<"}` → `action()->shortcuts().size() == 2`.
  - `preset-switch`: editing `I`, classic `Shift+I` → after `setPreset("classic")`, `key("t.a") == QKeySequence("Shift+I")` and `changed()` fired once.
  - `conflict-scope`: two Player commands on `I` → `conflicts("t.b", I) == {"t.a"}`; a Global command on `F` and a Player command asking for `F` → conflict; Player `I` vs nothing → empty.
  - `run-signal`: triggering the action calls `run` and emits `commandRan("t.a", Category::Playback)`.
  - `label`: `label("t.a")` equals `key("t.a").toString(QKeySequence::NativeText)`; empty string when unbound.
- [ ] **Step 2:** Build, run `build_qt/.../jefecheck --shortcuts-test`. Expected: link error / FAIL (no `Registry`).
- [ ] **Step 3: Implement `Registry`.** Store commands in a `QList<Command>` plus `QHash<QString,int>` index; create the `QAction` (parent = registry) in `registerCommand`, shortcut context `ApplicationShortcut` for Global, `WidgetShortcut` for Player. `attach(window, playerWidgets)`: `window->addAction()` for Global, `w->addAction()` on each player widget for Player (one `QAction` can be added to many widgets). `effectiveKeys(id)` = override if present else the preset's list; `applyKeys()` re-sets every action's shortcuts and emits `changed()` once.
- [ ] **Step 4:** Run `--shortcuts-test`. Expected: every line PASS, `SHORTCUTS-TEST PASS`, exit 0.
- [ ] **Step 5: Commit** `Shortcuts: registry core and --shortcuts-test`.

### Task 2: Overrides, persistence, import / export

**Files:**
- Modify: `src/qt/Shortcuts_qt.{h,cpp}`, `src/qt/ShortcutsSelfTest_qt.cpp`

**Interfaces:**
- Consumes: Task 1.
- Produces:
  - `void load(QSettings&);` `void save(QSettings&) const;` (keys from Global Constraints)
  - `void setHoldOverride(const char* id, Qt::Key);` `void clearHoldOverride(const char* id);`
  - `struct Snapshot; Snapshot snapshot() const; void restore(const Snapshot&);` (for Preferences Cancel)
  - `exportTo` / `importFrom` as in the spec.

- [ ] **Step 1: Write the failing tests:**
  - `override-drop-equal`: `setOverride(id, presetKey)` → `isCustom() == false`.
  - `override-unbind`: `setOverride(id, QKeySequence())` → `keys(id).isEmpty()`, saved as empty string, reloads as unbound (not as the preset key).
  - `override-unknown-kept`: QSettings holding `Shortcuts/overrides/gone.id = "Q"` → after `load` then `save`, the key is still there.
  - `persist-roundtrip`: override, `save` to a temp `QSettings(path, IniFormat)`, `reset()` + re-register, `load` → same keys.
  - `snapshot-restore`: snapshot, change preset + override, restore → keys and preset identical to before.
  - `export-import`: export to temp file; JSON has `format`, `version == 1`, `basedOn`, and **every** command id under `keys`; change things; import → all keys equal the exported ones.
  - `import-portable`: a file with `"t.a": "Ctrl+L"` and `"t.b": "Meta+Ctrl+F"` → `key("t.a") == QKeySequence(Qt::CTRL | Qt::Key_L)`, `key("t.b") == QKeySequence(Qt::META | Qt::CTRL | Qt::Key_F)`.
  - `import-unknown-id`: unknown id → import succeeds, `*error` is one line naming the id count, known keys applied.
  - `import-bad` (Review Focus 5): for each of invalid JSON, `format:"other"`, `version: 2`, empty file, `"t.a": "Ctrl+Nonsense+"` → `importFrom` returns false, `*error` non-empty and single-line, keys unchanged.
  - `import-conflict`: a file putting two Player commands on one key → returns true, both keys applied, `conflicts()` reports it.
- [ ] **Step 2:** Run `--shortcuts-test`; the new checks FAIL.
- [ ] **Step 3: Implement.** `QJsonDocument` for the file; reject on parse error, `format != "jefecheck-shortcuts"`, `version > 1`; validate every key text with `QKeySequence::fromString(s, PortableText)` and reject the whole file if any non-empty text yields an empty sequence. Unknown override ids live in a `QHash<QString,QString> orphanOverrides_` written back by `save`.
- [ ] **Step 4:** Run `--shortcuts-test`. Expected: all PASS.
- [ ] **Step 5: Commit** `Shortcuts: overrides, QSettings persistence, .jcshortcuts import/export`.

### Task 3: Register every current binding

**Files:**
- Create: `src/qt/ShortcutCommands_qt.cpp` (declared in `Shortcuts_qt.h`)
- Modify: `src/qt/ShortcutsSelfTest_qt.cpp`

**Interfaces:**
- Consumes: Task 1.
- Produces: `void registerAllCommands(Registry&);` registering, with **today's qt-experimental keys in both `editing` and `classic`**, every binding listed in the spec's recon table:
  - Player: `playback.play-pause` (Space), `playback.step-back` (Left, X), `playback.step-forward` (Right, C), `playback.go-start` (Home), `playback.go-end` (End), `playback.direction-reverse` (`,`, `<`), `playback.direction-forward` (`.`, `>`), `playback.loop-once`/`loop`/`bounce` (8/9/0), `inout.set-in` (I), `inout.set-out` (O), `inout.reset-in` (Shift+I), `inout.reset-out` (Shift+O), `inout.set-in-load` (Alt+I), `plate.track-prev` (Up), `plate.track-next` (Down).
  - Global: `layout.single`/`side`/`top-bottom`/`quad` (Ctrl+1–4), `plate.fit` (F), `plate.fit-all` (Shift+F), `plate.flip` (V), `plate.flip-all` (Shift+V), `plate.flop` (M), `plate.flop-all` (Shift+M), `plate.text-mode` (T), `plate.text-mode-all` (Alt+T), `plate.reset` (Ctrl+R), `plate.reset-all` (Ctrl+Alt+R), `color.reset` (Shift+R), `color.reset-all` (Shift+Alt+R), and one id per remaining menu action that has a key today (File, View fullscreen/histogram/hide-controls, Window panels F2–F9 and N, Help F1 and H, Ctrl+L, Ctrl+P and Ctrl+,). Window-level ones are registered with `run = nullptr` and get it from `bindRun` in Task 4.
  - Hold keys: `color.drag-gamma` W, `color.drag-exposure` E, `color.drag-brightness` Q, `color.drag-contrast` D, `color.drag-saturation` S.
  - Bridge-only `run`s call the same `jefe::qt::*` function the old handler called (see `GlViewport_qt.cpp:394-580` and `MainWindow_qt.cpp:227-296, 700-790`); `inout.reset-in` is `setInPoint(getFromFrame())`.

- [ ] **Step 1: Write the failing test `parity-2.0`:** a table of `{scope, key text, expected id}` covering every key in `GlViewport_Qt::keyPressEvent` and every `QShortcut`/menu key in `MainWindow_qt.cpp` today (build the table by reading those two files; ~65 rows) → `commandForKey(scope, QKeySequence(text)) == id` for each. Plus `presets-conflict-free`: for each preset, no `conflicts()` across all commands.
- [ ] **Step 2:** Run; FAIL (nothing registered).
- [ ] **Step 3: Implement `registerAllCommands`.** Data-first: one `registerCommand({...})` per command, grouped by category with a comment header per group.
- [ ] **Step 4:** Run `--shortcuts-test`; PASS.
- [ ] **Step 5: Commit** `Shortcuts: register every existing binding (2.0 parity table)`.

### Task 4: Move the app onto the registry

**Files:**
- Modify: `src/qt/MainWindow_qt.cpp` (ctor 227–296, menu building 480–790, 1020–1030; `runHeadlessPlateManagerTest` WINDOW-menu check 3642–3686), `src/qt/MainWindow_qt.h`
- Modify: `src/qt/GlViewport_qt.{h,cpp}` (`keyPressEvent`, `keyReleaseEvent`, `leaveEvent`, `mouseMoveEvent` 300–315, add `focusOutEvent` and `event()` for `ShortcutOverride`)
- Modify: `src/qt/TimelinePanel_qt.{h,cpp}` (accessors for the scrubber and tracks widgets)
- Modify: `src/qt/LUTPreview_qt.cpp` (`LutCloudWidget::event` claims F on `ShortcutOverride`)

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces: `TimelineScrubber_Qt* TimelinePanel_Qt::scrubber() const;` `TimelineTracks_Qt* TimelinePanel_Qt::tracks() const;` `bool GlViewport_Qt::isHolding(const char* holdId) const;`

- [ ] **Step 1: Write the failing checks** in `runHeadlessPlateManagerTest`'s WINDOW-menu block: after the menu walk, also walk `Registry::instance().commands()` and fail if any Global command's key equals another Global or any Player key, if any command has no `run`, or if `findChildren<QShortcut*>()` on the main window is non-empty ("all keys live in the registry"). Add a `HOLD-KEYS` check: send `QKeyEvent(KeyPress, W, autoRepeat=false)`, then a release+press pair with `autoRepeat=true` → `isHolding("color.drag-gamma")` stays true; send `QFocusEvent(FocusOut)` → false (Review Focus 2).
- [ ] **Step 2:** Run `--plate-manager-test`; the new checks FAIL (QShortcuts still exist).
- [ ] **Step 3: Implement.**
  - `MainWindow_Qt` ctor: `registerAllCommands(reg)`, `bindRun` every window-level id with the lambda the menu used, connect `reg.commandRan` to one slot that does what `addPlateMenuAction`/`handled()` did (viewport `update()`, `refreshAllCards()`, `plateStateChanged`), then `reg.attach(this, {viewport_, timeline->scrubber(), timeline->tracks()})` after docks exist.
  - Menus add `reg.action(id)` in place of building their own action; keep each menu's `objectName` (`menu.view.fit` etc.) by setting it on the registry action only if no Appium locator would break — otherwise keep both names by leaving the menu's objectName on the action (tests under `tests/ui/` use `menu.*` leaves; grep `locators.py` and keep those leaves).
  - Delete `bindLayout`, `bindPrefsShortcut`, `bindPlateAction` and their QShortcuts.
  - `GlViewport_Qt::keyPressEvent` keeps only: Escape-puts-pencil-down, chat typing, Return-starts-chat, hold-key tracking (keys from `reg.holdKey(id)` for every `holdKeys()` entry). `mouseMoveEvent` asks `isHolding(id)`.
  - `GlViewport_Qt::event`: on `QEvent::ShortcutOverride`, `accept()` every key while `remoteChatModeActive()` (chat fix, spec Scopes).
  - `focusOutEvent` clears the hold set.
  - `LutCloudWidget::event`: accept `ShortcutOverride` for bare F.
- [ ] **Step 4:** Run `--plate-manager-test` (expect `PLATEMANAGER-TEST PASS`) and `--shortcuts-test`; then run the Appium transport/plate suites: `cd tests/ui && pytest test_transport.py test_plate_ops.py test_layouts.py test_track_timeline.py -q`. Expected: all pass unchanged.
- [ ] **Step 5: Commit** `Shortcuts: every binding moves to the registry; chat and LUT cube claim their keys`.

### Task 5: Help overlay and tooltips from the registry

**Files:**
- Create: `src/qt/ShortcutHelpText_qt.cpp` (declared in `Shortcuts_qt.h`)
- Modify: `src/qt/SequenceLoadBridge_qt.{h,cpp}` (add `setHelpMessage`), `src/gfcplatemanager.cpp:39-80` (comment: string is the fallback), `src/qt/MainWindow_qt.cpp`, tooltips across `src/qt/` that name a key
- Modify: `src/qt/ShortcutsSelfTest_qt.cpp`

**Interfaces:**
- Produces: `QString buildHelpText(const Registry&);` and in `jefe::qt`: `void setHelpMessage(const std::string&);` (writes `plateManager.helpMessage`). `QString tooltipWithKey(const QString& text, const char* id);` → `"Play / Pause (Space)"`, or `text` alone when unbound.

- [ ] **Step 1: Write the failing tests:**
  - `help-labels`: `buildHelpText` contains `label(id)` for every command that has a key, grouped under the same section headings as today's string (`VIEWPORT LAYOUTS`, `WINDOW / GUI`, `WINDOW MENU`, `PLAYBACK`, `TRANSFORMS`, `COLOR CORRECTION`, `REMOTE SESSIONS`); mouse lines (Alt+LMB, Pan, Zoom, Shift+LMB pointer) are kept verbatim.
  - `help-override`: `setOverride("inout.set-in", "K")` → text contains `"Set In at playhead - K"` and no line pairs Set In with `I`.
  - `tooltip-grep`: scan `src/qt/*.cpp` (path passed via `JEFE_SOURCE_DIR` compile definition) for `setToolTip(` lines containing a parenthesised key name (`\((Space|Shift\+|Ctrl\+|Alt\+|Cmd\+|F\d+|Home|End|[A-Z0-9,.<>])\)`); expect zero matches.
- [ ] **Step 2:** Run; FAIL.
- [ ] **Step 3: Implement.** MainWindow connects `reg.changed` → `jefe::qt::setHelpMessage(buildHelpText(reg).toStdString())` and calls it once after `attach`. Replace each key-naming tooltip with `tooltipWithKey(...)`, rebuilt in the owning panel's existing refresh on `changed()` (spec "Left to the builder").
- [ ] **Step 4:** Run `--shortcuts-test`; PASS. Launch the app, press H, check the overlay looks as before.
- [ ] **Step 5: Commit** `Shortcuts: help overlay and tooltips read keys from the registry`.

### Task 6: Focus test and phase-1 PR

**Files:**
- Create: `src/qt/ShortcutsFocusTest_qt.cpp` (`int MainWindow_Qt::runHeadlessShortcutsFocusTest();` declared in `MainWindow_qt.h`)
- Modify: `src/main_qt.cpp` (`--shortcuts-focus-test`, `QTimer::singleShot(3000, …)` like `--ui-density-test`)
- Modify: `developer_notes.md` (new § "Keyboard shortcuts registry": scopes, adding a command, hold keys, chat override)

- [ ] **Step 1: Write the test**, each check via `QTest::keyClick` on the focused widget:
  - Playlist list focused with 2 items, Down → selection row 1 and active plate's track unchanged.
  - A `QLineEdit` in the Render dialog (output path) focused, `I` → the text ends with `i` and the In point is unchanged.
  - Chat mode on (`remoteChatBegin` with a fake connection hook, or skip with `SKIP` if no connection can be faked), `F` → chat buffer ends with `f`, plate fit unchanged.
  - Viewport focused, Space → `isPlaying()` toggles.
  - Timeline dock floated (`setFloating(true)`), scrubber focused, Right → current frame +1 (Review Focus 1).
  - Floating FX dock, its search `QLineEdit` focused, `F` → line edit text `f`, plate fit unchanged; same for H, M, T, V (Review Focus 4).
- [ ] **Step 2:** Run `--shortcuts-focus-test`; fix until `SHORTCUTS-FOCUS-TEST PASS`.
- [ ] **Step 3:** Run the full gate: `--shortcuts-test`, `--plate-manager-test`, `--ui-density-test`, Appium `pytest tests/ui -q`.
- [ ] **Step 4: Commit**, push `feature/shortcuts-registry`, open the PR to `qt-experimental` with the dropped accidental-Shift variants listed in the description, and make the before/after infographic artifact (owner's PR convention).

## Phase 2 — Presets and the Preferences list (`feature/shortcuts-presets`)

### Task 7: Bridge calls for the brought-back 1.7 commands

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.{h,cpp}`
- Test: `runHeadlessPlateManagerTest` in `src/qt/MainWindow_qt.cpp`, new `BROUGHT-BACK` report (these need the rendering chain, so not `--shortcuts-test`)

**Interfaces:**
- Produces, in `jefe::qt`:
  - `void toggleChannelOnActive(int channel);` (0 R, 1 G, 2 B, 3 A → `plateManager.toggleChannelR/G/B/A(activeQuad)`)
  - `bool getChannelOnActive(int channel);`
  - `void showTrackOnActive(int track);` (0–3 → `setTrackOnPlate(active, track)`)
  - `void scrollLUTOnActive(int direction);` (`plateManager.scrollLUT(active, ±1)`)
  - `void loadFXFavoriteToActive(int slot);` (`setFavoriteOnPlate(slot)`), `void appendFXFavoriteToActive(int slot);` (`appendFavoriteOnPlate(slot)`), `void saveFXFavoriteFromActive(int slot);` (`saveFavoriteFromPlate(slot)`)
  - CC favourites already exist: `applyCCFavoriteToActive`, `saveCCFavoriteFromActive`.

- [ ] **Step 1: Write the failing checks:** toggle R twice returns to the start state and once flips `getChannelOnActive(0)`; `showTrackOnActive(2)` → the active plate's track is 2; `scrollLUTOnActive(+1)` then `(-1)` returns to the same LUT name; save FX favourite 0 from a plate with one FX, load it on an empty plate → one FX; append → two.
- [ ] **Step 2:** Run `--plate-manager-test`; FAIL.
- [ ] **Step 3: Implement** each as a thin call, then `propagatePlateChanges()` (developer_notes §3).
- [ ] **Step 4:** Run; PASS.
- [ ] **Step 5: Commit** `Bridge: channel, track, LUT-scroll and FX-favourite calls for the 1.7 keys`.

### Task 8: The two presets and the new commands

**Files:**
- Modify: `src/qt/ShortcutCommands_qt.cpp`, `src/qt/Shortcuts_qt.{h,cpp}` (`HoldKey::with` dispatch, reserved keys), `src/qt/GlViewport_qt.cpp` (hold-with handling), `src/qt/ShortcutsSelfTest_qt.cpp`

**Interfaces:**
- Consumes: Task 7 bridges; `jefe::qt::togglePlayFwd`, `setPlayDirection`, `pausePlayback`, `isPlaying`, `seekToFrame`, `getInPoint`, `getOutPoint`, `getFromFrame`, `getToFrame`, `remoteChatBegin`.
- Produces: new ids `playback.jkl-reverse`, `playback.jkl-pause`, `playback.jkl-forward`, `inout.go-in`, `inout.go-out`, `inout.clear-both`, `color.channel-r/g/b/a`, `plate.show-track-1..4`, `color.cc-fav-apply-1..5`, `color.cc-fav-save-1..5`, `fx.fav-load-1..5`, `fx.fav-append-1..5`, `fx.fav-save-1..5`, `plate.lut-prev`, `plate.lut-next`, `remote.chat-type`, `remote.chat-toggle`, hold key `plate.lut-hold` (classic L, `with` Up → `scrollLUTOnActive(-1)`, Down → `+1`); reserved timeline keys `timeline.zoom-in` (`=`), `timeline.zoom-out` (`-`), `timeline.zoom-fit` (`\`), `timeline.zoom-inout` (Shift+`\`) registered with `run = nullptr` and no menu so the timeline project binds them. `void Registry::setDefaultPreset(const QString&)` used when `Shortcuts/preset` is unset (`editing`).

- [ ] **Step 1: Write the failing tests:**
  - `preset-editing`: one row per line of the spec's preset table, Editing column → `commandForKey`.
  - `parity-1.7`: build the table from `main` (`git show main:src/UICallbacks.cpp` `globalCB`, `git show main:src/GlViewport.cpp` `FL_KEYDOWN` ~line 930–1100); every key there → its command under `classic`. Keys 1.7 opened windows with (F2–F6) map to the 2.0 panel commands, per the spec note.
  - `jkl`: with fakes injected via `bindRun` capturing calls, `L` while playing backward → `setPlayDirection(+1)` and playing; `K` → paused; `J` → direction −1 and playing.
  - `classic-no-jkl`: under `classic`, `J`, `K` have no command and `L` is only the hold key.
  - `hold-with`: under `classic`, holding L then Up runs the `with` action and does **not** run `plate.track-prev`; Up without L runs `plate.track-prev`.
  - `reserved`: `conflicts(any, "=")` reports `timeline.zoom-in`.
  - `presets-conflict-free` (from Task 3) still passes for both presets.
- [ ] **Step 2:** Run; FAIL.
- [ ] **Step 3: Implement.** Fill `editing`/`classic` from the spec table, the 1.7 rules there decide every row. Viewport `keyPressEvent`: when a hold key with `with` entries is held and the pressed key matches one, run it and `accept()`; claim those keys via `ShortcutOverride` while the hold is down so the Player action doesn't also fire. The `parity-2.0` table becomes the Editing table's ancestor: update the rows the spec changed (Shift+I/O, Alt+I/O, Alt+I load) instead of deleting the check.
- [ ] **Step 4:** Run `--shortcuts-test`; PASS.
- [ ] **Step 5: Commit** `Shortcuts: Editing and shipped-1.7 presets, J/K/L, In/Out, 1.7 commands back`.

### Task 9: Preferences persistence wiring

**Files:**
- Modify: `src/qt/qt_prefs_persist.cpp` (`loadPreferences` → `Registry::instance().load(s)`; `writePreferences` → `save(s)`), `src/qt/PreferencesWindow_qt.{h,cpp}` (ctor snapshot next to `sett_backup_` at line 52; Cancel at line 88 restores it)

- [ ] **Step 1: Write the failing check** in `--shortcuts-test` `prefs-cancel`: snapshot as the dialog does, override a key, run the dialog's cancel path through a test hook `PreferencesWindow_Qt::cancelForTest()`, → key back. And `prefs-done`: override, done path, new `QSettings` read shows the override.
- [ ] **Step 2:** Run; FAIL.
- [ ] **Step 3: Implement.** Registry `load` runs after `registerAllCommands` (MainWindow ctor), not inside `loadPreferences()` if that runs earlier — check the order in `main_qt.cpp` and call `load` from MainWindow after registration if so.
- [ ] **Step 4:** Run; PASS.
- [ ] **Step 5: Commit** `Shortcuts: persist with Preferences Done, revert on Cancel`.

### Task 10: Preferences → Shortcuts page (list)

**Files:**
- Create: `src/qt/ShortcutsPage_qt.{h,cpp}` (`class ShortcutsPage_Qt : public QWidget`)
- Modify: `src/qt/PreferencesWindow_qt.cpp` (`addPage(tr("Shortcuts"), new ShortcutsPage_Qt(this))` after `buildRemotePage()`), `src/qt/UiDensityCheck_qt.cpp` if it enumerates pages
- Test: `tests/ui/test_shortcuts.py`, `tests/ui/jefecheck/locators.py`

**Interfaces:**
- Consumes: Registry (Tasks 1, 2, 8).
- Produces: `ShortcutsPage_Qt::selectCommand(const QString& id)` (used by the keyboard map in Task 14) and signal `commandSelected(QString id)`.

Layout and behaviour exactly as the spec's Preferences → Shortcuts section, minus the keyboard map (Task 13): preset combo + "(Custom)" label, Import…/Export…/Reset to preset; search box; `QTreeView` grouped by category with columns Command / Key; "Hold and drag" group; read-only "Inside panels" group (Playlist Enter, Delete/Backspace, Shift+Up/Down; LUT cube F; FX scrub field keys); in-place `QKeySequenceEdit` on double-click/Enter, Escape cancels, Backspace on empty capture unbinds; orange changed label + reset dot (reuse the FX panel's reset-dot widget/colour); inline conflict bar with Replace / Cancel; inline preset-switch bar "Switching discards N custom keys. [Switch] [Keep]". File dialogs start at `getDefaultBrowsePath()`.

- [ ] **Step 1: Write the failing Appium test `test_shortcuts.py`:** open Preferences, select Shortcuts; type "play forward" in `prefs.shortcuts.search` → one row; capture `P` on it → `prefs.shortcuts.conflict.replace` appears if `P` is taken (bind to an untaken key otherwise, then to a taken one to see the bar); Replace; Done; click viewport, press the key → transport shows playing. Second test: Export to a temp path, change a key, Import → key restored.
- [ ] **Step 2:** Run `pytest tests/ui/test_shortcuts.py -q`; FAIL.
- [ ] **Step 3: Implement the page.**
- [ ] **Step 4:** Run the Appium test, `--ui-density-test`, `--shortcuts-test`; PASS.
- [ ] **Step 5: Commit** `Preferences: Shortcuts page with search, capture, conflicts, import/export`.

### Task 11: First-launch banner

**Files:**
- Create: `src/qt/ShortcutsBanner_qt.{h,cpp}` (inline bar over the top of the viewport area, not a dialog)
- Modify: `src/qt/MainWindow_qt.cpp` (show on first `showEvent` when `Shortcuts/preset` is unset and no headless/test flag is set)

- [ ] **Step 1: Write the failing check** in `runHeadlessPlateManagerTest` `BANNER`: with a cleared `Shortcuts/preset`, `ShortcutsBanner_Qt::shouldShow(settings, isTestRun=false) == true`; `true` arg → false; after clicking the "Use 1.7 keys" button (`shortcuts.banner.classic`) the preset is `classic` and stored; "Keep" stores `editing`; "Customize…" stores `editing` and opens Preferences on the Shortcuts page.
- [ ] **Step 2:** Run; FAIL.
- [ ] **Step 3: Implement.** Copy exactly: "Keyboard shortcuts follow Premiere, Resolve and Final Cut. Used JefeCheck 1.7?" with buttons "Use 1.7 keys", "Keep", "Customize…".
- [ ] **Step 4:** Run; PASS.
- [ ] **Step 5: Commit** `Shortcuts: first-launch preset banner`.

### Task 12: Docs and phase-2 PR

**Files:** `docs/manual.md`, `docs/quick-start.md`, `developer_notes.md`, `CLAUDE.md` (Preferences now has six sections; Shortcuts registry pointer)

- [ ] **Step 1:** Manual and quick start list the Editing keys; add a "Coming from JefeCheck 1.7" table of the differences (generated from the two presets: run `--shortcuts-test --dump-presets` which prints a Markdown table, added in this step) and a paragraph on the Shortcuts page and the non-US-layout limit.
- [ ] **Step 2:** Full gate as in Task 6 Step 3, plus `pytest tests/ui/test_shortcuts.py`.
- [ ] **Step 3: Commit**, push, PR to `qt-experimental`, infographic artifact.

## Phase 3 — Keyboard map (`feature/shortcuts-keymap`)

### Task 13: `ShortcutKeyboard_Qt` (paint, layers, inspect)

**Files:**
- Create: `src/qt/ShortcutKeyboard_qt.{h,cpp}`
- Modify: `src/qt/ShortcutsPage_qt.cpp` (map above the list), `src/qt/ShortcutsSelfTest_qt.cpp`

**Interfaces:**
- Produces: `class ShortcutKeyboard_Qt : public QWidget` with `void setLayer(Qt::KeyboardModifiers);` `Qt::KeyboardModifiers layer() const;` `QRectF keyRect(Qt::Key) const;` `QString keyCaption(Qt::Key) const;` (short command name or empty), signal `keyClicked(Qt::Key)`; and a pure helper `QList<KeyCap> ansiLayout();` (`struct KeyCap { Qt::Key key; QString label; float x, y, w; }`, units of one key width: F-row, number row with `-`/`=`/Backspace, QWERTY rows, Space, arrows, Home/End).

- [ ] **Step 1: Write the failing tests:** `keymap-layout`: `ansiLayout()` contains every key any command in either preset uses (except mouse), no two caps overlap. `keymap-layer`: with Editing, `setLayer(NoModifier)` → `keyCaption(Key_I)` names Set In; `setLayer(Shift)` → Go to In; `setLayer(Alt)` → Clear In. `keymap-hold`: W shows the hold mark under no modifier.
- [ ] **Step 2:** Run; FAIL.
- [ ] **Step 3: Implement.** Category tints from theme tokens (must read on `jefecheck_dark.qss` background); modifier toggle buttons (none / ⇧ / ⌃ / ⌥ / ⌘ on macOS, None/Shift/Ctrl/Alt elsewhere) plus live physical-modifier tracking while the page has focus; clicking a key fills a side panel with that key on every layer; repaint on `Registry::changed`.
- [ ] **Step 4:** Run `--shortcuts-test`, `--ui-density-test`; PASS. Screenshot the page via `--ui-density-test <outDir>` and check it by eye in the dark theme.
- [ ] **Step 5: Commit** `Shortcuts page: keyboard map with modifier layers`.

### Task 14: Drag-to-assign and phase-3 PR

**Files:** `src/qt/ShortcutKeyboard_qt.cpp`, `src/qt/ShortcutsPage_qt.cpp`, `tests/ui/test_shortcuts.py`

- [ ] **Step 1: Write the failing tests:** `--shortcuts-test` `keymap-drop`: calling the drop handler with mime `application/x-jefecheck-command` = `playback.jkl-forward` on `Key_P` with layer Shift → key becomes Shift+P; dropping onto a taken key raises the same conflict bar as capture (page signal `conflictRaised(id, key)`). Appium: click `prefs.shortcuts.keymap`'s I key → side panel lists the I-key commands.
- [ ] **Step 2:** Run; FAIL.
- [ ] **Step 3: Implement** drag from list rows (`QAbstractItemView::DragOnly`) and drop on the map.
- [ ] **Step 4:** Full gate (Task 6 Step 3 + `test_shortcuts.py`); PASS.
- [ ] **Step 5: Commit**, push, PR to `qt-experimental`, infographic artifact.
