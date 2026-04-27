# Qt UI Layout Design

This document captures the layout and widget-translation decisions for the FLTK→Qt port. Reference for every window port in Phase 2E.

## Goals

- **Look and feel**: VFX-industry idiom (Nuke / DaVinci Resolve / After Effects) — dark theme, dockable panels, persistent layout.
- **Translate the FLTK layout in spirit, not pixel-perfectly.** Use Qt layouts (`QFormLayout`, `QHBoxLayout`, `FlowLayout`, etc.) so the UI flows on resize and HiDPI instead of FLUID's absolute positioning.
- **Everything is a panel that can be moved, torn off, kept, or closed.** Standard `QDockWidget` plumbing.

## Window architecture: single shell + dockable panels

```
┌─────────────────────────────────────────────────────────────┐
│  Menu bar  (QMenuBar — native macOS global menu by default) │
├─────────────────────────────────────────────┬───────────────┤
│                                             │  FX Stack /   │
│       Central widget:                       │  LUTs         │
│       GlViewport_Qt                         │  (right dock, │
│                                             │   stacked     │
│                                             │   tabs)       │
├──────────────────────┬──────────────────────┴───────────────┤
│   Plate Manager      │   Timeline + Transport               │
│   (flow of cards)    │   (transport row + multitrack)       │
└──────────────────────┴──────────────────────────────────────┘
```

- **Central widget**: `GlViewport_Qt` (`QOpenGLWidget` subclass). The image area. Always visible.
- **Bottom dock area**: split horizontally via `QMainWindow::splitDockWidget(plateDock, timelineDock, Qt::Horizontal)`. Plate Manager on the left (squarish by default), Timeline + Transport on the right (gets the rest of the width).
- **Right dock area**: FX Stack and LUTs as stacked tabs (drop one onto the other → automatic Qt tab bar).
- **Menu bar**: `QMenuBar` set on the `QMainWindow`. macOS pulls it into the system menu bar automatically; Linux/Windows show it in-window.

### Dock behavior (free with `QDockWidget`)

- Drag titlebar to redock anywhere (top/left/right/bottom).
- Drag outside the main window to float into a separate OS-level window (lives on a second monitor cleanly).
- Stack dock onto another dock to get tabbed panels.
- Close from titlebar; restore via View menu.
- Default sizes: Plate ~280–520px wide × ~360px tall, Timeline ~the rest of the bottom × ~360px tall, FX/LUT ~340px wide.
- `QMainWindow::saveState()` / `restoreState()` persists the user's arrangement to `QSettings` so it survives restart.

## Plate Manager: card flow

The Plate Manager dock contains a `QScrollArea` → inner `QWidget` with a **FlowLayout** (canonical Qt example, ~50 lines). Each plate is a self-contained card (`PlateCard_Qt : QFrame` with fixed minimum size, ~260×180px) holding the per-plate controls: track selector, aspect, zoom/pan, gamma, contrast, brightness, crop, FX-active indicator.

`FlowLayout` reflows by parent width:

| Dock shape          | Layout                       |
| ------------------- | ---------------------------- |
| Default (squarish)  | 2×2 grid, mirrors viewport   |
| Pulled to left edge | Single column, vertical flow |
| Stretched ultrawide | 4 cards in one row           |
| Future: 5+ tracks   | Grid grows / scrollbar       |

Cards are not individually tear-off-able — when the user pulls the Plate Manager out, all cards travel together as one floating window. Decision recorded 2026-04-26 (Daniel: "no need for individual ones").

## Timeline + Transport: integrated panel

Transport controls live inside the timeline panel, not as a separate top toolbar. Matches Resolve / Premiere / Nuke / After Effects.

```
┌─────────────────────────────────────────────────────────┐
│ ◀◀ ◀ ⏸ ▶ ▶▶  Once▾  │ Frame: 42  In: 1  Out: 120  FPS: 24│
├─────────────────────────────────────────────────────────┤
│ In ●──────────────[playhead]─────────────● Out          │
│ Track A  ════════════════════●════════                  │
│ Track B  ═════════●═══════════                          │
│ Track C  ═════════════════════════════                  │
│ Track D  ═════════════════════════════                  │
└─────────────────────────────────────────────────────────┘
```

- Implemented as a custom `TimelinePanel_Qt : QWidget` (rewrite, not a port of `TrackWidget`). Top row is a `QToolBar`-styled `QWidget` with the transport buttons + frame/FPS labels. Below it, a custom-painted multitrack widget (`paintEvent`).
- Transport row uses standard Qt widgets (`QPushButton` icons, `QComboBox` for loop mode, `QSpinBox` for frame, `QLabel` for FPS readouts).
- When the user tears the timeline dock out into a floating window, transport travels with it.

## Floating dialogs (stay as `QDialog`)

These don't need to be persistent dock panels; modal dialogs make the interaction model clearer:

- Preferences
- Render
- Load (use `QFileDialog::getOpenFileName` instead of a custom window where possible)
- About / Min Specs
- Remote / Network (could go either way; start as dialog, promote to dock if usage warrants)

## Widget translation table

| FLTK                             | Qt equivalent                                          | Notes                                                            |
| -------------------------------- | ------------------------------------------------------ | ---------------------------------------------------------------- |
| `Fl_Window` (modeless)           | `QWidget` inside `QDockWidget` *or* `QMainWindow`      | depends on whether it's a panel or shell                         |
| `Fl_Window` (modal)              | `QDialog::exec()`                                      |                                                                  |
| `Fl_Tabs`                        | `QTabWidget`                                           |                                                                  |
| `Fl_Hold_Browser` sidebar nav    | `QListWidget` + `QStackedWidget`                       | Resolve/Nuke style                                               |
| `Fl_Group` w/ `BORDER_FRAME`     | `QGroupBox` w/ title                                   |                                                                  |
| `Fl_Check_Button`                | `QCheckBox`                                            |                                                                  |
| `Fl_Round_Button` (radio)        | `QRadioButton` in a `QButtonGroup`                     |                                                                  |
| `Fl_Value_Slider` (Horizontal)   | `QSlider` + `QDoubleSpinBox` paired in `QHBoxLayout`   | drag *or* type                                                   |
| `Fl_Value_Input` / spinner       | `QSpinBox` / `QDoubleSpinBox`                          |                                                                  |
| `Fl_Input`                       | `QLineEdit`                                            |                                                                  |
| `Fl_Output` (read-only text)     | `QLabel`                                               |                                                                  |
| `Fl_Choice`                      | `QComboBox`                                            |                                                                  |
| `Fl_Menu_Bar`                    | `QMenuBar`                                             | use as `QMainWindow::setMenuBar()`                               |
| `Fl_Menu_Button`                 | `QPushButton` + `QMenu` *or* `QToolButton`             |                                                                  |
| `Fl_Pack` (vertical/horizontal)  | `QVBoxLayout` / `QHBoxLayout`                          |                                                                  |
| `Fl_Scroll`                      | `QScrollArea`                                          |                                                                  |
| `Fl_Color` swatch                | `QPushButton` w/ stylesheet → `QColorDialog::getColor` |                                                                  |
| Path field + "browse" button     | `QLineEdit` + `QPushButton` → `QFileDialog`            | replaces `NativeFileChooser`                                     |
| `Fl_Progress`                    | `QProgressBar`                                         |                                                                  |
| `Fl_File_Chooser`                | `QFileDialog`                                          | covers both single-file and directory selection                  |
| `fl_alert` / `fl_choice`         | `QMessageBox`                                          |                                                                  |
| `Fl_Tile`                        | `QSplitter`                                            |                                                                  |

### Custom widgets needing real ports

| FLTK                       | Qt port                                            | Notes                                                  |
| -------------------------- | -------------------------------------------------- | ------------------------------------------------------ |
| `TrackWidget` (timeline)   | `TimelinePanel_Qt` (rewrite)                       | merges in transport row; multitrack `paintEvent`       |
| `Fl_Choice_gfc`            | `QComboBox` with stylesheet                        | dark-theme variant                                     |
| `Fl_Button_gfc`            | `QPushButton` with stylesheet                      | dark-theme variant                                     |
| `Fl_Input_Choice_gfc`      | `QComboBox` (editable)                             |                                                        |
| `Fl_DragBar`               | `QDockWidget` titlebar                             | already supports drag                                  |
| `gfcMoreOptionsPopup`      | `QMenu`                                            | context menu off the track-bar                         |

## Layout strategy inside windows

Every section in a settings/dialog window:

1. Wrap in a `QGroupBox` for a titled frame.
2. Use **`QFormLayout`** for label-on-left, control-on-right (Qt's idiomatic settings layout, auto-aligned).
3. Stack groupboxes vertically inside a `QVBoxLayout`.
4. If sections overflow tab height, wrap the whole thing in `QScrollArea`.

No fixed pixel positions. Let the layout do the work.

## Open items

- **Plate controls model**: current Qt design shows all 4 plate cards in a 1- or 2-column grid (modern DAW/compositor idiom). The original FLTK design had a single "active panel" with a 1/2/3/4 selector. Keep the multi-card view as the docked default; consider adding a compact mode (single card + selector strip) when the user only uses one plate at a time.
- **Viewport layout selector** (1×1 / 2×1 / 1×2 / 2×2): chooses how the central GL viewport subdivides into plate quadrants. Not yet placed in the Qt shell — likely wants to live as a 4-icon strip at the top of the Plate Manager dock, or as a `QToolBar` action on the main window.
- **Theme**: Dark VFX QSS lives at `src/qt/theme/jefecheck_dark.qss`. Loaded from disk by `main_qt.cpp`. To be refined as widgets get ported.
- **Icon set for transport buttons**: TBD. Either Unicode glyphs (▶ ⏸ ◀◀ etc.) for v1 or proper SVGs later.
- **Per-track-card color/badge** to reflect track ID (A/B/C/D currently): defer until cards are real.
