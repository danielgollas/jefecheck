# Panel polish (launch milestone P2): density, Plate Manager, FX, Remote

Status: design, agreed in conversation 2026-11-12/13. Branch `feature/panel-polish` from `qt-experimental`.

## Why

The Qt build works but feels cluttered: controls in one row have different heights, text sizes differ between labels and buttons, spacing is chosen per widget, and some panels squash or clip when docks snap or change shape (the Load window's path field against its Browse button, the Timeline transport row's overlapping F/In/Out labels, the LUT panel when its dock changes shape). Before launch, every panel should look like it belongs to one pro tool, in the family of Nuke and Resolve, and FX and Remote should be at least as well organised as the LUT panel, which the owner describes as well organised, with good visualisations, though not a model UI.

Qt 6 already renders at device pixel ratio 2 on Retina; the clutter is sizing and spacing, not missing high-DPI support.

## Goals

1. **One density rule across the app** (chosen from mockups: "24 px").
2. **Plate Manager** shows the plates you care about, in a shape that fits the dock, and makes the active plate obvious in the dock and the viewport.
3. **FX parameters** drawn the way each effect's `.jfx` file says (Nuke-style slider rows, knobs, or plain fields), with soft ranges, reset, and a model ready for hardware controllers.
4. **Remote** follows one clear flow: Start, Set up, Waiting, In session.
5. **LUT panel** keeps its content and stops misbehaving when its dock changes shape.

## Non-goals

- No colour or theme changes: the dark palette and orange accent stay.
- No new effects and no shader changes.
- No change to remote networking, the coordinator, sign-in, or the wire format; Remote is a UI rework over the existing `jefe::qt` remote state (`RemoteUiState`, `RemotePhase`, participants, pending joiners).
- No MIDI or Tangent driver in this milestone (see Hardware controllers).
- No user override of an effect's widget style.

## Anti-goals (would make this a failure even if it works)

- A panel that looks tidy at one dock size and breaks at another.
- Existing `.jfx` files that stop loading or lose a control.
- Remote steps that hide what state you are in, or a button that relabels itself to report progress.
- Any regression in the macOS drag-performance patterns (`developer_notes.md` §5–6) or the TU separation (§1).

## 1. Density rule

Decided:

- **Text:** 13 px everywhere, including buttons (today: base 12 px, `QPushButton` 11 px in `src/qt/theme/jefecheck_dark.qss`).
- **Control height:** every `QLineEdit`, `QPushButton`, `QToolButton` (text or icon), `QComboBox`, `QSpinBox`, `QDoubleSpinBox` is exactly **24 px** tall. Square icon buttons are 24 × 24. Checkboxes and radio buttons sit centred in a 24 px row.
- **Spacing scale:** 4 px between controls that belong together (transport buttons, a value and its unit), 8 px between unrelated controls in a row and between rows, 12 px inside group boxes and cards and between groups.
- Labels are vertically centred on their field.
- The values live in the QSS plus one shared C++ header of spacing constants (for example `jefe::qt::ui::kGapTight=4, kGap=8, kGapGroup=12, kControlH=24`), so no panel picks its own margins. Expect most of the change to be deleting per-widget `setContentsMargins`/`setSpacing`/`setFixed*` calls.
- **Narrow docks:** each dock has a minimum width; below it, a row wraps or drops its text labels (keeping icons and tooltips), never squashes or overlaps.

Then an **audit of every dialog and dock** against the rule: Load window (`TrackStrip_qt.cpp`), Timeline, Plate Manager, LUT, FX, Remote, Playlist, Notes, Preferences, Render, About, System Specs. Known breaks to fix: the Load window path field and Browse button, the transport row's F/In/Out labels, clipping in narrow docks.

## 2. Plate Manager

Today (`PlateManager_qt.cpp`) the dock always shows all four plate cards, whatever the viewport layout. The FLTK build showed only the active plate's controls (`layouts1`–`layouts4` stacked at one spot, `setActiveQuad` showing one), with a row of four small squares (`activeViewport0`–`3`) whose active one turned orange.

Decided:

- **Mode switch** at the top of the dock, three segments, remembered in QSettings:
  - **Active plate:** one card, the active plate's.
  - **Visible plates:** one card per plate on screen (1 in 1x1, 2 in 2x1 or 1x2, 4 in 2x2). **Default** (my call; it is the mode the owner described).
  - **All four:** today's behaviour; cards of plates not on screen are dimmed.
- **Arrangement by dock shape:** the cards copy the viewport arrangement when there is room, else fall back to a strip:
  - **Bottom/top dock:** one row, left to right, wide-short cards; wraps to two rows when the window is too narrow for four.
  - **Side dock:** one column, A→D, narrow-tall cards; scrolls; clicking a plate scrolls its card into view.
  - **Floating:** a map of the viewport: side by side for 2x1, stacked for 1x2, a 2×2 grid for 2x2. The floating window resizes when the layout changes.
  - All four mode arranges as a 2x2 viewport would.
- **Active plate indicator in the dock header** (all modes): a tiny map of the current viewport layout, about 44 × 24 px, one cell per visible plate, the active one filled orange; clicking a cell makes that plate active. (Chosen over an FLTK-style line of squares, "for now".)
- **Active plate indicator in the viewport:** a thin orange frame (1–2 px, the theme accent) around the active plate whenever more than one plate is on screen. Never in 1x1, never in renders, Stamp Notes or review-package thumbnails. Always on (my call; the alternative was a brief flash on change).
- Changing the viewport layout reflows the cards immediately; clicking a card still makes its plate active (my calls).
- **Bottom dock height:** the bottom dock (Plate Manager beside or above the Timeline) stays at about today's height under the 24 px rule (owner's decision). The wide-short plate card is redesigned to fit that height (fewer rows: secondary controls move into a compact second line or a per-card overflow menu) rather than re-spaced taller. Whatever the arrangement, the Timeline keeps its full usable height: its scrubber and track lanes are never squeezed to make room for plate cards.

## 3. FX parameters

Today `FXParamPanel_qt.cpp` draws floats as `QDoubleSpinBox`, bools as checkboxes, and choice/texture/cube/LUT as combos, one row each, and ignores the `.jfx` layout widgets (`newLine` ×99, `spacer` ×17 across the 35 bundled effects).

Decided: **the effect file has the final say** on how each control is drawn; there is no per-card user switch.

- New optional widget attribute **`style`**:
  - `slider`: a Nuke-style row: label column (right-aligned), number field, slider, reset.
  - `knob`: a rotary knob with label above or below and the value under it; drag up/down to turn, double-click to type.
  - `field`: just the number field (drag on it to scrub).
  - Absent: a float is `slider`. So every existing `.jfx` keeps working with no edits.
- New optional attributes **`softMinimum`/`softMaximum`**: the range the slider or knob covers. `minimum`/`maximum` stay the hard limits for typed values. Absent: soft = hard, unless the hard range is huge (the 9999-style maxima), in which case the builder picks a sensible default (for example 0–2 around a default of 1) and documents the rule.
- **`newLine` and `spacer` are honoured**: widgets between two `newLine`s share one row, so an author can lay four knobs side by side like a controller bank. A `slider` row always takes the full width: sliders inside one group stack one per line (so `ADD.jfx`'s Factor 1 and Factor 2 stay two rows), while knobs, fields, bools and combos in a group sit side by side and wrap to the next line when the dock is too narrow. This applies to every `.jfx`, bundled or user-written, so old files never squash (my call).
- **Same behaviour in every style:** changed-from-default shown by an orange label; reset to default (a click on the reset dot for rows, the context menu for every style); double-click to type; drag to scrub. `labelColor` from the file is kept where it is not white (it is used today to dim secondary parameters such as Average Y).
- **Card header:** drag handle, active checkbox, effect name, remove. The cards and their drag-reorder stay as they work today.
- **The bundled effects** are updated in this milestone: a `style` and soft range picked for each float (my call).
- The `.jfx` format change is documented in `developer_notes.md` §23 and in the manual's FX section.

### Hardware controllers (designed for, not built)

Assignment belongs to the parameter, not its look, so changing a file's `style` never loses an assignment. In this milestone every parameter gets a stable identity that **follows the effect, not its stack position** (my call: dragging to reorder, or a second copy of the same effect, must not move an assignment): each FX instance on a plate gets a persistent instance id, saved with the session, and a parameter is (plate, instance id, `varName`) and one entry point that sets it from a normalised 0–1 value over its soft range, so a later MIDI/Tangent milestone can bind encoders without touching the widgets. No assign menu is shown until controllers exist (my call: no dead UI).

## 4. Remote

Today (`RemotePanel_qt.cpp`) the offline panel has three tabs, **Host** (LAN server: name, port, password), **Cloud** (internet hosting: group, name, knock, password, timeout, max peers; browser sign-in on first use, reported by relabelling the Host button "Signing in…" / "Waiting for browser…") and **Join** (name, Code-or-IP radio, fields, password). Then the body swaps: a knock card replaces the forms for a waiting joiner; in session the forms hide and Participants and chat appear, with a code banner for a cloud host.

Agreed flow (the owner: "that matches, go with it"):

1. **Start:** two large choices, *Host a session* and *Join a session*.
2. **Set up:**
   - **Host:** one switch, *Over the internet* (default; you get a code to share) or *On this network* (shows your IP and port). For internet hosting the **Group** picker and New group button sit at the top, as today's groups remember their own name, knock, password, timeout and max people and fill the fields below (groups keep working as they do; my call). Then session name, password, "Let people in myself" (knock; internet only, as today, since LAN sessions have no lobby). Timeout, max people and port folded under *Advanced*. The signed-in account and Sign out sit in Advanced for internet hosting.
   - **Join:** one field that takes a code (JEFE-XXXX) or an address (`host:port`) and works out which; your name; password if the session asks. Join.
   - Back returns to Start.
   - Cancel while signing in aborts the browser wait: `AuthSession` gains an abort that closes the loopback listener (the one sign-in change allowed; my call). A sign-in that completes after Cancel stores its token but never starts hosting on its own; the `hostRetryPending_` retry is cleared by Cancel. The Waiting step may read panel-local and `AuthSession` state for the sign-in sub-step; `RemoteUiState` does not need a new phase.
3. **Waiting:** its own step with plain words and always a Cancel: "Starting…", "Signing in: finish in your browser", "Waiting for the host to let you in" (with the code and elapsed time, as the knock card has today). Sign-in is shown here as a step, never on a button.
4. **In session:** one layout for host and joiner: header with the code or address and Copy; People, each row keeping today's connection health (dot, round-trip time, direct or relay), and the host also sees Admit / Deny rows for anyone waiting; chat; one red button, *End session* (host) or *Leave* (joiner). Credits stay in the header, host-only.

A step strip at the top (Start · Set up · Waiting · In session) shows where you are. The step is derived from `RemoteUiState` every refresh, as visibility is today; there is no separate state to keep in sync. The UI-preview path (`uiPreviewActive_`) is updated to show the In session step.

## 5. LUT panel

Content stays (list, info, 1D curve / 3D cloud preview and its view controls). Known cause of the shape problems: `LUTPreview_Qt::resizeEvent` flips the info/controls section between beside and above the canvas at a 1.25 aspect threshold with a 200 px maximum width when beside, so snapping a dock makes it jump and squeezes the controls grid. The builder fixes it under the density rule: a stable choice with hysteresis (or a fixed side placement per dock area), controls that wrap instead of squash, and the canvas keeping a minimum size. The 24 px rule applies to its checkboxes, combo and slider.

## Constraints

- Qt 6 widgets and QSS; no QML. Icons stay programmatic (`jefe::qticons`).
- TU separation: UI code reaches the managers only through `jefe::qt::*` (`SequenceLoadBridge_qt.cpp`), `developer_notes.md` §1. The viewport frame is drawn in the GL plate path (`gfcPlate`/`gfcPlateManager`), guarded off in render and readback paths.
- Plate-card writes still call `propagatePlateChanges()` (§3); drag-perf patterns (§5–6) kept.
- Object names follow the dotted-leaf scheme in `tests/ui/jefecheck/locators.py`; new widgets get names, renamed ones update the locators.
- Must look right on macOS (DPR 2), and on Windows/Linux at 100 % and 150 % scaling.

## Left to the builder

- Order of work and how to split it into PRs (the density rule first is natural, since everything else is checked against it).
- The knob's exact drawing, drag sensitivity, and fine-adjust modifier.
- The soft-range default rule for very large hard ranges, and the per-effect styles chosen for the bundled `.jfx` files.
- How narrow-dock wrapping is done per panel.
- Verification: before/after screenshots of every dock in each dock area and floating, at two window sizes; any automated checks that fit the existing headless self-test style.

## Related, done separately

- Dropped frames in the fps overlay (`fps: 23.9 / 24  dropped: 12`, reset on play) is on `feature/dropped-frames`, outside this milestone.
