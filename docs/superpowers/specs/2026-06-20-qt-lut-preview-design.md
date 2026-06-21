# Qt LUT Preview — Design Spec

**Status:** Approved (2026-06-20); **extended to v2 (LUT inspector) 2026-06-20** — see "## v2 — LUT inspector" at the bottom.
**Branch target:** `qt-experimental`

## Goal

Add an in-window visual preview of a loaded LUT to the Qt LUT panel: a 2D curve for 1D LUTs, a slowly-spinning 3D point cloud for 3D LUTs, plus an info subpanel (type, size, bit depth, etc.) and a show/hide toggle. A gut-check aid to confirm a LUT looks sane — not a hero feature. **(v2 turns the 3D side into an interactive cube inspector — see the v2 section.)**

## Background / current state

- **Qt LUT panel:** `LUTPanel_Qt` (`src/qt/FXLutPanel_qt.{h,cpp}`) — a `QListWidget` of LUT names (item 0 = "(No LUT)", 1+ = loaded LUTs), an "Apply to active plate" button, a "Refresh" button, a status label, and drag-drop load. Selection/apply route through `jefe::qt::*` bridge accessors.
- **LUT data model:** `CubeLUT` (`src/trilerp.h`). Public members: `int type` (enum `LUTTYPES { BASELIGHT3DCUBE=0, JEFECHECK1D=1, IMAGELUT2D=2 }`), `int size`, `float lut1D[1024]`, `std::vector<std::vector<std::vector<Vec3D>>> cube`, `float maximum1DValue`, `int fromBits`, `int toBits`, `char filename[250]`, `getNameNoPath()`. `Vec3D` holds the mapped RGB at a cube point. A LUT is **either** 1D (`JEFECHECK1D`) **or** 3D (`BASELIGHT3DCUBE` / `IMAGELUT2D`, both use `cube`).
- **LUT manager:** `gfcLUTManager` (`src/gfclutmanager.{h,cpp}`) stores `std::vector<CubeLUT> lutArray`; `getLUT(int)`, `getAllNames()`, name→index lookups. No "current" selection — callers pass an index. The Qt list's row `r` maps to `plate.LUT = r - 1` (row 0 = no LUT).
- **Existing LUT bridge** (`src/qt/SequenceLoadBridge_qt.h`): `getLutNames()`, `loadLUTFile()`, `applyLUTToActivePlate(int)`, `applyLUTToPlate(int,int)`, `getLUTOnActivePlate()`, `autoloadLUTs()`.
- **FLTK fullscreen preview (NOT used):** `gfcPlateManager::showLutPreview` + `setDrawLUTPreview()` + `CubeLUT::draw()/drawSkewed()` render the LUT *over the whole viewport*. This spec does **not** revive that path — it stays untouched and unused. The new preview is confined to the LUT panel.
- **TU separation (developer_notes §1):** Qt UI `.cpp` files must not include `trilerp.h`/`gfclutmanager.h` (they pull glad, which conflicts with Qt's `QOpenGLWidget` on macOS). All LUT data reaches the widget through `jefe::qt::*` accessors returning Qt/STL types. A panel `QOpenGLWidget` must use Qt's `QOpenGLFunctions` (its own context), never glad.

## Decisions (from brainstorming)

1. **Render tech is split by LUT type** (a LUT is only ever one type): 1D → QPainter 2D curve; 3D → `QOpenGLWidget` point cloud. Swapped via a `QStackedWidget`.
2. **3D cube: slow auto-spin** (runs only while visible), **drag to nudge** rotation.
3. **In-window only** — no viewport takeover.
4. **Toggle, shown by default** — a "Preview" checkbox collapses/expands the preview area to reclaim list space.
5. **Info subpanel** with: Name, Type, Size, bit depth, max value.

## Architecture

### Bridge surface (new `jefe::qt::*` in `SequenceLoadBridge_qt.{h,cpp}`)

```cpp
// Qt-safe snapshot of a LUT for the panel preview. The bridge .cpp
// (which already includes trilerp.h) copies the sample data out so the
// widget never touches CubeLUT/glad. guiLutIndex is the LUT-panel row:
// 0 = "(No LUT)" → invalid; 1+ map to lutManager indices (row-1... see
// getLutNames ordering, applied consistently with applyLUTToActivePlate).
struct LutPreviewData {
    bool        valid     = false;
    int         type      = 0;     // CubeLUT::LUTTYPES
    bool        is3D      = false; // type != JEFECHECK1D
    int         size      = 0;     // samples (1D) or cube edge (3D)
    int         fromBits  = 0;
    int         toBits    = 0;
    float       max1D     = 1.0f;
    std::string name;
    // 1D: `size` output samples in [0, max1D] (caller normalizes by max1D).
    std::vector<float> curve1D;
    // 3D: flat [x,y,z, r,g,b] per sampled point. Positions are the
    // normalized grid coords in [0,1]; colors are the mapped Vec3D
    // (clamped to [0,1] for display). Subsampled so point count is capped.
    std::vector<float> points3D;
    int                point3DCount = 0;  // number of points (points3D.size()/6)
};

LutPreviewData getLutPreview(int guiLutIndex);
```

- Implementation: `if (guiLutIndex <= 0) return {invalid};`. Otherwise `CubeLUT lut = lutManager.getLUT(guiLutIndex - 1);` then fill the struct from `lut.type/size/lut1D/cube/maximum1DValue/fromBits/toBits/getNameNoPath()`.
- **1D:** copy `lut1D[0 .. size-1]` into `curve1D`; set `max1D = maximum1DValue` (guard `>0`).
- **3D:** iterate `cube[x][y][z]` over a stride so the emitted point count ≤ `kMaxPreviewPoints = 20000`. For each kept point push `x/(size-1), y/(size-1), z/(size-1)` and the clamped `cube[x][y][z]` RGB. Stride = `ceil(size / cbrt(kMaxPreviewPoints))` per axis (e.g. 64³ → stride 3 → ~10k pts).
- `getLUT` returns a **copy** (`CubeLUT` by value) — fine for a one-shot snapshot on selection; not called per-frame.

### Preview widget (`src/qt/LUTPreview_qt.{h,cpp}`, new)

A `LUTPreview_Qt : QWidget` containing:
- An info subpanel: a small grid of `QLabel`s (Name, Type, Size, Depth, Max).
- A `QStackedWidget` with two pages:
  - **`LutCurveWidget : QWidget`** — `paintEvent` draws axes + grid + the `curve1D` polyline (input x ∈ [0,size), output y ∈ [0, max1D] normalized to the widget). Neutral curve color on the dark theme.
  - **`LutCloudWidget : QOpenGLWidget, protected QOpenGLFunctions`** — `initializeGL` calls `initializeOpenGLFunctions()`; holds the current `points3D` (uploaded to a VBO or drawn from client memory via legacy `glBegin`/`GL_POINTS` — match whatever GL profile the panel context provides; a compatibility `GL_POINTS` draw is acceptable since this is a tiny throwaway cloud). `paintGL` sets a perspective + rotating modelview (yaw from an angle member), depth test on, draws the points colored per-vertex. A `QTimer` (~30fps) advances the angle and calls `update()` **only while the widget is visible**; `showEvent`/`hideEvent` start/stop it. `mousePressEvent`/`mouseMoveEvent` nudge yaw/pitch on drag.
- `void setLut(const jefe::qt::LutPreviewData& d)`: populates the info labels, picks the stacked page by `d.is3D`, and hands the data to the active page. Empty/invalid → show a "no preview" placeholder page (or clear).

### LUT panel integration (`FXLutPanel_qt.{h,cpp}`)

- Add a `QCheckBox* previewToggle_` ("Preview", checked by default) and a `LUTPreview_Qt* preview_` below the existing list/buttons.
- `previewToggle_` toggled → `preview_->setVisible(checked)`.
- On list `currentRowChanged` (and after `refreshList`), call `preview_->setLut(jefe::qt::getLutPreview(row))` when the toggle is on.
- Object names for tests: `lut.preview.toggle`, `lut.preview` (container), `lut.preview.cloud`, `lut.preview.curve`.

### Data flow

`list selection / refresh` → `getLutPreview(row)` (bridge copies sample data) → `LUTPreview_Qt::setLut` → info labels + the type-appropriate canvas. Toggle controls visibility; the cloud's spin timer only runs while visible.

## Out of scope (v1)

- Editing LUTs; before/after image preview; exporting the graph image.
- Reviving the FLTK fullscreen `showLutPreview` viewport mode.
- Per-plate "what LUT is applied" preview beyond the selected list row.

## Files

- **Create:** `src/qt/LUTPreview_qt.{h,cpp}` — `LUTPreview_Qt` + `LutCurveWidget` (QPainter) + `LutCloudWidget` (QOpenGLWidget/QOpenGLFunctions).
- **Modify:** `src/qt/SequenceLoadBridge_qt.{h,cpp}` — `LutPreviewData` + `getLutPreview`.
- **Modify:** `src/qt/FXLutPanel_qt.{h,cpp}` — toggle + preview pane + selection wiring.
- **Modify:** `tests/ui/jefecheck/locators.py` + a test file under `tests/ui/`.
- **Build:** add `LUTPreview_qt.cpp` to the Qt sources in `CMakeLists.txt`.

## Testing

- **Appium smoke:** the preview container and toggle resolve by object name; toggling doesn't crash. (Per-pixel canvas content isn't AX-addressable.)
- **Manual:** load a 1D LUT → curve renders with axes; load a 3D LUT (.cube/.cub) → point cloud auto-spins, drag nudges it, colors look right; info labels show correct type/size/depth; toggle hides/shows the pane; selecting "(No LUT)" clears the preview.

---

## v2 — LUT inspector (extension, 2026-06-20)

The 3D side grows from an auto-spinning point cloud into an interactive cube inspector. The 1D curve, info subpanel, and toggle are unchanged. v1 (above) is already implemented and committed on the branch; v2 builds on it in the same PR.

### Resolved decisions
- **Deformed placement = output color** (position driven by the LUT's output; identity = undeformed cube). Delta-mode (`out − in`) is a possible later toggle, not in v2.
- **Dots marker = a temporary dropdown** of dot / mini-cube / tetrahedron (we'll settle on one later).
- **Resizable preview = a QSplitter** between the LUT list and the preview pane. A detach-to-window button is a stretch goal only if trivial.
- **Auto-spin** becomes an optional checkbox (default off) now that a real camera drives interaction.
- **Default render mode:** Dots on, Solid/Grid off.

### Data-model change (bridge)
Solid faces and the Grid lattice need the **structured cube grid with adjacency**, not v1's flat subsampled point list. Replace `points3D`/`point3DCount` in `LutPreviewData` with:
```cpp
    int                cubeSize = 0;   // working cube edge after any subsample
    std::vector<float> cubeRGB;        // cubeSize^3 * 3 floats: output color per
                                       // node, x-major: idx((x*cubeSize+y)*cubeSize+z)*3
```
- The bridge subsamples to a working edge `cubeSize ≤ kMaxCubeEdge = 33` (so faces ≤ ~6·33²·2 tris, lattice ≤ ~3·33²·32 lines) for large LUTs; logs when it caps.
- Node position: **uniform** → `(x,y,z)/(cubeSize-1)`; **deformed** → that node's `cubeRGB` (clamped to [0,1]).

### 3D widget (`LutCloudWidget`) — rework
- **Maya-style camera** replacing the auto-spin/simple-drag: orbit (Alt+LMB), pan (Alt+MMB), dolly (Alt+RMB and mouse wheel), focus/frame the cube (F key → reset target+distance to fit). Camera state = target (Vec3-ish), yaw, pitch, distance; `paintGL` builds the modelview from it. Auto-spin checkbox advances yaw when enabled and idle.
- **Three composable render modes** (independent checkboxes), all reading the same `cubeRGB` at the current placement:
  - **Solid** — the 6 outer boundary faces as Gouraud-shaded quads (per-vertex output color), depth-tested, no interior fill.
  - **Grid** — lines between each pair of adjacent nodes along ±x/±y/±z; per-vertex color so GL interpolates along each segment.
  - **Dots** — a marker per node colored by its output: `GL_POINTS`, a small cube, or a small tetrahedron per the marker dropdown.
- **Background-brightness slider** (0..1 gray) sets `glClearColor`; the axis-end labels recolor (dark text on bright bg, light on dark) to stay readable.
- **Axis labels** — small "R"/"G"/"B" text at the +X/+Y/+Z axis ends, drawn with a `QPainter` overlay (`QOpenGLWidget` supports painting after `beginNativePainting`/`endNativePainting`), projected from the 3D axis endpoints.
- **Uniform vs deformed** checkbox selects the node placement (see data-model).

### Panel controls (under the existing Preview toggle, shown only when a 3D LUT is selected)
- A QSplitter makes the preview pane resizable.
- A small controls strip: render-mode checkboxes (Solid / Grid / Dots), the Dots marker dropdown, the uniform/deformed checkbox, the auto-spin checkbox, and the background-brightness slider. Object names: `lut.preview.mode.solid/grid/dots`, `lut.preview.marker`, `lut.preview.deformed`, `lut.preview.autospin`, `lut.preview.bg`.

### Out of scope (v2)
- Delta deform mode; per-node picking/selection (F just frames the whole cube); saving inspector state across sessions; the 1D side gaining a camera.
