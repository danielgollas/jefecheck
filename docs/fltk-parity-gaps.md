# FLTK → Qt Parity Gaps (audit 2026-06-21)

Audit of features the FLTK build (git `9a1c605`) + user manual describe that the current Qt build (`qt-experimental`) does **not** yet implement. Sources: `git show 9a1c605:src/{mainWindow.fl,UICallbacks.cpp,GlViewport.cpp,renderWindow.h}`, current `src/qt/*`, `docs/manual.md`.

**Legend:** 🟢 backend exists in shared C++ (cheap — just wire Qt UI/shortcuts) · 🟡 partial in Qt · 🔴 not implemented (real work) · ✅ done.

## A. Session & playlist (state management)
- 🟢 **Save Session / Open Session** (`Ctrl+S` / `Ctrl+O`) → `.jcs`. `gfcSessionManager::saveSession/loadSession` fully exist; no Qt menu/shortcut. *(This is the chosen next project.)*
- 🟢 **Recent Sessions** submenu — `sett.recentSessions` tracked; `rebuildRecentSessionsMenu()` stubbed; no Qt menu.
- 🟢 **Crash recovery on startup** — `writeCrashSession`/`checkCrashedSession`/`loadCrashedSession` exist; no Qt startup hook.
- 🟡 **Playlist Manager** (`Ctrl+P`) — a Qt playlist dock exists (`dock.playlist`); FLTK's full save/load `.jpl`, drag-append, remote-session integration not fully verified/ported. Manual §Playlists.
- 🟢 **Window layout persistence** — ✅ already works (`saveLayout`/`restoreLayout` via QSettings).

## B. File export / saving / render
- 🔴 **Image saving** (`gfcImageSaver`) — STUB; `getImageSaverInstance()` returns NULL, nothing writes. FLTK wrote EXR/JPEG/TIFF/PNG/TGA/BMP. **Blocks render output.** Would wire through OIIO (same as the .tga read we just added).
- 🟡 **Render Manager** (`src/qt/RenderDialog_qt`) — UI + sync render-trigger exist, but actual file writes depend on the stubbed saver; missing: per-format quality knobs (JPEG/PNG/TIFF/EXR depth+compression), **video creation** (FLTK used mencoder, Linux-only), progress dialog (frame counter + abort), "open when done". Header comments mark these "PR-39b".
- 🔴 **Save Chat Log** (File menu) — remote dialog exists; chat-log save not wired.

## C. Color-correction favorites (5 slots)
- 🟢 **Save/Load CC favorites** (`Cmd+1..5` load / `Cmd+Shift+1..5` save) — `gfcPlateManager::saveFavoriteColorCorrectionFromPlate` / `setFavoriteColorCorrectionOnPlate` exist + persist in session XML; no Qt menu or shortcuts. Manual §Control Bar ("save and load up to 5 favorite color corrections").

## D. Visualization / display
- ✅ **LUT visualization** (1D graph / 3D cube) — DONE in PR #99 (matches manual §LUT Manager Visualization).
- 🔴 **Show Histogram** (`Ctrl+H`) — RGB histogram overlay, draggable/resizable. Not ported.
- 🔴 **Hide Controls** (`Ctrl+Alt+F`) — hide menu/control bars, shortcuts still active. Not ported.
- 🟡 **Zoom Filtering** (point vs bilinear) — FLTK View-menu toggle; in Qt this may live in Preferences (verify) — at least no View-menu item.
- 🟡 **Aspect bar opacity** — FLTK View-menu (4 levels); Qt moved it to Preferences → View (developer_notes). Functional, different access.

## E. Menu access & shortcuts (dialogs mostly exist as Qt docks)
- 🔴 **F-key dialog shortcuts**: F2 FX Stack, F3 FX Manager, F4 LUT, F5 Remote, F6 Render. The Qt equivalents exist as docks/dialogs but have **no F-key shortcuts** and there's no "Dialogs" menu.
- 🔴 **Help menu**: F1 manual, Quick Start, Online Support, Video Tutorials, **Toggle On-screen Help (`H`)**, (About + System Specs already exist in Qt Help menu).

## F. Playback / transport keyboard shortcuts
Qt has Space (pause), Left/Right (step), Up/Down (track cycle). FLTK additionally had:
- 🔴 **Playback direction** `.` (fwd) / `,` (rev); **frame step** `C`/`X`; **fast-fwd** `V` / **rewind** `Z`; **loop mode** `8`/`9`/`0`. *(Note: `V`/`H` are now flip/flop in Qt.)*
- 🔴 **In/Out point keys** `I`/`O` (+ `Shift`/`Alt` variants: set to 1, set to max, set-and-load-at). Spinboxes exist in the timeline; keys don't.
- 🔴 **LUT cycling** `L` + Up/Down.

## G. Channel toggles
- 🔴 **R/G/B/A bare-key channel-visibility toggles** — removed (printable keys at app scope conflict with text input). Per-plate RGBA mask toggles exist in the plate-card UI, just no global keys.

## H. Remapped (functional parity, different keys — NOT missing)
- Flip/Flop `Ctrl+8`/`Ctrl+9` → `V`/`H` (+ `Shift` for all).
- Fullscreen `Ctrl+F` → `F11` / `Ctrl+Cmd+F`.
- Reset CC `Shift+R`, Reset-all `Shift+Alt+R` (kept; modifiers tidied).
- Load Manager `Ctrl+L` (kept, opens the new Load Sequence Manager).
- Preferences `Cmd+,` / `Cmd+P`.

---

## Suggested clustering for PRs
1. **Session restore** (A: save/open/recent/crash-recovery) — backend exists; chosen next. Likely pulls in **CC favorites** (C) since both serialize to the same `.jcs`.
2. **Image saving + render completion** (B) — wire `gfcImageSaver` through OIIO, then the Render dialog actually writes; add quality knobs + progress. (Video creation = separate, lower priority.)
3. **Histogram** (D) — self-contained overlay feature.
4. **Shortcut/menu parity pass** (E/F/G/H) — a "Dialogs" menu + F-keys + transport keys + on-screen help; mostly wiring existing actions. One focused PR.
