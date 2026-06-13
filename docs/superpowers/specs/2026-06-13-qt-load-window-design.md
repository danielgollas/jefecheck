# Qt Load Window — Design Spec

> Branch: `qt-experimental` (2.x). Tracking ID: PR-LAST.

## Goal

Reimplement the FLTK Load Manager as a Qt modal (`LoadWindowDialog_Qt`) that mediates the "prepare four tracks, then load them all at once" workflow. The drag-drop fast path and Quick Load file picker continue to bypass the modal for one-shot loads.

## Motivation

1. **Debug load issues with eyes on the input controls.** Today the Qt build hides the entire prep step — drop-and-go fires `loadFileIntoPlate` with implicit defaults, and a bad channel/bit-depth/scale choice surfaces only as a wrong-looking pixel later. Putting an explicit prep surface in front of the user lets them see frame range, channel, bit depth, and estimates before committing memory.
2. **Simplify the render path.** Today `gfcPlate::showPreview` flips between true and false based on async events from multiple writers (drop, autoload, FLTK loadWindow callbacks). After this change there are exactly two writers, both gated on a single boolean (`viewport.loadWindowOpen_`). No more "did the preview frame arrive before the loaded frame?" race.

## Trigger surface

| Shortcut | Menu item | Behavior |
|---|---|---|
| **Cmd+L** | File → Load Sequence Manager… | Opens the modal. Pre-populates each strip from current `gfcSequenceGUI_Qt` state. |
| **Cmd+O** | File → Quick Load… | `QFileDialog` → `loadFileIntoPlate(activePlateIdx, path)` (existing fast path, unchanged). |
| **Cmd+Shift+O** | File → Open Session… | Restores FLTK convention; today's Qt port had Cmd+O on the file picker by accident. |
| (no shortcut) | Drop on viewport | Modal closed: existing fast path. Modal open: forwarded into the strip whose track the target plate is assigned to. |

## Architecture

A new modal `LoadWindowDialog_Qt` (subclass `QDialog`) owned by `MainWindow_Qt`. The dialog owns four `TrackStrip_Qt` widgets and a single global **Load All** button. No business logic in the dialog — it composes strips and wires the global button.

Lifecycle when the modal opens:
1. `MainWindow_Qt` calls `viewport_->setLoadWindowOpen(true)`.
2. All four plates render their tracks' preview frames while the flag is true — deterministic, no per-track "was-touched" state.
3. Live edits in any strip immediately re-decode that track's preview via `gfcSequence::loadPreview()`.
4. Viewport repaints on every change so the user sees the effect immediately.
5. **Load All** → `accept()` → `setLoadWindowOpen(false)` → for each non-empty track, `trackManager.startLoadingSequence(idx)`. Plates show loaded frames as they arrive.
6. **Dismiss without Load All** (Esc / close button) → `reject()` → `setLoadWindowOpen(false)` → track GUI state retains edits (live writes already applied), but no `startLoadingSequence` call fires. Plates revert to whatever they were rendering before.

State separation: each strip reads/writes its `gfcSequenceGUI_Qt` directly. The dialog owns no canonical state — it's purely a view onto the four GUI instances.

## Components

### New files

`src/qt/LoadWindowDialog_Qt.{h,cpp}` — the modal `QDialog`. Owns four `TrackStrip_Qt` widgets in a `QFlowLayout`-based container (so 2×2 at default width, 4×1 when narrow). Owns the single global **Load All** button. No business logic — just composition, the global Load All slot, and re-entry from `MainWindow_Qt::onLoadWindowDropForwarded`.

`src/qt/TrackStrip_Qt.{h,cpp}` — one strip per track.

`src/qt/FlowLayout_Qt.{h,cpp}` — vendored from the Qt examples (it's reference code, not a built-in). Reflows children based on available width.

Strip layout:

```
┌─────────────────────────────────────────────────────────┐
│ Track A: foo.####.exr                                   │  ← header label (generic seq pattern)
├─────────────────────────────────────────────────────────┤
│ [filename________________] [Browse] [Recent ▼]          │
│ From [123 ▲▼]   To [456 ▲▼]   Scale [100% ▼]            │
│ Bit Depth [16-half ▼]  Channels [right.RGB ▼]   ☐ Crop  │
│ [Reload]                            [Unload & Clear]    │
│ 240 frames · ≈1.2 GB · ~8s                              │  ← estimates one-liner
└─────────────────────────────────────────────────────────┘
```

Each widget binds bidirectionally to one accessor on `gfcSequenceGUI_Qt`:

| Widget | gfcSequenceGUI_Qt accessor |
|---|---|
| `filename` QLineEdit | `getFilename` / `setFilename` |
| `Browse` QPushButton | `QFileDialog` → `setFilename` |
| `Recent` QToolButton menu | recents list persisted via `QSettings` (cap 10/track) |
| `header` QLabel | `gfcSequence::filenameGeneric` (read-only) |
| `from` / `to` QSpinBox | `getFrom`/`setFromFrame`, `getTo`/`setToFrame` |
| `scale` QComboBox | `getScale` / `setScale` |
| `bit-depth` QComboBox | `getCompression` / `setCompression` |
| `channels` QComboBox | `getChannel` / `setChannel`, options from `getChannelOptions` |
| `crop` QCheckBox | `getCrop` / `setCrop` |
| `Reload` QPushButton | calls bridge `reloadTrackPreview(idx)` |
| `Unload & Clear` QPushButton | calls bridge `unloadAndClearTrack(idx)` |
| `estimates` QLabel | computed from `gfcSequence::updateEstimates` output |

### Edits to existing files

**Bridge additions** (`src/qt/SequenceLoadBridge_qt.{h,cpp}`):
- `reloadTrackPreview(int trackIdx)` — runs `seq->loadPreview()` with viewport's GL context current.
- `unloadAndClearTrack(int trackIdx)` — clears loaded frames, drops preview, resets GUI defaults.
- `startLoadingAllTracks()` — iterates tracks, calls `trackManager.startLoadingSequence` for each non-empty one. Pre-aborts in-flight loads.
- `getTrackEstimates(int trackIdx) → struct{frames, bytes, seconds}` — formats the one-line estimate.
- `setTrackFilenameFromDrop(int trackIdx, std::string path)` — the modal-open drop forwarding entry.

**Viewport additions** (`src/qt/GlViewport_qt.{h,cpp}`):
- `setLoadWindowOpen(bool)` — flag read by `paintGL` to gate per-plate preview-vs-loaded.
- `dropEvent` branches on the new flag: when open, emit `fileDroppedWhileLoadWindowOpen(plateIdx, path)`; when closed, existing `fileDroppedWithScale` path.

**Main window additions** (`src/qt/MainWindow_qt.{h,cpp}`):
- File menu: add "Load Sequence Manager…" (Cmd+L), demote existing "Load Sequence" to "Quick Load…" (Cmd+O), add "Open Session…" (Cmd+Shift+O).
- Slot `openLoadWindow()` — instantiates and `exec()`s the modal.
- Slot `onLoadWindowDropForwarded(plateIdx, path)` — routes to `LoadWindowDialog_Qt::setTrackFilename`.

**OIIO loader fix** (`src/gfcimageloaderoiio.cpp`):
- Replace the `gflResize` call at line 230 with `OIIO::ImageBufAlgo::resize(out, in, OIIO::Filter2D::create(filterName, w, h))`. Map `params.filterType` to one of: `box` (nearest), `triangle`, `mitchell`, `lanczos3`. This is the change that makes the filter pref actually do something — without it the load window's filter setting would be a no-op (today's Qt-build resize is silently nearest, regardless of caller intent).

**Preferences additions** (`src/qt/PreferencesWindow_Qt.cpp`, `src/gfcStructures.{h,cpp}`):
- New "Default decode filter" combo in the Engine panel: `nearest` / `triangle` / `mitchell` / `lanczos3`. Default `lanczos3`. Persisted as `gfcSettings::defaultDecodeFilter` (int matching `FILTER*_ID` enum).
- Applied by `gfcSequence::loadPreview` and the loader worker via the existing `getLoadParamsFromGUI` path (which already passes filterType).

## Data flow

### Preview update path (live edits in the modal)

```
TrackStrip_Qt widget change (e.g. user edits "From" spinner)
   │
   ▼
slot writes to gfcSequenceGUI_Qt (setFromFrame / setChannel / setCompression / setScale / setCrop / setFilename)
   │
   ▼
TrackStrip_Qt emits trackEdited(trackIdx)
   │
   ▼
LoadWindowDialog_Qt::onTrackEdited(idx) → jefe::qt::reloadTrackPreview(idx)
   │
   ▼ (in bridge, with viewport context current)
gfcSequence::loadPreview()
   ├─ findSequenceFiles → updates from_/to_ on the GUI
   ├─ previewFrame.loadFrame(params)
   ├─ previewFrame.generateTexture()
   └─ updates estimates
   │
   ▼
LoadWindowDialog_Qt::onTrackEdited refreshes that strip's
   estimates label, channel-combo options, and seq-pattern header
   │
   ▼
viewport_->update() — plates re-render; each plate reads its track's previewFrame
   because viewport.loadWindowOpen_ == true
```

Reentrancy guard: each `TrackStrip_Qt` has a `bool refreshing_` flag set during programmatic writes to its widgets (e.g. when `findSequenceFiles` clamps the From/To range), so widget `valueChanged` signals don't bounce back through `trackEdited`.

### Load All path (commit)

```
User clicks "Load All"
   │
   ▼
LoadWindowDialog_Qt::onLoadAll()
   ├─ accept() → dialog closes
   ├─ viewport_->setLoadWindowOpen(false)
   └─ jefe::qt::startLoadingAllTracks()
        │
        ▼ (for each track with non-empty filename)
        trackManager.startLoadingSequence(trackIdx)
           ├─ spawns the worker thread that decodes frames into rawFrames queue
           └─ playback tick (QTimer) drains rawFrames → GPU textures (PR-16 path)
   │
   ▼
plate->showPreview = false (set by setLoadWindowOpen(false))
plates render loaded frames as the worker fills them
```

### Dismiss-without-Load-All path

```
User presses Esc or clicks the window's close button
   │
   ▼
QDialog::reject() (default for Esc) or close button calls reject()
   │
   ▼
LoadWindowDialog_Qt::reject() override
   ├─ viewport_->setLoadWindowOpen(false)
   └─ no startLoadingSequence call — track GUI state stays as edited,
      but loaded frames remain whatever they were before
```

### Drop-while-modal-open path

```
User drags file over viewport while LoadWindowDialog_Qt is showing
   │
   ▼
GlViewport_Qt::dropEvent (branches on loadWindowOpen_ == true)
   ├─ scale modifiers (Shift, Shift+Cmd) ignored — they don't apply
   │   when the load window owns the load configuration
   ├─ determines target plate index. Today: always 0 (matches today's
   │   fast-path behavior — "drop always goes to plate 0" per
   │   GlViewport_qt.cpp:288 comment). Future PR will switch to the
   │   plate under the cursor; same signal will carry the right idx.
   └─ emits fileDroppedWhileLoadWindowOpen(plateIdx, path)
   │
   ▼
MainWindow_Qt::onLoadWindowDropForwarded(plateIdx, path)
   ├─ trackIdx = plateManager.getPlateTrack(plateIdx)
   └─ loadWindowDialog_->setTrackFilename(trackIdx, path)
        │
        ▼ (same as user clicking Browse and picking the file)
        TrackStrip_Qt updates its QLineEdit → emits trackEdited
        → bridge::reloadTrackPreview → preview re-decodes
        → plate shows new preview (because load window is open)
```

### Render path simplification

`gfcPlate::showPreview` is now written by exactly **two** sites:

1. `setLoadWindowOpen(true)`  →  for every plate, `showPreview = (track has a decoded preview frame)`.
2. `setLoadWindowOpen(false)` →  for every plate, `showPreview = false`.

The drop-while-closed fast path no longer touches `showPreview`. It calls `startLoadingSequence` directly; plates render `frames[]` (or the existing fallback if `frames[]` is empty).

## Error handling

No popups, ever. Errors surface as red strip labels (per-track) or status bar messages (cross-track / lifecycle). The dialog can be modal-on-everything-else without producing modal-on-modal stacks.

| Scenario | Detection | UI response |
|---|---|---|
| Filename doesn't exist | `loadPreview()` returns empty string after `fileExists()` and search-paths fail | Strip's sequence-pattern label flips to red text: `"File not found"`. Estimates row shows `"–"`. Plate keeps its prior preview frame if there was one, otherwise renders the checkerboard placeholder used for empty tracks. Other strips unaffected. |
| OIIO can't open / unsupported format | `inp` is null after `ImageInput::open` | Same as "not found" — red label `"Cannot read: <reason>"` using `OIIO::geterror()`. |
| Channel index out of range | Channel-options list shorter than requested | Combo snaps to `0`, preview re-decodes. Status-bar message: `"Track A: requested channel not present, fell back to layer 0"`. |
| From > To | Spinner valueChanged | Spinbox values clamped — From is `min(from, to-1)`, To is `max(to, from+1)`. Reentrancy guard prevents loop. |
| Decoded preview too large for memory (rare) | OIIO read returns false or allocator throws | Red label + estimates row strikethrough. Plate shows checkerboard. |
| Load All clicked while a previous track load is still running | `trackManager.isLoadingSequence(trackIdx)` true | Pre-flight: abort in-progress loads via `trackManager.abortLoadingSequence(idx)` before re-firing. Status bar message: `"Aborted in-flight load on track X to start new load"`. Don't block the user. (Assumes `abortLoadingSequence` accessor; verify in implementation — today `gfcSequence::abortLoading` is the underlying call, may need a thin wrapper.) |
| OIIO filter name unknown (defensive) | `OIIO::Filter2D::create` returns null | Fall back to `box`, log a warning. User-visible: nothing. |
| Track is empty (no filename) at Load All time | `seq->myGUI->getFilename().empty()` | Skipped without error; status bar reports `"Loaded N of M tracks"`. |
| RAM exhaustion during Load All | Worker thread allocation fails (today's `freeFrames` / queue-full behavior) | Loader stops where it ran out. Status bar reports `"Track A: loaded 180/240 frames (RAM full)"`. Plate plays back whatever range made it in. No pre-flight check, no popup. |
| Drop path (modal closed): file not found | `loadFileIntoPlate` bridge returns false | Existing path: status-bar 5s message `"Load failed: <path>"`. Unchanged. |

## Testing

### UI tests (existing pytest harness)

- `test_load_window_smoke`: open via Cmd+L, assert dialog visible, four strips present.
- `test_load_window_dismiss_no_load`: open, edit Track A filename, close via Esc → preview ran but `trackManager.isLoaded(0)` stays false; widget state persists for next open.
- `test_load_window_load_all`: open, set Track A filename to a fixture sequence, click Load All → window closes, `trackManager.isLoaded(0)` flips to true, `plateManager.showPreview(0)` is false.
- `test_load_window_live_preview`: edit Track A's channel combo (multi-layer EXR fixture) → `gfcSequence::previewFrame.getChannelNames` reflects the new selection without clicking Reload.
- `test_load_window_quick_load`: Cmd+O opens QFileDialog (not the modal), picks a file, loads into the active plate's track via the fast path.
- `test_load_window_drop_while_open`: open the modal, drop a file on the viewport → Track A's filename input picks up the dropped path, preview re-decodes. Modal stays open.
- `test_load_window_filter_pref`: change default filter in Preferences from `lanczos3` → `mitchell`, restart, verify next preview decode used the new filter (assert via debug bridge accessor exposing the last `params.filterType` value).
- `test_load_window_bad_filename`: type a nonexistent path → strip label shows red "File not found", estimates row shows "–", plate stays unchanged. No popup.

### Fixtures

- New: 8-frame 64×64 EXR sequence `tests/ui/fixtures/seq/test_seq.0001.exr` … `test_seq.0008.exr`. Generated with OIIO `ImageBufAlgo` and committed (small, deterministic).
- Reuse existing PR-30 multi-layer EXR for the channel-combo test if present; otherwise add a 64×64 EXR with `R,G,B,A,right.R,right.G,right.B,right.A` channels.

### Visual baseline

Open the load window, set Track A to the multi-frame fixture, click Load All, advance 4 frames, snapshot. Locks in that loaded-frame rendering after a Load All matches preview rendering at the same frame.

### AX locators (`tests/ui/jefecheck/locators.py`)

```python
LOAD_WINDOW            = "dialog.loadwindow"
LOAD_WINDOW_LOAD_ALL   = "dialog.loadwindow.button.loadAll"
LOAD_STRIP_FMT         = "dialog.loadwindow.strip.{idx}"           # idx in 0..3
LOAD_FILENAME_FMT      = "dialog.loadwindow.strip.{idx}.filename"
LOAD_BROWSE_FMT        = "dialog.loadwindow.strip.{idx}.browse"
LOAD_FROM_FMT          = "dialog.loadwindow.strip.{idx}.from"
LOAD_TO_FMT            = "dialog.loadwindow.strip.{idx}.to"
LOAD_SCALE_FMT         = "dialog.loadwindow.strip.{idx}.scale"
LOAD_BITDEPTH_FMT      = "dialog.loadwindow.strip.{idx}.bitdepth"
LOAD_CHANNELS_FMT      = "dialog.loadwindow.strip.{idx}.channels"
LOAD_CROP_FMT          = "dialog.loadwindow.strip.{idx}.crop"
LOAD_RELOAD_FMT        = "dialog.loadwindow.strip.{idx}.reload"
LOAD_UNLOAD_FMT        = "dialog.loadwindow.strip.{idx}.unload"
LOAD_ESTIMATES_FMT     = "dialog.loadwindow.strip.{idx}.estimates"
LOAD_HEADER_FMT        = "dialog.loadwindow.strip.{idx}.header"
```

## Out of scope (deferred to other PRs)

- Render-dialog wiring — separate PR.
- Worker-thread render isolation — separate PR.
- Plate-under-cursor drop targeting (today: hardcoded plate 0; future PR).
- Per-track loaded-progress indicators in the dialog — Load All fires-and-closes, so this is moot; per-plate status bars already cover progress.

## Open assumptions worth verifying in implementation

- `gfcSequence::updateEstimates` currently writes to `myGUI->setEstimates(...)` (a string). The bridge accessor `getTrackEstimates` may need to recompute the numeric values (frames, bytes, seconds) directly rather than re-parse the formatted string. Confirm during implementation.
- `plateManager.getPlateTrack(plateIdx)` — verify this accessor exists with this exact signature on `qt-experimental`; today's PlateCard already reads `plate->trackId`, so the data is there even if the named accessor isn't.
- `OIIO::ImageBufAlgo::resize` works on `OIIO::ImageBuf`, not raw pixel buffers. The OIIO loader path may need to keep the decoded data in an `ImageBuf` longer (rather than copying into `GFL_BITMAP` early) to call `resize` cleanly. Cost: one extra buffer's worth of memory at decode time, freed before GL upload. Acceptable.
