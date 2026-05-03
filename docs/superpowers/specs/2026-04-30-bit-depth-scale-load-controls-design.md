# Bit Depth + Scale Load Controls — Design

**Status:** Approved 2026-04-30. Replaces the deferred "Qt load window
(PR-LAST)" item from the FLTK→Qt migration plan.

## Background

The FLTK build's `loadWindow.cxx` (~1,300 lines) was a 4-track-wide
modal dialog that controlled per-track frame range, scale, gamma,
format, channels, and load mode before committing a load. The Qt
build has shipped enough alternatives that the modal is no longer
necessary:

- **Drag-and-drop** loads instantly into the active plate
  (`GlViewport_Qt::dropEvent` → `loadFileIntoPlate`).
- **Cmd+O** (PR-34) opens a `QFileDialog` for the keyboard-driven flow.
- **Plate cards** expose post-load gamma / scale / pan / channel mask
  / track per-plate (PR-7, PR-13).
- **EXR layer combo** picks channels per-plate at runtime (PR-30).
- **Auto-flip from preview to playback** (PR-33) handles the FLTK
  Load-dialog-close-as-commit transition.

Two FLTK Load window controls have **no** Qt equivalent today:

1. **Bit depth** (`compression` field on `gfcSequenceGUI`, one of
   `GFC_8BPC` / `GFC_16BPC` / `GFC_16HALF` / `GFC_4BPC` /
   `GFC_S3TCDX1`). Affects how the texture is uploaded; cannot be
   changed after load without reloading.
2. **Scale** (percentage downsample applied at load). Same constraint —
   set once at load, fixed afterward.

This spec adds those two and **closes** PR-LAST. No load window is
built; instead the controls slot into existing surfaces that match
how the user actually uses them.

## User decisions captured during brainstorming

- Bit depth and scale are set **once per session**, not per load
  (i.e. "I'm always grading at 16-half, not picking per clip").
- Preferences feels too hidden for bit depth. Status bar surfaces
  better — the user sees it, the setting is one click away, and the
  status bar already hosts the load-related labels (Layout, Track,
  Loaded, Startup).
- Scale is rare enough that a power-user modifier is the right
  affordance. No UI control needed.
- Scale modifier mapping: plain drop = 100%, **Shift+drop = 50%,
  Shift+Cmd+drop = 25%**. The 25% case covers 8K → 2K proxy work;
  Shift = 50% covers the 4K → 2K common case.

## Design

### 1. Bit depth: status-bar combobox

A new `QComboBox` permanent widget on the main window status bar,
positioned **leftmost** of the right-aligned permanent labels. Final
status bar reads:

```
[ Depth: 16-half ▼ ]  Layout: …  Track: …  Loaded: …  Startup: …
```

**Options** (matching `UIConstants.h` `gfctextureformatmodes`):

| Display | Underlying enum | Notes |
|---|---|---|
| `8` | `GFC_8BPC` | 8-bit unsigned per channel — proxy / SDR |
| `16` | `GFC_16BPC` | 16-bit unsigned — DPX, 16-bit TIFF |
| `16-half` | `GFC_16HALF` | OpenEXR half-float — VFX default |
| `32-float` | `GFC_4BPC`* | 32-bit float (full-precision) |

*The enum name `GFC_4BPC` is a historical misnomer in the codebase
— actually means 4 bytes/component = 32-bit float. We display
`32-float` and silently map to `GFC_4BPC`. A code comment notes this
trap. Renaming the enum is out of scope (touches FLTK code paths).

`GFC_S3TCDX1` (DXT1 compressed) is not in the dropdown. It's a
load-time storage optimization, not a quality choice; if anyone
needs it back, expose via Preferences in a follow-up.

**Default:** `16-half` (`GFC_16HALF`) — current implicit default for
the EXR / DPX VFX flow.

**Object name:** `statusbar.depth.combo` (Qt AX → Mac2 / XCUITest
identifier ENDSWITH predicate).

**Persistence:** `QSettings` key `Engine/defaultTextureFormat`,
integer holding the enum value. Loaded on `MainWindow_Qt`
construction (mirrored into `sett.defaultTextureFormat`) before
any sequence load can fire; written on every combo change.

**Tooltip:** *"Bit depth used when loading new sequences. Existing
plates keep their current depth until reloaded."*

**Behavior contract:**

- `gfcSettings` has no per-load default bit depth field today
  (only `fp16` boolean and `textureCompression` boolean, which
  are different concepts). Add a new field
  `int defaultTextureFormat` to `gfcSettings`, defaulted to
  `GFC_16HALF` in the constructor.
- Combo change writes `sett.defaultTextureFormat` **and** persists
  to QSettings.
- Subsequent loads (drag, Cmd+O, --open-file) call
  `seq->myGUI->setCompression(sett.defaultTextureFormat)` before
  `loadPreview` so the chosen depth applies. The existing FLTK
  Load window's per-track compression chooser path is
  unaffected — it still writes through
  `gfcSequenceGUI_FLTK::setCompression` directly. Only the Qt
  build's defaulted-load path consults `defaultTextureFormat`.
- Already-loaded plates are unaffected. Switching the combo from
  16-half to 8 doesn't reload anything; the user must reload to
  pick up the change. The tooltip explains this.

### 2. Scale: drag modifier (no UI)

`GlViewport_Qt::dropEvent` reads `event->keyboardModifiers()`:

| Modifiers | Scale | Display string passed to `setScale` |
|---|---|---|
| (none) | 100% | `"100"` |
| Shift | 50% | `"50"` |
| Shift+Cmd | 25% | `"25"` |
| any other combo | 100% (modifier ignored) | `"100"` |

**Wiring:** `GlViewport_Qt::dropEvent` already emits
`fileDropped(QString)`. Promote that to
`fileDroppedWithScale(QString, float)` — old signal kept and
forwarded with `scale=1.0` so other connected slots don't break.
`MainWindow_Qt::onFileDropped` becomes
`onFileDropped(QString, float)` and threads scale through to a new
optional `loadFileIntoPlate(plateIdx, path, scale)` overload.

**Bridge:**
`jefe::qt::loadFileIntoPlate(path, plateIdx, kickOffSequenceLoad,
scale)` grows a 4th defaulted parameter (`scale = 1.0f`). Inside,
before `loadPreview()`:

```cpp
char buf[8];
std::snprintf(buf, sizeof(buf), "%d", int(scale * 100));
seq->myGUI->setScale(buf);
```

`setScale` takes `std::string` because the FLTK Choice widget
emitted percentage strings ("50", "25"); we keep that contract.

**Discoverability:** Drop without Shift = 100%. Drop *with* Shift
fires a 3-second status-bar message: *"Loaded foo.0001.exr at 50%
scale"*. Without that flash, the modifier is invisible — the user
would Shift-drop and have no idea why the file looks different
from a plain drop.

**Cmd+O dialog:** unchanged. No scale picker. Power users use
drag; menu users get 100%.

**Why not a keyboard shortcut to cycle scale modes?** Modes only
matter at load time. Adding a Cmd+Shift+S "set load scale" toggle
would still need a visible state to be useful, which contradicts
the user's "preferences too hidden" feedback. Modifier-on-drop
binds the choice to the action and avoids both problems.

## Files touched

| File | Change |
|---|---|
| `src/qt/MainWindow_qt.cpp` | Add `depthCombo_` member; populate & wire to `sett.defaultTextureFormat`; restore from / save to QSettings (`Engine/defaultTextureFormat`); add `addPermanentWidget` call leftmost. Update `onFileDropped` signature. |
| `src/qt/MainWindow_qt.h` | Declare `depthCombo_` and the new `onFileDropped(QString, float)` slot. |
| `src/qt/GlViewport_qt.cpp` | Read `keyboardModifiers()` in `dropEvent`; emit `fileDroppedWithScale`. Keep old `fileDropped` signal forwarded with 1.0. |
| `src/qt/GlViewport_qt.h` | Add the new signal. |
| `src/qt/SequenceLoadBridge_qt.h/.cpp` | `loadFileIntoPlate` grows defaulted `float scale = 1.0f`; sets per-sequence scale before `loadPreview`. |
| `src/gfcStructures.h` | Add `int defaultTextureFormat` field to `gfcSettings`; initialize to `GFC_16HALF` in the constructor. |
| `tests/ui/jefecheck/locators.py` | Add `STATUSBAR_DEPTH = "statusbar.depth.combo"`. |
| `tests/ui/test_load.py` | New test verifying the combo persists across launch (set value → quit → relaunch with same `--config-dir` → assert combo shows the saved value). |

## Test plan

### Unit / build
- `cmake --build build_qt` clean.

### Behavioural (Mac2 / Appium)
- `test_load.py`:
  - **`test_depth_combo_default_is_16_half`** — fresh launch shows
    `Depth: 16-half` in the combo title.
  - **`test_depth_combo_persists_across_launch`** — set to `8`,
    quit, relaunch with the same `--config-dir`, assert combo
    title is `8`.

### Manual
- Drop a 16-half EXR sequence → frames advance, depth combo
  unchanged at `16-half`.
- Switch combo to `8` → drop the same EXR → loaded as 8-bit
  (status bar message confirms; visual quality drops noticeably
  on smooth gradients).
- Shift+drop a 4K image → status bar flashes "Loaded … at 50%
  scale". Plate card's scale reads `0.5`.
- Shift+Cmd+drop the same image → status bar flashes `… at 25%
  scale`.
- Drop without modifier → 100%, no flash.

### Out of scope for this PR
- Per-plate bit depth (one global setting, matches FLTK).
- Reloading existing plates when depth changes.
- Scale picker in the Cmd+O dialog.
- Resurrecting `GFC_S3TCDX1` (DXT1) as a depth choice.

## What this closes

- **Plan task #90** — "PR-LAST: Qt load window (after UX
  revisions)". Ship this PR, mark task complete, no full Load
  Manager planned.
- **Plan Phase 2E item 2** — "LoadWindow_qt". Replaced by this
  smaller surface.

## Sequencing

This PR (PR-35) sits behind the four open against `qt-migration`:

1. **PR #63** drag-preview fix — already merged.
2. **PR #62** text renderer + shortcuts — already merged.
3. **PR #65** Cmd+O Load Sequence — needs rebase after #62 merged
   (touches MainWindow_qt menu region).
4. **PR #64** About + Fullscreen — needs rebase same reason.
5. **PR-35 (this)** — branches off `qt-migration` after the above
   land. Conflicts likely on the same MainWindow_qt status-bar
   region; resolution is mechanical (interleave the new combo
   with the existing labels).
