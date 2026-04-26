# FLTK → Qt Migration

JefeCheck is moving from FLTK 1.4 to Qt 6 for the GUI layer. This doc tracks
where the migration is, what's done, what's next, and how to keep working.

**Long-lived feature branch:** `qt-migration` (off `main`).
**Sub-work:** branched as `qt/<topic>` and PR'd into `qt-migration`. Nothing
goes directly to `main` until the migration ships.

## Why

FLTK is showing its age — limited widget styling, no native dark mode, bitmap
text rendering, poor HiDPI. Qt provides modern widgets, `QOpenGLWidget` for GL
integration, signal/slot, and proper platform native look.

Target aesthetic: dark VFX (Nuke / DaVinci Resolve).

## Phases (full plan)

See `/Users/dgollas/.claude/plans/quizzical-spinning-twilight.md` for the
original detailed plan. Summary:

| Phase | Scope | Estimate | State |
|---|---|---|---|
| 0A | Define abstract UI interfaces in `src/ui/` | done in #16 | ✅ on `main` |
| 0B | Remove `gfcPlate`'s 23 direct FLTK widget pointers | 1 wk | not started |
| 0C | Extract `gfcPlateManagerGUI` abstraction | 3 days | not started |
| 0D | Wrap `GlViewport` behind `IGLViewport` (still FLTK-backed) | 1 wk | not started |
| 1 | Decouple core: split `UICallbacks`, `AppContext`, replace `Fl::check/wait`, drop GLUT | 4-5 wks | not started |
| 2A | CMake `USE_QT` option | done | ⏳ this PR |
| 2B | `QOpenGLWidget`-based `GlViewport_qt` (skeleton) | done | ⏳ this PR |
| 2C | `*_qt` skeletons for the 5 existing GUI abstractions | done | ⏳ this PR |
| 2D | Dark VFX QSS theme | done | ⏳ this PR |
| 2E | Port FLUID windows to Qt (MainWindow, Load, Preferences, FX, etc.) | 4-6 wks | not started |
| 2F | Replace `Fl::run()` with `QApplication::exec()` | 1 wk | not started |
| 3 | Feature parity validation per window | 4-6 wks | not started |
| 4 | Remove FLTK and `*_fltk` files; simplify | 2-3 wks | not started |

## What works today

- **`USE_QT=OFF` (default):** existing FLTK build. Untouched. CI green.
- **`USE_QT=ON`:** CMake configures cleanly, finds Qt6, generates build files.
  Will **not** link yet — `main.cpp`, `UICallbacks.cpp`, FLUID-generated
  windows, `GlViewport`, `gfcPlate` still hardcode FLTK. Phase 0B-0D and
  Phase 1 fix that.

```bash
# FLTK build (current, default)
cmake -B build && cmake --build build -j

# Qt build (configure only, won't link until Phase 1 done)
cmake -B build_qt -DUSE_QT=ON -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
```

## Layout

```
src/ui/                  — abstract backend-agnostic interfaces (Phase 0A, on main)
  IEventSystem.h         replaces 150+ Fl::event_* call sites
  IGLViewport.h          abstract OpenGL surface
  IMainWindow.h          main window surface
  IApplication.h         replaces Fl::check/wait/run, screens, timers
  IFileChooser.h         replaces NativeFileChooser
  IMessageDialog.h       replaces fl_alert / fl_choice / fl_message / fl_input

src/                     — current FLTK code, 1:1 with the abstractions
  gfcplategui.h          abstract base, FLTK-free
  gfcplategui_fltk.cpp   FLTK impl
  gfcplaybackgui.h       abstract base
  gfcplaybackgui_fltk.cpp
  ...

src/qt/                  — Qt skeletons (this PR)
  gfcplategui_qt.h/.cpp        stub _Qt subclasses
  gfcplaybackgui_qt.h/.cpp
  gfcsequencegui_qt.h/.cpp
  gfcnetworkclientgui_qt.h/.cpp
  gfcnetworkservergui_qt.h/.cpp
  GlViewport_qt.h/.cpp         QOpenGLWidget skeleton
  theme/jefecheck_dark.qss     dark VFX theme
```

## How to pick up the work

Each Phase 0 or Phase 1 task should be a sub-branch off `qt-migration`:

```bash
git checkout qt-migration
git pull
git checkout -b qt/0b-gfcplate-widget-pointers
# ...do work...
gh pr create --base qt-migration --title "..."
```

Don't merge sub-branches to `main`. They go to `qt-migration`. When the full
migration is feature-complete and passes parity tests, `qt-migration` itself
is PR'd to `main` as one large change (or fast-forwarded if linear).

## Coding rules during migration

1. **No new FLTK call sites.** New code uses the abstractions in `src/ui/`.
2. **No new Qt call sites in shared code.** Qt calls are confined to
   `src/qt/` (the backend implementation). Application code talks to
   `IXxx` interfaces only.
3. **`*_fltk` and `*_qt` files are pure backend.** They include FLTK/Qt
   headers freely; nothing else does.
4. **`#ifdef JEFECHECK_USE_QT` is acceptable** as a transition tool in
   `main.cpp` and the few files that haven't been abstracted yet — but the
   end-state has zero ifdefs in shared code.

## Open questions

- **Apple Developer ID + notarization:** if we sign + notarize the final Qt
  build, the install UX is dramatically cleaner (no Gatekeeper "damaged"
  errors). Otherwise we keep the ad-hoc-sign + Homebrew-Cask-strips-quarantine
  approach. Decide before Phase 2E ships.
- **FLUID → Qt Designer or programmatic:** ports the 14 `.fl` windows. Qt
  Designer's `.ui` files are easier to edit, programmatic Qt is easier to
  refactor. Lean programmatic.
- **GLUT dependency:** `glutInit` and `glutGet(GLUT_ELAPSED_TIME)` are still
  used for timing. Phase 1D replaces with `std::chrono`. Should be done
  regardless of UI backend.
