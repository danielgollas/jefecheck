# Qt Playlist Parity — Design

**Date:** 2026-06-28
**Branch:** `qt/playlist-parity`
**Status:** Design approved, pending spec review

## Goal

Bring the Qt6 playlist panel up to (and slightly past) FLTK parity. The
FLTK build's playlist lets a user snapshot a full viewing setup, recall it,
inspect per-track detail, reorder/append by drag-drop, and load at a reduced
scale. The Qt panel currently only adds single-track items from a file dialog
and shows basenames.

## Key finding: the engine already supports everything

The playlist model is shared between both builds and is fully capable. **No
engine model or public-API changes are required** — this is UI plus a handful
of glad-free bridge accessors. The one possible exception is auto-advance
end-of-playback detection: it is done in the bridge TU from existing playback
state, and only if that state proves insufficient would a small **additive,
read-only** helper be added to the playback manager (see Testing/Files).
Relevant existing engine API:

- `gfcTrackManager::getPlaylistItem()` — snapshots the **current live setup**:
  every track's load params (from the GUI), the per-plate FX stacks, and the
  full program state (`getCurrentProgramState()` → layout, playback mode/FPS,
  in/out, per-plate color correction + flip/flop/crop + RGBA, per-track
  offset/hold).
- `gfcTrackManager::setPlaylistItem(item)` — applies an item back, honoring
  `scaleOverride`.
- `gfcTrackManager::setScaleOverride(int pct)` — overrides load scale.
- `gfcPlaylistManager`: `addItemlist`, `getItem`, `removePlaylistItem`,
  `movePlaylistItem` (note: `+1` = up, `-1` = down — inverted vs. the UI's
  "down" arrow; the bridge already translates this), `clearPlaylist`,
  `setSelectedItem` / `selectedItem`, `createPlaylistItemFrom(files)`,
  `appendTracksToItem(files, index)`, `savePlaylist` / `loadPlaylist` (`.jpl`).
- `gfcPlaylistItem`: `loadParams[]` (per-track `gfcLoadParams`), `fxstacks[]`,
  `programState`.

The Qt double-click **load** path already calls `setPlaylistItem` and restores
full state correctly. The parity gap is entirely in **creation, display, and
interaction**.

## Scope

In scope (all five):

1. Snapshot the current setup into an item ("Add Current").
2. Rich per-item display with Compact-view and Show-full-paths toggles.
3. Drag-drop + keyboard + context-menu interaction.
4. Scale override on load.
5. Auto-advance on playback end (net-new; neither build had it) + optional
   Loop-playlist.

Out of scope: persisting the playlist inside `.jcs` session files (standalone
`.jpl` only, as today); network/remote playlist sync UI (engine already
syncs); the FLTK separate-window form (the Qt build keeps the panel as a
**dock**).

## Architecture

### TU separation (developer_notes §1)

`PlaylistPanel_qt.cpp` and the new card widget are **UI TUs** and must stay
glad-free: they include no rendering-chain/manager/glad headers. All engine
access goes through `jefe::qt::*` accessors in `SequenceLoadBridge_qt.cpp`
(the only TU that includes the managers). Per-item detail crosses the boundary
as a plain POD struct, not a `gfcPlaylistItem`.

### Components

- **`PlaylistPanel_Qt`** (`src/qt/PlaylistPanel_qt.{h,cpp}`) — the dock panel:
  toolbar (buttons + toggles + scale override + auto-advance), a `QListWidget`
  hosting one `PlaylistItemCard` per item, status label. Owns all user-action
  slots and routes them through the bridge, then `refreshList()`.
- **`PlaylistItemCard`** (new `QWidget`, in the same files as the panel) —
  one card per item, mirroring the FX-panel card pattern (consistency):
  - **Header row:** `☰` drag-handle affordance · index · item name ·
    track chips `[A][B]…` · `▸/▾` collapse chevron.
  - **Collapsible detail block:** one row per track —
    `letter  filename  from-to (total)  scale%  filter  CC:on/off  [crop]  bitdepth`.
    Honors the global **Show full paths** toggle (full path vs. basename).
  - Emits signals for load / remove / move / append-tracks / toggle-expand so
    the panel does the bridge calls (card stays dumb + glad-free).
- **`SequenceLoadBridge_qt`** — new accessors (below).
- **`MainWindow_Qt`** — idle-tick hook that polls the auto-advance signal and
  drives advancement; dock already exists.

### Data flow

```
User action → PlaylistItemCard signal / panel button
            → PlaylistPanel_Qt slot
            → jefe::qt::* bridge call (manager/trackManager)
            → PlaylistPanel_Qt::refreshList()
            → rebuild cards from getPlaylistItemNames() + getPlaylistItemDetail(i)
```

```
Idle tick (MainWindow) → jefe::qt::consumePlaylistAdvanceSignal()
            → if true and panel auto-advance armed:
                 PlaylistPanel_Qt::advanceToNext()
                 → jefe::qt::loadPlaylistItem(next) + resume playback
```

## Feature detail

### 1. Item creation

- **"Add Current"** (primary) → `jefe::qt::addCurrentAsPlaylistItem()` →
  `playlistManager.addItemlist(trackManager.getPlaylistItem())`. Snapshots the
  live setup (tracks + FX + program state). This is the core playlist
  workflow.
- **"Add Files…"** → multi-select `QFileDialog` →
  `jefe::qt::addPlaylistFiles(paths)` → one **multi-track** item via
  `createPlaylistItemFrom` (A–D from the chosen files). The old single-file
  add is folded into this.

### 2. Display + toggles

- Cards render from `getPlaylistItemNames()` (names) and a new
  `getPlaylistItemDetail(index)` (per-track POD rows).
- **Compact view** toggle collapses/expands all cards' detail blocks; each
  card's chevron toggles its own. **Show full paths** toggle switches
  filename rendering. Both persist in QSettings
  (`UI/playlistCompactView`, `UI/playlistShowFullPaths`), matching FLTK's
  `playlistShowCompactView` / `playlistShowFullPaths`.
- The currently-loaded item (`getSelectedPlaylistItem()`) is highlighted.

### 3. Interaction

- **Drag-reorder:** `QListWidget` `InternalMove`; on row move call
  `movePlaylistItem`. The `☰` handle is the visible affordance.
- **Drag-drop onto the panel:**
  - `.jpl` file → `loadPlaylistFile` (replace playlist).
  - media files on empty area → `addPlaylistFiles` (new item).
  - media files dropped **on a card** → `appendTracksToPlaylistItem(index, paths)`.
- **Keyboard:** Enter / double-click = load; Delete / Backspace = remove;
  ↑/↓ = select (native); Shift+↑/↓ = reorder.
- **Right-click context menu:** Load · Append tracks… · Remove · Move up ·
  Move down · Show full paths · Compact view.
- **Toolbar** (unchanged shape, new buttons):
  `Add Current · Add Files… · − · ↑ · ↓ · Clear   …   Load · Save`.

### 4. Scale override

- A `[ ] Scale override [100 ▾]` row (checkbox + 100/50/25 combo).
- When checked, before each `loadPlaylistItem` the panel calls
  `jefe::qt::setPlaylistScaleOverride(pct)`; when unchecked it calls
  `setPlaylistScaleOverride(0)` (0 = "no override"). Maps to
  `trackManager.setScaleOverride`. State persists in QSettings.

### 5. Auto-advance on playback end

- An `[ ] Auto-advance` checkbox (persisted).
- **Arming condition:** enabled, the loaded content came from the playlist
  (`getSelectedPlaylistItem() >= 0`), and it is playing in **once** (non-loop)
  mode.
- **Trigger:** the rendering-chain TU detects the transition to
  *stopped-at-end* and latches a one-shot flag. The bridge exposes
  `consumePlaylistAdvanceSignal()` which returns `true` exactly once per
  completion (clears the latch on read). MainWindow's existing idle tick polls
  it; when armed, it tells the panel to `advanceToNext()`.
- **advanceToNext():** `next = getSelectedPlaylistItem() + 1`. If `next` is in
  range → `loadPlaylistItem(next)` + resume playback. If past the end → stop,
  unless **Loop playlist** is checked, in which case `next = 0`.
- Loop (once-mode off) never ends, so it never triggers — no runaway. An empty
  or single-item playlist with no next item simply stops.
- **`[ ] Loop playlist`** checkbox (persisted): wraps the last item to the
  first instead of stopping.

### 6. Bridge additions (`SequenceLoadBridge_qt`)

```
void  addCurrentAsPlaylistItem();
void  addPlaylistFiles(const std::vector<std::string>& paths);
void  appendTracksToPlaylistItem(int index, const std::vector<std::string>& paths);
void  setPlaylistScaleOverride(int pct);          // 0 = no override
bool  consumePlaylistAdvanceSignal();             // one-shot, edge-detected
bool  isPlaylistItemPlayingOnce();                // arming helper (once vs loop)
struct PlaylistTrackDetail {                       // POD — keeps card glad-free
    std::string letter;        // "A".."D"
    std::string path;          // full path (panel shortens per toggle)
    int  fromFrame, toFrame, totalFrames;
    int  scalePct;
    std::string filter;        // "linear"/"bilinear"/...
    bool ccOn;
    bool crop;
    std::string bitDepth;      // "8"/"16"/"16f"/"32f"...
};
std::vector<PlaylistTrackDetail> getPlaylistItemDetail(int index);
```

Existing accessors reused: `getPlaylistItemNames`, `removePlaylistItem`,
`movePlaylistItem`, `clearPlaylist`, `loadPlaylistItem`,
`getSelectedPlaylistItem`, `savePlaylistFile`, `loadPlaylistFile`.

### 7. Locators

`tests/ui/jefecheck/locators.py`: add object names under `playlist.*` for the
new controls (`playlist.button.addcurrent`, `playlist.button.addfiles`,
`playlist.check.autoadvance`, `playlist.check.loop`,
`playlist.check.scaleoverride`, `playlist.combo.scale`,
`playlist.check.compact`, `playlist.check.fullpaths`, and per-card leaves).

## Error handling

- All index-taking bridge calls bounds-check (return / no-op on out-of-range),
  as the existing ones do.
- `addCurrentAsPlaylistItem` with nothing loaded still produces a valid item
  (empty/default load params) — same as FLTK; it is not an error.
- Drag-drop of an unrecognized extension is ignored.
- Auto-advance with no next item and Loop off: stop cleanly, no error.

## Testing

- **Headless `--playlist-test`** (new flag in `main_qt.cpp`): load a known
  image → `addCurrentAsPlaylistItem` → `savePlaylistFile` → `clearPlaylist`
  → `loadPlaylistFile` → assert item count == 1 and the first item's track
  path/from/to round-trip. Prints `PLAYLIST-TEST PASS/FAIL`. Wire into the
  existing headless smoke-test set alongside `--render-test` / `--fx-test`.
- **Manual GUI check:** Add Current, drag-reorder, drop media to append,
  Compact/full + full-paths toggles, scale override load, and auto-advance
  across a 2-item playlist (once mode) including Loop wrap.

## Files touched

- `src/qt/PlaylistPanel_qt.{h,cpp}` — rewrite panel + add `PlaylistItemCard`.
- `src/qt/SequenceLoadBridge_qt.{h,cpp}` — new accessors + POD + advance latch.
- `src/qt/MainWindow_qt.cpp` — idle-tick auto-advance poll; `--playlist-test`
  hook wiring (flag handled in `main_qt.cpp`).
- `src/main_qt.cpp` — `--playlist-test` flag.
- `tests/ui/jefecheck/locators.py` — playlist locators.
- `developer_notes.md` — new section documenting the playlist panel + advance
  latch + TU-safe detail POD.
- `CLAUDE.md` — update the UI/playlist description.

No engine (`src/gfc*`) source changes expected; if end-of-playback detection
needs a small read-only helper on the playback manager, it will be additive
and confined to the rendering-chain TU.
