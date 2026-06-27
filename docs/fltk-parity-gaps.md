# FLTK → Qt Parity Gaps (audit 2026-06-21)

Audit of features the FLTK build (git `9a1c605`) + user manual describe that the current Qt build (`qt-experimental`) does **not** yet implement. Sources: `git show 9a1c605:src/{mainWindow.fl,UICallbacks.cpp,GlViewport.cpp,renderWindow.h}`, current `src/qt/*`, `docs/manual.md`.

**Legend:** 🟢 backend exists in shared C++ (cheap — just wire Qt UI/shortcuts) · 🟡 partial in Qt · 🔴 not implemented (real work) · ✅ done · ⚪ intentionally not ported.

## Status (updated 2026-06-21)

The 2026-06-21 parity run closed every high/medium-value gap (PRs #100–108):
session restore + CC favorites, image saving + render output, render quality
knobs, histogram overlay, playlist `.jpl`, the shortcut/menu/transport set
(Dialogs/Help menus, Hide Controls, transport + In/Out + direction/step keys),
and the Help menu. What remains is, by design, **not** scheduled:

- **Async render + progress dialog** — the only remaining item with real value;
  the sync render works, so this is a scoped future PR (QThread driver), not a
  gap. Video creation is dead (FLTK used Linux-only mencoder).
- **Save Chat Log** — low value; tied to the rarely-used remote-chat feature.
- **Zoom Filtering / Aspect-bar opacity** — functionally present via Preferences;
  only the FLTK View-menu *access path* differs.
- **R/G/B/A channel keys** — deliberately removed (printable keys at app scope
  collide with text input); per-plate RGBA masks live in the plate card.
- **Playlist drag-append / remote integration**, **`Ctrl+P`** (taken by
  Preferences in Qt) — minor.

## A. Session & playlist (state management)
- ✅ **Save Session / Open Session / Recent / crash recovery** — DONE (PR #100). Configurable launch behavior (empty/reopen/ask); auto-loads footage; CC favorites folded in (✅ C).
- 🟡 **Playlist Manager** (`Ctrl+P`) — Qt playlist dock (`dock.playlist`) with add/remove/reorder/clear and ✅ **Save/Load `.jpl`** (playlist PR — Load…/Save… buttons → `gfcPlaylistManager::loadPlaylist`/`savePlaylist`, round-trip verified via `--playlist-test`). Still TODO: drag-append, remote-session integration, `Ctrl+P` dock shortcut. Manual §Playlists.
- 🟢 **Window layout persistence** — ✅ already works (`saveLayout`/`restoreLayout` via QSettings).

## B. File export / saving / render
- ✅ **Image saving** (`gfcImageSaver`) — DONE (image-saving PR). OIIO-backed `gfcImageSaverOIIO` writes JPEG/PNG/TIFF/TGA/BMP (8-bit RGBA, RGB for JPEG/BMP) and EXR (half/float). Verified end-to-end via `--render-test`.
- ✅ **Render Manager** (`src/qt/RenderDialog_qt`) — writes real files; format-specific quality knobs (JPEG/PNG/TIFF/EXR via a per-format `QStackedWidget`); progress bar + cancel (event-loop incremental render); and ✅ **video export** (video PR): H.264 (MP4) / H.265 (MP4) / ProRes (MOV) via the cross-platform FFmpeg CLI (`VideoEncoder_qt`, `QProcess`). Renders a temp PNG sequence then encodes (pad-to-fit handles varying frame sizes); fps + quality controls; two-phase progress (render 0–50%, encode 50–100%) with cancel. Replaces FLTK's Linux-only mencoder path. A GPL static ffmpeg is bundled per-platform in release packaging (resolved bundled→Preferences→PATH). Only optional "open when done" remains.
- ⚪ **Save Chat Log** (File menu) — deferred (low value; tied to the rarely-used remote-chat feature). Remote dialog exists; chat-log save not wired.

## C. Color-correction favorites (5 slots)
- 🟢 **Save/Load CC favorites** (`Cmd+1..5` load / `Cmd+Shift+1..5` save) — `gfcPlateManager::saveFavoriteColorCorrectionFromPlate` / `setFavoriteColorCorrectionOnPlate` exist + persist in session XML; no Qt menu or shortcuts. Manual §Control Bar ("save and load up to 5 favorite color corrections").

## D. Visualization / display
- ✅ **LUT visualization** (1D graph / 3D cube) — DONE in PR #99 (matches manual §LUT Manager Visualization).
- ✅ **Show Histogram** (`Ctrl+H` active quad / `Ctrl+Alt+H` all plates) — toggle via bridge + View menu (histogram PR). Drag/resize works via the GL color-pick subsystem (histogram-drag PR): the Qt build had never registered `pickManager` drawees/notifees or driven `doPicking` on mouse events, so the overlay (and AOI corners) couldn't be grabbed. Now wired in `GlViewport_Qt`'s press/move/release.
- ✅ **Hide Controls** (`Ctrl+Alt+F`) — hides menu bar / status bar / all docks; the shortcut still fires while hidden so it toggles back. DONE (shortcut-parity PR).
- ✅ **Toggle On-Screen Help** (FLTK bare `h`) — DONE (help-menu PR; Help → Toggle On-Screen Help. Menu item since bare H is flop in Qt).
- 🟡 **Zoom Filtering** (point vs bilinear) — FLTK View-menu toggle; in Qt this may live in Preferences (verify) — at least no View-menu item.
- 🟡 **Aspect bar opacity** — FLTK View-menu (4 levels); Qt moved it to Preferences → View (developer_notes). Functional, different access.

## E. Menu access & shortcuts (dialogs mostly exist as Qt docks)
- ✅ **F-key dialog shortcuts**: F2 FX Stack, F3 FX Params, F4 LUT, F5 Remote, F6 Render — DONE (shortcut-parity PR). New "Dialogs" menu (`menu.dialogs`) raises the matching dock/dialog; Hide Controls (`Ctrl+Alt+F`) lives here too.
- ✅ **Help menu**: F1 User Manual + Quick Start Guide (shortcut-parity PR); ✅ **Report an Issue** (→ GitHub issues — modern replacement for FLTK's dead jefecorp.com "Online Support"; no Video Tutorials exist for the OSS release) and ✅ **Toggle On-Screen Help** (FLTK's bare `h`; menu item since bare H is flop) added (help-menu PR). (About + System Specs already existed.)

## F. Playback / transport keyboard shortcuts
Qt has Space (pause), Left/Right (step), Up/Down (track cycle). FLTK additionally had:
- ✅ **Playback direction** `.` (fwd) / `,` (rev) and **frame step** `C` (fwd) / `X` (rev) — DONE (transport-keys PR; `c`/`x` reuse `stepFrame`, `.`/`,` via new `setPlayDirection`). **Rewind / fast-fwd** ✅ `Home`/`End` (FLTK's `Z`/`V` are taken by flip/flop in Qt). **Loop mode** ✅ `8`/`9`/`0` (once/loop/bounce) — DONE (shortcut-parity PR).
- ✅ **In/Out point keys** `I`/`O` set in/out at current frame; `Shift+I`/`Shift+O` set to timeline ends (shortcut-parity PR); ✅ `Alt+I` sets the in point and reloads all tracks from there (inout-alt PR, via `setInPointAndLoad` → `trackManager.startLoadingAllAt`).
- ⚪ **LUT cycling** `L` + Up/Down — the audit misattributed this: FLTK's `l` opens the load window (already `Ctrl+L` in Qt), it does not cycle LUTs. Nothing to port.

## G. Channel toggles
- ⚪ **R/G/B/A bare-key channel-visibility toggles** — intentionally not ported (printable keys at app scope conflict with text input). Per-plate RGBA mask toggles exist in the plate-card UI.

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
