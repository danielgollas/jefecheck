# FLTK → Qt Parity Gaps (audit 2026-06-21)

Audit of features the FLTK build (git `9a1c605`) + user manual describe that the current Qt build (`qt-experimental`) does **not** yet implement. Sources: `git show 9a1c605:src/{mainWindow.fl,UICallbacks.cpp,GlViewport.cpp,renderWindow.h}`, current `src/qt/*`, `docs/manual.md`.

**Legend:** 🟢 backend exists in shared C++ (cheap — just wire Qt UI/shortcuts) · 🟡 partial in Qt · 🔴 not implemented (real work) · ✅ done.

## A. Session & playlist (state management)
- ✅ **Save Session / Open Session / Recent / crash recovery** — DONE (PR #100). Configurable launch behavior (empty/reopen/ask); auto-loads footage; CC favorites folded in (✅ C).
- 🟡 **Playlist Manager** (`Ctrl+P`) — a Qt playlist dock exists (`dock.playlist`); FLTK's full save/load `.jpl`, drag-append, remote-session integration not fully verified/ported. Manual §Playlists.
- 🟢 **Window layout persistence** — ✅ already works (`saveLayout`/`restoreLayout` via QSettings).

## B. File export / saving / render
- ✅ **Image saving** (`gfcImageSaver`) — DONE (image-saving PR). OIIO-backed `gfcImageSaverOIIO` writes JPEG/PNG/TIFF/TGA/BMP (8-bit RGBA, RGB for JPEG/BMP) and EXR (half/float). Verified end-to-end via `--render-test`.
- 🟡 **Render Manager** (`src/qt/RenderDialog_qt`) — ✅ now writes real files (saver wired + GL context made current around the render). Format-specific quality knobs are applied from `gfcRenderParams` defaults but **not yet exposed in the dialog UI**; still missing: **video creation** (FLTK used mencoder, Linux-only), progress dialog (frame counter + abort), "open when done". Header comments mark these "PR-39b".
- 🔴 **Save Chat Log** (File menu) — remote dialog exists; chat-log save not wired.

## C. Color-correction favorites (5 slots)
- 🟢 **Save/Load CC favorites** (`Cmd+1..5` load / `Cmd+Shift+1..5` save) — `gfcPlateManager::saveFavoriteColorCorrectionFromPlate` / `setFavoriteColorCorrectionOnPlate` exist + persist in session XML; no Qt menu or shortcuts. Manual §Control Bar ("save and load up to 5 favorite color corrections").

## D. Visualization / display
- ✅ **LUT visualization** (1D graph / 3D cube) — DONE in PR #99 (matches manual §LUT Manager Visualization).
- ✅ **Show Histogram** (`Ctrl+H` active quad / `Ctrl+Alt+H` all plates) — DONE (histogram PR). The RGB histogram overlay (`gfcPlate::drawHistogram` + draggable `gfcHistogramGLWindow`) already existed in the render chain; this just adds the toggle (bridge + View menu). Drag/resize uses the existing pick-selection path.
- ✅ **Hide Controls** (`Ctrl+Alt+F`) — hides menu bar / status bar / all docks; the shortcut still fires while hidden so it toggles back. DONE (shortcut-parity PR).
- 🟡 **Zoom Filtering** (point vs bilinear) — FLTK View-menu toggle; in Qt this may live in Preferences (verify) — at least no View-menu item.
- 🟡 **Aspect bar opacity** — FLTK View-menu (4 levels); Qt moved it to Preferences → View (developer_notes). Functional, different access.

## E. Menu access & shortcuts (dialogs mostly exist as Qt docks)
- ✅ **F-key dialog shortcuts**: F2 FX Stack, F3 FX Params, F4 LUT, F5 Remote, F6 Render — DONE (shortcut-parity PR). New "Dialogs" menu (`menu.dialogs`) raises the matching dock/dialog; Hide Controls (`Ctrl+Alt+F`) lives here too.
- 🟡 **Help menu**: ✅ F1 User Manual + Quick Start Guide (open the GitHub docs) added (shortcut-parity PR). Online Support / Video Tutorials / on-screen-help toggle (`H`) still TODO. (About + System Specs already existed.)

## F. Playback / transport keyboard shortcuts
Qt has Space (pause), Left/Right (step), Up/Down (track cycle). FLTK additionally had:
- 🟡 **Playback direction** `.` (fwd) / `,` (rev); **frame step** `C`/`X`. Still TODO. **Rewind / fast-fwd** ✅ remapped to `Home`/`End` (FLTK's `Z`/`V` keys are taken by flip/flop in Qt). **Loop mode** ✅ `8`/`9`/`0` (once/loop/bounce) — DONE (shortcut-parity PR).
- 🟡 **In/Out point keys** ✅ `I`/`O` set in/out at current frame; `Shift+I`/`Shift+O` set to timeline ends — DONE (shortcut-parity PR). Alt set-and-load-at variant still TODO.
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
