# stb_truetype Text Rendering System Design

## Problem

FLTK 1.4's `gl_draw()` uses `glDrawPixels` (fixed pixel size) for text bitmaps but `glRasterPos` (projection-affected) for positioning. In multi-plate layouts (2x1, 1x2, 2x2), the contracted projection matrix causes text to be squashed horizontally or vertically. Multi-line layout is also broken because FLTK assumes standard GL Y convention for line advancement. No combination of projection, alignment, or coordinate system fixes both proportions and line ordering. Additionally, text is blurry on Retina/HiDPI displays because FLTK renders bitmap fonts at logical pixel size.

## Solution

Replace all FLTK `gl_draw`/`gl_font` calls with a custom `GfcTextRenderer` that:
1. Uses stb_truetype to rasterize TrueType glyphs into a texture atlas
2. Bakes drop shadows directly into the atlas
3. Draws text as textured GL_TEXTURE_2D quads
4. Handles word wrapping and alignment in our own code

Since quads go through the same projection as image content, text scales and positions identically — no squashing, no offset, regardless of layout mode.

## Architecture

### GfcTextRenderer (singleton)

**Responsibilities:**
- Load TTF font data from file
- Bake glyph atlas with pre-baked drop shadows
- Cache atlas as a GL texture (rebake on font size or DPI change)
- Draw text strings as textured quads
- Word wrapping and alignment
- Text measurement

**Atlas specifications:**
- Texture size: 1024x1024 (sufficient for ASCII 32-126 at up to 48px with shadow padding)
- Character range: ASCII 32-126 (printable ASCII only — codebase uses no Unicode)
- Two-channel texture (Luminance + Alpha)
- Luminance: 1.0 for glyph foreground, 0.0 for shadow
- Alpha: combined coverage (glyph solid, shadow soft falloff)
- `glColor4f(r, g, b, opacity)` tints the white glyph pixels, shadow stays dark

**Shadow baking:**
- For each glyph, expand bounding box by shadow offset + blur radius (~3px)
- Copy glyph bitmap at offset (1, 1) and apply box blur (3x3 or 5x5)
- Composite: shadow alpha as background layer, glyph alpha on top
- Result: one draw pass per string, consistent shadow quality

**Retina/HiDPI:**
- Query `Fl_Gl_Window::pixels_per_unit()` for display scale factor
- Bake atlas at `pixelSize * dpiScale` (e.g. 24px atlas for 12pt on Retina)
- Draw quads at `pixelSize` in GL units — produces crisp 2x rendering
- Rebake atlas if DPI scale changes (e.g. window moves between displays)

**Font size strategy:**
- Pre-bake atlases for common sizes at startup (10, 12, 14, 18, 24)
- Cache each size's atlas separately (keyed by pixelSize * dpiScale)
- `setFont()` switches to a cached atlas or bakes a new one if not cached
- Max ~6 cached atlases (~12MB total at 1024x1024x2 bytes each)

**GL state safety:**
- The renderer must push/pop all GL state it touches
- Save and restore: `GL_TEXTURE_2D` enable/binding, `GL_BLEND` enable/func, `GL_DEPTH_TEST`, active texture unit
- Use `glPushAttrib(GL_ALL_ATTRIB_BITS)` / `glPopAttrib()` for simplicity

**GL context loss:**
- Atlas texture handles may become invalid if GL context is recreated
- Track a `contextGeneration` counter; if it changes, invalidate all cached atlases
- Check validity at the start of each `draw()` call

**Thread safety:**
- Not thread-safe; all GL calls happen on the main thread (FLTK guarantee)
- Stated as an explicit assumption

### API

```cpp
// Global accessor
GfcTextRenderer& textRenderer();

// Font setup (replaces gl_font)
void setFont(const std::string &fontPath, float pixelSize);
void setDPIScale(float scale);

// Color and shadow
void setColor(float r, float g, float b, float a);
void setShadow(bool enabled, float offsetX = 1, float offsetY = -1, float opacity = 0.5);

// Drawing (replaces gl_draw variants)
void draw(const char *str, float x, float y);
void draw(const char *str, float x, float y, float w, float h, int align);
void draw3D(const char *str, float x, float y, float z); // for 3D-positioned labels (trilerp)

// Measurement (replaces gl_height, fl_measure)
float textWidth(const char *str);
float lineHeight();
void measure(const char *str, int &w, int &h, int wrap_width = 0);
```

### Wrapper Functions (migration helpers)

```cpp
// Drop-in replacements with same signatures as FLTK
void gfc_gl_font(int face, int size);
void gfc_gl_draw(const char *str);
void gfc_gl_draw(const char *str, float x, float y);
void gfc_gl_draw(const char *str, int x, int y, int w, int h, Fl_Align align);
float gfc_gl_height();
void gfc_gl_measure(const char *str, int &w, int &h, int wrap = 0);
```

These call `GfcTextRenderer` internally. Migration is a find-and-replace: `gl_font(` → `gfc_gl_font(`, `gl_draw(` → `gfc_gl_draw(`.

**Supported alignment flags:**
- `FL_ALIGN_TOP`, `FL_ALIGN_BOTTOM`, `FL_ALIGN_LEFT`, `FL_ALIGN_CENTER`
- `FL_ALIGN_WRAP` — enable word wrapping
- `FL_ALIGN_INSIDE` — draw inside bounding box (treat same as default, included for compatibility)

## Font Bundling

- **Font:** DejaVu Sans (regular + bold), Bitstream Vera / public domain license
- **Files:** `common/fonts/DejaVuSans.ttf` (~700KB), `common/fonts/DejaVuSans-Bold.ttf` (~700KB)
- **Loading:** Read from Resources path using `getApplicationDataPath()` + `"fonts/"`, same path resolution as FX files
- **Fallback:** If font file not found, try `common/fonts/` relative to working directory. If still not found, print error and disable text rendering (no crash)

## Word Wrapping and Alignment

Simple implementation matching current usage:
- Split text on `\n` for explicit line breaks
- For each line, measure words and break at `maxWidth`
- `FL_ALIGN_TOP | FL_ALIGN_LEFT` — start from top-left of bounding box
- `FL_ALIGN_BOTTOM` — align text block to bottom of bounding box
- `FL_ALIGN_CENTER` — center horizontally within bounding box
- `FL_ALIGN_WRAP` — enable word wrapping (default on)
- `FL_ALIGN_INSIDE` — treated same as default (for FLTK API compatibility)
- Line height = glyph ascent + descent + 2px leading

## 3D Text (trilerp.cpp)

`trilerp.cpp` uses `glRasterPos3f(x, y, z)` to position labels in 3D LUT cube visualizations. The `draw3D()` method handles this:
- Set `glRasterPos3f(x, y, z)` to establish the raster position (goes through projection)
- Then draw the text quad at that projected screen position
- Implementation: use `gluProject()` to get screen coordinates from the 3D point, then draw the quad at those coordinates

## Call Sites (~61 total, 6 files)

| File | Calls | Usage |
|------|-------|-------|
| `gfcPlate.cpp` | ~12 | Plate labels, remote pointers, AOI coordinates, warnings |
| `gfcnetworkmanager.cpp` | ~10 | Sync status, chat overlay |
| `gfcplatemanager.cpp` | ~10 | Help text, feedback messages, LUT labels |
| `trilerp.cpp` | ~24 | 3D LUT cube labels (uses glRasterPos3f) |
| `gfchistogram.cpp` | ~3 | Histogram axis labels |
| `mtpoly.cpp` | 2 | Area of interest label |

**Migration notes:**
- `trilerp.cpp` uses comma operator inline: `glRasterPos3f(...),gl_draw(tmp)` — needs manual rewriting, not mechanical find-and-replace
- Duplicate `gl_font(FL_TIMES...)` / `gl_font(FL_HELVETICA...)` pairs are dead code — remove the FL_TIMES call during migration
- `gfcplatemanager.cpp` help text draws text twice (dark then light) as a manual shadow — replace with single `gfc_gl_draw` call with shadow enabled
- `gfcplatemanager.cpp:1166` uses `fl_measure()` — replace with `gfc_gl_measure()`
- `gfcnetworkmanager.cpp:509` uses `gl_height()` — replace with `gfc_gl_height()`

## Files

| File | Action |
|------|--------|
| `third_party/stb/stb_truetype.h` | Add (single header, public domain) |
| `common/fonts/DejaVuSans.ttf` | Add (font file) |
| `common/fonts/DejaVuSans-Bold.ttf` | Add (font file) |
| `src/gfcTextRenderer.h` | Create (class declaration, wrapper function declarations) |
| `src/gfcTextRenderer.cpp` | Create (implementation) |
| `src/gfcPlate.cpp` | Modify (replace ~12 gl_draw/gl_font calls) |
| `src/gfcnetworkmanager.cpp` | Modify (replace ~10 calls) |
| `src/gfcplatemanager.cpp` | Modify (replace ~10 calls, remove manual shadow) |
| `src/trilerp.cpp` | Modify (replace ~24 calls, rewrite comma-operator patterns) |
| `src/gfchistogram.cpp` | Modify (replace ~3 calls) |
| `src/mtpoly.cpp` | Modify (replace 2 calls) |
| `src/main.cpp` | Modify (initialize renderer, remove gl_texture_pile_height) |
| `CMakeLists.txt` | Modify (add stb include path) |

## Implementation Phases

### Phase 1: Build the renderer
- Add `stb_truetype.h` to `third_party/stb/`
- Bundle DejaVu Sans fonts
- Create `src/gfcTextRenderer.cpp/h` — atlas baking, shadow baking, quad drawing, wrapping
- Test with a single `drawText()` call in gfcPlate

### Phase 2: Create wrapper functions
- Implement `gfc_gl_font`, `gfc_gl_draw`, `gfc_gl_height`, `gfc_gl_measure`
- Match FLTK signatures for minimal call site changes

### Phase 3: Migrate call sites (one file per commit)
1. `gfcPlate.cpp` — most critical (plate labels in multi-plate layouts)
2. `gfcplatemanager.cpp` — help overlay, feedback, LUT labels
3. `gfcnetworkmanager.cpp` — sync/chat text
4. `gfchistogram.cpp` — axis labels
5. `mtpoly.cpp` — AOI label
6. `trilerp.cpp` — 3D LUT labels (manual rewrite, most complex)

### Phase 4: Cleanup
- Remove `gl_texture_pile_height()` from main.cpp
- Remove dead `gl_font(FL_TIMES...)` calls
- Verify all layouts, test resize, test Retina

## Success Criteria

1. Text renders correctly (not squashed) in 1x1, 2x1, 1x2, and 2x2 plate layouts
2. Text is crisp on Retina/HiDPI displays
3. Drop shadow visible on all backgrounds (light and dark images)
4. Text opacity/fade works (textDisplayOpacity)
5. No crashes on window resize or layout mode change
6. Builds and passes CI on macOS, Linux, and Windows
7. All ~61 call sites migrated with no regressions in text positioning
8. 3D LUT labels in trilerp still render at correct 3D positions
