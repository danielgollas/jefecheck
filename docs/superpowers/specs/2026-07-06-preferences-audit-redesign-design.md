# JEF-16 — Wire up all preferences + redesign the Preferences window

**Ticket:** JEF-16 (Story) — "Wire up all preferences to actually affect the product"
**Branch:** `JEF-16-wire-up-preferences` (off `qt-experimental`, sitting on the merged JEF-13 design tokens)
**Date:** 2026-07-06

## Goal

Two intertwined deliverables:

1. **Correctness** — every preference the user can see either actually affects the
   product or is removed. Go section by section; for each setting decide *used /
   desired / works* and act on it.
2. **Redesign** — refresh the Preferences window and its sections in the newest
   design language (JEF-13 discreet VFX-dark tokens), as we touch each section.

Disposition bias: **wire-up-biased** — prefer making a dead setting work; only
remove settings that are truly obsolete or duplicated.

## Context (current state)

### The window
`src/qt/PreferencesWindow_qt.{h,cpp}`. A modal `QDialog`: a fixed-width
`QListWidget` sidebar selects pages in a `QStackedWidget`; each page is a plain
`QFormLayout`. Six pages today:

- **General**, **Engine**, **Formats** — real pages.
- **Text**, **Remote**, **Paths** — `buildPlaceholderPage()` stubs ("coming soon").

Widgets bind to the global `sett` (`gfcSettings`) and mutate it **live** on
change. **Done** calls `saveSettings(&sett)` (writes the JefeCheck XML in
`getApplicationDataPath()`); **Cancel** just closes.

### Persistence is effectively broken (cross-cutting problem)
**Critical finding:** `saveSettings(const gfcSettings*)` and `readSettings(gfcSettings&)`
in `gfcStructures.cpp:314` / `:379` are **empty stubs**. There is **no global
settings file** written or read. So the Preferences dialog's Done →
`saveSettings(&sett)` is a **no-op**: changes mutate the in-memory `sett`, take
effect for the current session (only if consumed), and are **lost on restart**.

The only settings that actually persist today go through **Qt `QSettings`**, set ad
hoc in a handful of call sites: `Engine/defaultDecodeFilter`,
`Engine/defaultTextureFormat` (`MainWindow_qt.cpp:80`), `Session/startupBehavior`,
`Playlist/compactView`, `Playlist/showFullPaths`, `MainWindow/lastLoadDir`,
`UI/statusBarVisible`. Everything else in the Preferences window is session-only.

**Consequence for this ticket:** "wire up all preferences to actually affect the
product" includes *making them persist*. We standardize on **`QSettings`** (the only
working store) and add a small persistence backbone in the shell task:
- **Load** — at startup, read persisted keys into `sett` (extend the existing
  `MainWindow_qt.cpp` QSettings-load block to cover every persisted preference).
- **Save** — on Done, write every preference to `QSettings` (replace the no-op
  `saveSettings(&sett)` call with a real `writePreferences()` that enumerates them).
- **Cancel** — snapshot `sett` (+ touched keys) on open; restore on reject.

The dead legacy XML `saveSettings`/`readSettings`/`saveSetting`/`setWidgetFromNode`
path is left as-is (not resurrected); we do not add a second store. Removing the 19
dead struct fields is safe — none are persisted anywhere.

### Two behavior bugs to fold in
- **Cancel doesn't revert.** Widgets write `sett` live, so Cancel closes with the
  changes already applied in memory (they just aren't saved to XML).
- **Duplicate persistence.** A setting can be read from one store and written to
  another, so the UI and the consumer disagree.

## The audit

Method: cross-reference **exposed in the current UI** against **consumed in the
active build** (excluding the build-filtered `gfcimageloaderexr` / `gfcimagesaver_exr`,
and the definition/UI files themselves). "Consumed" = name referenced in active
source; per-setting *does it actually take effect* is verified during that
section's execution pass.

### Quadrant ① — Exposed + wired (keep; verify each takes effect)
`enableCrashRecoverySession`, `startupSessionBehavior`,
`aspectBarsOpacity`, `renderingEngine`, `vsync`, `maximumFramesInQueue`,
`numOfPartitions`, `balanceReads`, `forcePBO`, `defaultDecodeFilter`,
`defaultTextureFormat`.

> **Note:** `bgColor` was initially assumed wired but is actually **dead** — the
> viewport background is a hardcoded black clear (`GlViewport_qt.cpp` fallback +
> the plate render path); nothing reads `sett.bgColor` to color it, and
> `gfcsessionmanager.cpp` even saves a hardcoded literal. Moved to quadrant ②.

### Quadrant ② — Exposed + DEAD (the core problem — fix each)
| Setting | Verdict |
|---|---|
| `bgColor` | **Wire** — drive the viewport background clear from `sett.bgColor` (grayscale); today it's hardcoded black. Basis for the checkerboard feature below. |
| `defaultBrowsePath` | **Wire** — seed file dialogs' initial directory |
| `startFullscreen` | **Wire** — apply fullscreen at startup |
| `openLoadWindowAtStartup` | **Wire** — open the Load window at startup when set (today nothing reads it) |
| `processorPriority` | **Remove** — low value on modern macOS; wire cost ≫ benefit |
| `exrIgnoreDisplayWindow` | **Wire** to the OIIO loader. *Note:* the OIIO loader currently always uses the EXR **data window** (= "ignore display window" already). Wiring the **default (0 = honor display window)** requires implementing display-window compositing (mirror legacy `gfcimageloaderexr.cpp:884-918`) — the larger of the two. |
| `exrIgnoreHeadersAspectRatio` | **Wire** to the OIIO loader — trivial: `if (!sett.exrIgnoreHeadersAspectRatio) quadSizeX *= spec.get_float_attribute("PixelAspectRatio",1.0f)` (mirror `exr.cpp:1447-1450`). |
| `exrExposure`, `exrDefog`, `exrGamma`, `exrKneeLow`, `exrKneeHigh` | **Remove** — obsolete; only the *disabled* custom EXR loader consumed them (OIIO owns EXR now) |

### Quadrant ③ — Hidden + wired (add UI for user-facing ones)
Surface: `showThumbnails`, `feedbackMessageFadeDelay`, `feedbackMessageSize`,
chat group (`chatFadeDelay`, `chatAutoFade`, `chatTextBG`, `chatFontSize`,
`chatOpacity`, `chatDisplayLines`), `remotePointerFadeDelay`, `remotePointerColor`,
`nickName`, `sendRemoteLoadRequests`, `autoAcceptRemoteLoadRequests`,
`useSearchPaths`.

Keep hidden (internal GL/runtime state, not user prefs): `glsl`, `fbo`, `fp16`,
`filterMin`, `filterMax`, `textureRectangles`, `framingMode`, `loopMode`,
`loopPriority`, `lutPath`, `maxRecentSessions/Browsed/IPs`.

### Quadrant ④ — Hidden + DEAD (remove from struct)
`playbackOnLoad`, `textureCompression`, `maxRecentFXStacks`, `maxRecentFXs`,
`defaultLUTName`, `feedbackMessageOn`, `serverNickname`, `clientPort`,
`serverPort`, `licensePath`, and the duplicate `playlistShowCompactView` /
`playlistShowFullPaths` (live feature already in `QSettings`).

### Half-implemented features (decided)
- **Search Paths** (`searchPaths`, `searchPathsRecursive`, `useSearchPaths`) —
  **complete it.** `useSearchPaths` is consumed in `gfcSequence.cpp`; wire the
  path list + recursive flag and give it a UI section.
- **Mirror Paths** (`mirrorPaths[16]`) — **defer.** `numOfPartitions` is wired but
  the mirror-path strings aren't; multi-mirror parallel loading is a large, niche
  feature. Remove `mirrorPaths` from the UI surface for now (leave the struct
  field or drop it — decided in the Engine section pass); revisit in a later ticket.

> The full per-field matrix above is the **starting** classification. Each section's
> execution pass re-verifies "does it actually work" by reading the consumer, and
> updates the verdict if reality differs. Treat the matrix as living.

## New feature — checkerboard background

Add a **"Checkerboard background"** option (new setting, e.g. `bgCheckerboard`,
default off) in the General section next to the background-color picker. Behavior:

- **Off** — the viewport background is a flat fill of `sett.bgColor` (grayscale).
  (This alone is the `bgColor` wiring from quadrant ②.)
- **On** — instead of a flat fill, draw a checkerboard whose two shades are
  **derived from `bgColor`**: one cell = `bgColor`, the other = `bgColor` offset by
  a fixed delta (lighten if dark, darken if light, clamped to [0,1]) so the pattern
  reads at any background value. VFX-viewer convention (shows image alpha/extent).

Implementation notes:
- Drawn in the viewport-background pass (the flat clear the plate render currently
  does behind/around the plates) — exact site pinned during the General pass; it is
  the same place `bgColor` gets wired, so the two ship together.
- Checker cell size: a sensible fixed pixel size (e.g. 16–32 px), in **physical**
  pixels (respect `dpiScale`/`devicePixelRatioF` like the text/overlay code, so it
  doesn't halve on Retina). A cell-size preference is out of scope unless trivial.
- Persist alongside `bgColor` in the same store (single-path rule).

## Redesigned window

Replace the flat sidebar+form with the JEF-13 discreet aesthetic
(`src/qt/theme/design-tokens.md`, `jefecheck_dark.qss`), grouping related controls
with `CollapsibleSection_qt`. Keep the sidebar-selects-page shell (it works and is
a11y-mapped) but restyle it and give pages real structure.

### Sections (post-redesign)
1. **General** — background color **+ checkerboard-background toggle**, default
   browse path, start fullscreen, open Load window at startup, on-launch session
   behavior, crash recovery, timeline thumbnails, aspect-bar opacity,
   feedback-message group (size / fade).
2. **Playback & Engine** — rendering engine, vsync, max frames in queue, loader
   partitions, balance reads, force PBO, default decode filter, default bit depth.
3. **Formats** — EXR: ignore display window, ignore header aspect ratio (both wired
   to the OIIO loader). *Tonemap floats removed.*
4. **Search Paths** *(new)* — enable, path list (add/remove), recursive.
5. **Remote** *(fills placeholder)* — nickname, chat group, remote-pointer group
   (color + fade), send/auto-accept load-request toggles.
6. **Text** *(fills placeholder)* — `GfcTextRenderer` prefs: font / bold, size,
   color, opacity, shadow (offset / color / blur / enabled), hint mode,
   nearest-vs-linear filter, gamma. Persistence path (XML per developer_notes §22
   vs `QSettings`) pinned during this section's pass; wire through the renderer's
   setter API and confirm it applies live.

The standalone **Paths** tab is removed (default browse path → General; LUT/search
paths handled in General/Search Paths).

### Behavior changes (window-shell section)
- **Cancel reverts.** Snapshot `sett` (and any touched `QSettings` keys) on open;
  restore on Cancel/reject. Done saves + closes.
- **One persistence path per setting.** Each control reads and writes the single
  store that its consumer actually reads. Remove orphan duplicate fields.
- **Token styling.** The global `jefecheck_dark.qss` styles by class selector, so
  plain widgets inherit the discreet button/input/checkbox/combo recipes for free.
  For section headers / cards / accent buttons, add a small dialog-scoped stylesheet
  with `[role="section"]` / `[card="true"]` / `[accent="true"]` property hooks
  mirroring RemotePanel's `kRemoteStyle` (`RemotePanel_qt.cpp:27-89`); group controls
  with `CollapsibleSection_qt` (`setContentWidget()` idiom). Follow the global **warm
  orange** accent (not RemotePanel's slate).
- **Object-name standardization.** The window currently mixes two schemes
  (`preferences.<section>.<field>.<role>` vs `prefs.engine.<field>`). Standardize on
  the dotted **`preferences.<section>.<field>.<role>`** leaf scheme and update
  `tests/ui/jefecheck/locators.py` in lockstep, so Mac2/XCUITest keeps resolving
  widgets by trailing leaf.

## Execution order

Each step is its own commit with verification. Approach A: shell first, then
sections.

0. **Window-shell redesign** — new chrome (token styling, `CollapsibleSection`
   scaffolding, sidebar restyle), Cancel-reverts snapshot/restore, persistence-path
   discipline. No setting changes yet; existing three pages render in the new shell.
1. **General** — redesign + wire `bgColor` (viewport background) **and add the
   checkerboard-background option**; wire `defaultBrowsePath`, `startFullscreen`,
   `openLoadWindowAtStartup`; add `showThumbnails` + feedback-message group; verify
   quadrant-① General settings.
2. **Playback & Engine** — redesign + verify wired settings; remove
   `processorPriority`; decide `mirrorPaths` struct disposition.
3. **Formats** — remove 5 tonemap floats; wire the 2 EXR ignore-* toggles to the
   OIIO loader.
4. **Search Paths** (new) — UI + wire `searchPaths` / `searchPathsRecursive` /
   `useSearchPaths`.
5. **Remote** (new) — UI + wire nickname, chat group, remote-pointer group,
   load-request toggles.
6. **Text** (new) — UI + wire `GfcTextRenderer` prefs; confirm live apply.
7. **Struct cleanup** — remove quadrant-④ dead fields and their save/load lines;
   build clean.
8. **Final verification** — full build, launch, walk every section, screenshot.

## Verification strategy

- **Build:** `cmake --build build` clean after each section.
- **Behavioral:** for each newly-wired setting, exercise the actual product effect
  (e.g. set `startFullscreen` → relaunch → window is fullscreen; set decode filter →
  load a downscaled image → correct filter used). Prefer driving the real app
  (`/run`, `/verify`) over asserting on the widget alone.
- **Visual:** launch and screenshot each redesigned section; compare against the
  JEF-13 token recipes.
- **Regression:** confirm object names still match `tests/ui/jefecheck/locators.py`.

## Out of scope (explicit)

- Multi-mirror parallel loading (`mirrorPaths`) — deferred to a later ticket.
- Re-implementing EXR tonemap-on-load via OIIO — the 5 tonemap settings are removed,
  not reimplemented.
- Wholesale migration of persistence to a single store — we enforce *one path per
  setting* and remove duplicates, but don't rewrite the persistence layer.

## Risks

- **"Consumed" ≠ "works."** Grep found references, not correctness; the per-section
  read-the-consumer pass is where real verification happens. Budget for verdicts
  flipping.
- **Persistence duplication** can hide bugs where UI and consumer use different
  stores — the one-path rule addresses this but each case needs checking.
- **Struct field removal** touches `saveSetting`/`setWidgetFromNode` in
  `gfcStructures.cpp`; must remove both the field and its save/load lines together,
  and confirm old XML files still load (unknown/missing keys tolerated).
