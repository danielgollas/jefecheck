# FreeType Text Rendering Integration

## Problem

stb_truetype lacks font hinting — it rasterizes glyphs without snapping stems to pixel boundaries, producing poor coverage on diagonal strokes and thin features. On Retina displays this is visible as jagged diagonals (e.g., `/`, `7`, `k`) and inconsistent stroke weights. FLTK's native widgets look noticeably sharper because they use Core Text (macOS) / FreeType (Linux) for hinting.

## Solution

Replace stb_truetype's rasterizer with FreeType in `bakeAtlas()`. FreeType's TrueType hinting engine snaps glyph stems to pixel boundaries and produces superior coverage values. Everything else in the text rendering pipeline stays unchanged.

## Scope

**Changes:**
- `src/gfcTextRenderer.cpp` — rewrite `bakeAtlas()` internals to use FreeType
- `src/gfcTextRenderer.h` — remove stb_truetype dependency comment (no struct changes)
- `CMakeLists.txt` — add `find_package(Freetype REQUIRED)` and link
- `.github/workflows/build.yml` — add FreeType dev packages to CI

**No changes:**
- `GfcFontAtlas` struct, `GfcBakedGlyph` struct
- `drawLine()`, `emitQuads()`, `draw()`, `draw3D()`
- Two-pass shadow, pixel snapping, GL_NEAREST
- Wrapper functions (`gfc_gl_font`, `gfc_gl_draw`, etc.)
- Font loading (`loadFont`/`loadBoldFont` — already reads TTF into memory)
- All call sites in migrated files

## Architecture

### bakeAtlas() Flow (FreeType)

1. `FT_New_Memory_Face(library, fontData.data(), fontData.size(), 0, &face)` — create face from in-memory TTF data (already loaded by `loadFont()`)
2. `FT_Set_Pixel_Sizes(face, 0, pixelSize)` — set target pixel size
3. For each glyph (ASCII 32-127):
   a. `FT_Load_Char(face, ch, loadFlags)` — load with hinting
   b. `FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)` — render to 8-bit bitmap
   c. Copy `face->glyph->bitmap.buffer` into atlas at current packing position
   d. Extract `GfcBakedGlyph` metrics from `face->glyph->metrics` and `face->glyph->bitmap_left/top`
4. Extract font-level metrics from `face->size->metrics` (ascent, descent, line height)
5. Flip atlas bitmap vertically for GL convention
6. Upload as `GL_ALPHA` 2048x2048 texture with `GL_NEAREST`
7. `FT_Done_Face(face)` — cleanup (library persists for atlas cache rebuilds)

### Hinting Mode

```cpp
FT_Int32 loadFlags = FT_LOAD_RENDER;
if (dpiScale >= 2.0f)
    loadFlags |= FT_LOAD_TARGET_LIGHT;   // Retina: preserve glyph shape
else
    loadFlags |= FT_LOAD_TARGET_NORMAL;  // Standard DPI: snap to pixel grid
```

- **Light hinting** (`dpiScale >= 2`): Only vertical stems hinted. Preserves designer's glyph shape. Smooth diagonals. Matches Core Text behavior on macOS.
- **Full hinting** (`dpiScale < 2`): Both axes hinted aggressively. Maximum sharpness on low-DPI displays. Classic ClearType-like rendering.

### Atlas Packing

Simple row-by-row bin packing (same approach stb uses internally):
- Start at (x=padding, y=padding) in the 2048x2048 bitmap
- For each glyph: if it fits in the current row, place it and advance x. Otherwise, start a new row.
- Padding between glyphs: 2 pixels (prevents texture filtering bleed)
- At 32px pixel size (16pt Retina), 96 ASCII glyphs fit easily in ~4 rows

### FreeType Library Lifecycle

- `FT_Library` initialized once (static local in `bakeAtlas()` or as a class member)
- `FT_Face` created per `bakeAtlas()` call and destroyed after glyph extraction
- Font data stays in `fontData`/`boldFontData` vectors (already in memory) — FreeType reads from these buffers, no extra file I/O

### Metric Extraction

```
GfcBakedGlyph.x0     = glyph->bitmap_left
GfcBakedGlyph.y0     = -(glyph->bitmap_top)              // FreeType Y-up to stb Y-down convention
GfcBakedGlyph.x1     = glyph->bitmap_left + bitmap.width
GfcBakedGlyph.y1     = -(glyph->bitmap_top) + bitmap.rows
GfcBakedGlyph.xadvance = glyph->advance.x / 64.0f        // FreeType uses 26.6 fixed-point
```

Font-level metrics (from `face->size->metrics`, also 26.6 fixed-point):
```
atlas.ascent     = face->size->metrics.ascender / 64.0f
atlas.descent    = face->size->metrics.descender / 64.0f   // negative
atlas.lineHeight = face->size->metrics.height / 64.0f
```

### Texture Format

Unchanged: `GL_ALPHA` 2048x2048, `GL_NEAREST` min/mag. FreeType's `FT_RENDER_MODE_NORMAL` produces 8-bit grayscale bitmaps — same format as stb_truetype's output.

## Build System

### CMakeLists.txt

```cmake
find_package(Freetype REQUIRED)

target_link_libraries(jefecheck PRIVATE
    Freetype::Freetype
    ...existing libs...
)
```

CMake's `FindFreetype` module is built-in — no extra `.cmake` files needed.

### CI Packages

**macOS:** `brew install freetype` (already installed as OIIO dependency, but make explicit)
**Linux:** `sudo apt-get install libfreetype6-dev`
**Windows/MSYS2:** `pacman -S mingw-w64-x86_64-freetype`

## stb_truetype Removal

After FreeType integration:
- Remove `#define STB_TRUETYPE_IMPLEMENTATION` and `#include "stb_truetype.h"` from `gfcTextRenderer.cpp`
- Remove `third_party/stb/` include path from `CMakeLists.txt` (unless stb is used elsewhere)
- Keep `third_party/stb/stb_truetype.h` in the repo as a fallback (or delete if not needed)

## Licensing

FreeType is dual-licensed: **FreeType License** (BSD-like) or **GPL v2**. Use the FreeType License for Apache 2.0 compatibility. Include the FreeType copyright notice in the project's license documentation.

## Success Criteria

1. Text renders with hinted glyphs — vertical stems snap to pixel boundaries
2. Diagonal strokes (`/`, `7`, `k`) are noticeably smoother than stb_truetype
3. All existing text positions, sizes, alignment, and wrapping unchanged
4. Shadow rendering unchanged
5. Builds on macOS, Linux, and Windows CI
6. No new runtime dependencies beyond what OIIO already pulls in (macOS/Linux)
