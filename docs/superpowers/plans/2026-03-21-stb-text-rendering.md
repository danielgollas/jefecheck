# stb_truetype Text Rendering Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace FLTK's gl_draw/gl_font with a custom stb_truetype-based text renderer that draws textured quads, fixing text squashing in multi-plate layouts and adding Retina support with drop shadows.

**Architecture:** A singleton `GfcTextRenderer` loads TTF fonts via stb_truetype, bakes glyph+shadow texture atlases, and draws text as `GL_TEXTURE_2D` quads. Wrapper functions (`gfc_gl_font`, `gfc_gl_draw`) match FLTK's signatures for minimal call site changes. Text quads go through the same projection as image content, eliminating the squashing problem.

**Tech Stack:** stb_truetype (single header, public domain), DejaVu Sans TTF fonts, OpenGL 2.1+ compatibility profile

**Spec:** `docs/superpowers/specs/2026-03-21-stb-text-rendering-design.md`

---

## Chunk 1: Core Renderer (Tasks 1-3)

### Task 1: Add stb_truetype and font files

**Files:**
- Create: `third_party/stb/stb_truetype.h`
- Create: `common/fonts/DejaVuSans.ttf`
- Create: `common/fonts/DejaVuSans-Bold.ttf`
- Modify: `CMakeLists.txt`
- Modify: `.gitignore`

- [ ] **Step 1: Download stb_truetype.h**

```bash
mkdir -p third_party/stb && curl -sL https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h -o third_party/stb/stb_truetype.h
```

- [ ] **Step 2: Download DejaVu Sans fonts**

```bash
mkdir -p common/fonts && curl -sL https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2 | tar -xjf - --strip-components=2 -C common/fonts dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf dejavu-fonts-ttf-2.37/ttf/DejaVuSans-Bold.ttf
```

- [ ] **Step 3: Update .gitignore to allow .ttf files**

Add `!*.ttf` to `.gitignore` (currently `*.ttf` is not blocked but font files need to be tracked).

- [ ] **Step 4: Add stb include path to CMakeLists.txt**

Add to the `target_include_directories` block:
```cmake
target_include_directories(jefecheck PRIVATE
    src/
    third_party/stb/
)
```

- [ ] **Step 5: Symlink fonts to Resources**

```bash
ln -sf $(pwd)/common/fonts Resources/fonts
```

- [ ] **Step 6: Commit**

```bash
git add third_party/stb/stb_truetype.h common/fonts/ CMakeLists.txt .gitignore && git commit -m "Add stb_truetype and DejaVu Sans fonts for custom text rendering"
```

---

### Task 2: Create GfcTextRenderer — atlas baking and basic drawing

**Files:**
- Create: `src/gfcTextRenderer.h`
- Create: `src/gfcTextRenderer.cpp`

- [ ] **Step 1: Create gfcTextRenderer.h**

```cpp
#ifndef GFCTEXTRENDERER_H
#define GFCTEXTRENDERER_H

#include <glad/glad.h>
#include <string>
#include <map>
#include <vector>

// Alignment flags (matching FLTK for compatibility)
#define GFC_ALIGN_LEFT    0
#define GFC_ALIGN_CENTER  1
#define GFC_ALIGN_TOP     0
#define GFC_ALIGN_BOTTOM  2
#define GFC_ALIGN_WRAP    4
#define GFC_ALIGN_INSIDE  8

struct GfcBakedGlyph {
    float x0, y0, x1, y1;   // quad position offset from cursor (in pixels)
    float u0, v0, u1, v1;   // texture coordinates
    float xadvance;          // horizontal advance
};

struct GfcFontAtlas {
    GLuint textureID;
    int texWidth, texHeight;
    float pixelSize;
    float ascent, descent, lineGap;
    float lineHeight;        // ascent - descent + lineGap
    GfcBakedGlyph glyphs[96]; // ASCII 32-127
    bool valid;
};

class GfcTextRenderer {
public:
    GfcTextRenderer();
    ~GfcTextRenderer();

    // Initialize with font file path
    bool loadFont(const std::string &fontPath);
    bool loadBoldFont(const std::string &fontPath);

    // Font setup
    void setSize(float pixelSize);
    void setBold(bool bold);
    void setDPIScale(float scale);

    // Color
    void setColor(float r, float g, float b, float a);

    // Shadow (baked into atlas, just controls whether to use shadow atlas variant)
    void setShadowEnabled(bool enabled);

    // Drawing
    void draw(const char *str, float x, float y);
    void draw(const char *str, float x, float y, float w, float h, int align);
    void draw3D(const char *str, float x, float y, float z);

    // Measurement
    float textWidth(const char *str);
    float lineHeight();
    void measure(const char *str, int &w, int &h, int wrapWidth = 0);

private:
    // Font data (kept in memory for rebaking)
    std::vector<unsigned char> fontData;
    std::vector<unsigned char> boldFontData;
    bool fontLoaded, boldFontLoaded;

    // Atlas cache keyed by (pixelSize * dpiScale * 100)
    std::map<int, GfcFontAtlas> atlasCache;
    std::map<int, GfcFontAtlas> boldAtlasCache;

    // Current state
    float currentSize;
    float dpiScale;
    bool currentBold;
    float colorR, colorG, colorB, colorA;
    bool shadowEnabled;

    // Get or create atlas for current size/DPI
    GfcFontAtlas& getAtlas();

    // Bake a new atlas
    GfcFontAtlas bakeAtlas(const std::vector<unsigned char> &fontData, float pixelSize);

    // Bake drop shadow into atlas
    void bakeShadow(unsigned char *bitmap, int w, int h, int shadowOffX, int shadowOffY, int blurRadius);

    // Word wrapping
    struct TextLine {
        const char *start;
        int length;
        float width;
    };
    std::vector<TextLine> wrapText(const char *str, float maxWidth);

    // Draw a single line of text as quads
    void drawLine(const char *str, int len, float x, float y);
};

// Global singleton
GfcTextRenderer& textRenderer();

// FLTK-compatible wrapper functions
void gfc_gl_font(int face, int size);
void gfc_gl_draw(const char *str);
void gfc_gl_draw(const char *str, float x, float y);
void gfc_gl_draw(const char *str, int x, int y, int w, int h, int align);
float gfc_gl_height();
void gfc_gl_measure(const char *str, int &w, int &h, int wrap = 0);

#endif
```

- [ ] **Step 2: Create gfcTextRenderer.cpp — font loading and atlas baking**

Implement:
- `loadFont()` / `loadBoldFont()` — read TTF file into memory buffer
- `bakeAtlas()` — use `stbtt_BakeFontBitmap()` to rasterize ASCII 32-127 into a 1024x1024 bitmap, then `bakeShadow()` to add drop shadow, upload as `GL_LUMINANCE_ALPHA` texture
- `bakeShadow()` — for each pixel with alpha > 0 in the glyph bitmap, write a blurred version offset by (1,-1) into the shadow channel
- `getAtlas()` — look up cached atlas by size key, bake new if not found, validate with `glIsTexture()`
- Font metrics: use `stbtt_GetFontVMetrics()` for ascent/descent/lineGap

Key implementation note for atlas baking:
```cpp
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// In bakeAtlas():
stbtt_bakedchar cdata[96];
unsigned char *bitmap = new unsigned char[1024 * 1024];
stbtt_BakeFontBitmap(fontData.data(), 0, pixelSize, bitmap, 1024, 1024, 32, 96, cdata);
// Convert stbtt_bakedchar to GfcBakedGlyph, applying shadow expansion
// Create GL_LUMINANCE_ALPHA texture from bitmap + shadow
```

- [ ] **Step 3: Implement drawLine() — render text as textured quads**

For each character in the string:
1. Look up the `GfcBakedGlyph` for the character
2. Calculate quad vertices: `(x + glyph.x0/dpiScale, y + glyph.y0/dpiScale)` to `(x + glyph.x1/dpiScale, y + glyph.y1/dpiScale)`
3. Set texture coordinates from `(glyph.u0, glyph.v0)` to `(glyph.u1, glyph.v1)`
4. Advance cursor by `glyph.xadvance / dpiScale`

GL state management:
```cpp
glPushAttrib(GL_ALL_ATTRIB_BITS);
glEnable(GL_TEXTURE_2D);
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
glDisable(GL_TEXTURE_RECTANGLE_ARB);
glDisable(GL_DEPTH_TEST);
glBindTexture(GL_TEXTURE_2D, atlas.textureID);
glColor4f(colorR, colorG, colorB, colorA);
glBegin(GL_QUADS);
// ... per-character quads ...
glEnd();
glPopAttrib();
```

- [ ] **Step 4: Implement draw(str, x, y, w, h, align) with word wrapping**

`wrapText()`:
- Split on `\n` for explicit breaks
- For each line, accumulate word widths using `textWidth()`
- When accumulated width exceeds `maxWidth`, start new line
- Return vector of `TextLine` structs

`draw(str, x, y, w, h, align)`:
- Call `wrapText(str, w)`
- Calculate starting Y based on alignment (top = y, bottom = y + h - totalHeight)
- Calculate X per line based on alignment (left = x, center = x + (w - lineWidth)/2)
- Call `drawLine()` for each line

- [ ] **Step 5: Implement draw3D(str, x, y, z)**

```cpp
void GfcTextRenderer::draw3D(const char *str, float x, float y, float z) {
    // Project 3D point to 2D screen coordinates
    GLdouble modelview[16], projection[16];
    GLint viewport[4];
    GLdouble sx, sy, sz;
    glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
    glGetDoublev(GL_PROJECTION_MATRIX, projection);
    glGetIntegerv(GL_VIEWPORT, viewport);
    gluProject(x, y, z, modelview, projection, viewport, &sx, &sy, &sz);

    // Save projection, set up pixel-exact projection for this viewport
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(viewport[0], viewport[0]+viewport[2], viewport[1], viewport[1]+viewport[3], -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    drawLine(str, strlen(str), (float)sx, (float)sy);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}
```

- [ ] **Step 6: Implement measurement functions**

```cpp
float GfcTextRenderer::textWidth(const char *str) {
    GfcFontAtlas &atlas = getAtlas();
    float w = 0;
    while (*str) {
        if (*str >= 32 && *str < 128)
            w += atlas.glyphs[*str - 32].xadvance / dpiScale;
        str++;
    }
    return w;
}

float GfcTextRenderer::lineHeight() {
    return getAtlas().lineHeight / dpiScale;
}

void GfcTextRenderer::measure(const char *str, int &w, int &h, int wrapWidth) {
    if (wrapWidth > 0) {
        auto lines = wrapText(str, (float)wrapWidth);
        float maxW = 0;
        for (auto &l : lines) maxW = std::max(maxW, l.width);
        w = (int)maxW;
        h = (int)(lines.size() * lineHeight());
    } else {
        w = (int)textWidth(str);
        h = (int)lineHeight();
    }
}
```

- [ ] **Step 7: Commit**

```bash
git add src/gfcTextRenderer.h src/gfcTextRenderer.cpp && git commit -m "feat: create GfcTextRenderer with stb_truetype atlas and quad drawing"
```

---

### Task 3: Implement FLTK-compatible wrapper functions

**Files:**
- Modify: `src/gfcTextRenderer.cpp` (add wrapper implementations)
- Modify: `src/main.cpp` (initialize renderer at startup)

- [ ] **Step 1: Implement wrapper functions**

```cpp
static bool s_currentBold = false;
static int s_currentSize = 12;

void gfc_gl_font(int face, int size) {
    // FL_HELVETICA=0, FL_BOLD=1, FL_ITALIC=2
    s_currentBold = (face & 1) != 0; // FL_BOLD flag
    s_currentSize = size;
    textRenderer().setBold(s_currentBold);
    textRenderer().setSize((float)size);
}

void gfc_gl_draw(const char *str) {
    // Bare draw at current raster position — not supported in quad renderer
    // Fallback: draw at (0,0) — callers should migrate to draw(str, x, y)
    textRenderer().draw(str, 0, 0);
}

void gfc_gl_draw(const char *str, float x, float y) {
    textRenderer().draw(str, x, y);
}

void gfc_gl_draw(const char *str, int x, int y, int w, int h, int align) {
    textRenderer().draw(str, (float)x, (float)y, (float)w, (float)h, align);
}

float gfc_gl_height() {
    return textRenderer().lineHeight();
}

void gfc_gl_measure(const char *str, int &w, int &h, int wrap) {
    textRenderer().measure(str, w, h, wrap);
}
```

- [ ] **Step 2: Initialize renderer in main.cpp**

After the GL context is ready and GLAD is initialized, add:
```cpp
// Initialize text renderer
std::string fontPath = getApplicationDataPath() + "fonts/DejaVuSans.ttf";
std::string boldFontPath = getApplicationDataPath() + "fonts/DejaVuSans-Bold.ttf";
if (!textRenderer().loadFont(fontPath)) {
    // Try relative path
    textRenderer().loadFont("common/fonts/DejaVuSans.ttf");
}
if (!textRenderer().loadBoldFont(boldFontPath)) {
    textRenderer().loadBoldFont("common/fonts/DejaVuSans-Bold.ttf");
}
textRenderer().setDPIScale(mw.vp->pixels_per_unit());
textRenderer().setShadowEnabled(true);
```

- [ ] **Step 3: Commit**

```bash
git add src/gfcTextRenderer.cpp src/main.cpp && git commit -m "feat: add FLTK-compatible wrapper functions and renderer initialization"
```

---

## Chunk 2: Migration — Call Site Replacement (Tasks 4-9)

### Task 4: Migrate gfcPlate.cpp (~12 active calls)

**Files:**
- Modify: `src/gfcPlate.cpp`

- [ ] **Step 1: Add include**

Add `#include "gfcTextRenderer.h"` at the top of `gfcPlate.cpp`.

- [ ] **Step 2: Replace plate label overlay (line ~2283)**

Replace:
```cpp
gl_font(FL_HELVETICA + FL_BOLD, textDisplaySize);
glColor4f(textDisplayColor, textDisplayColor, textDisplayColor, textDisplayOpacity);
gl_draw(labelString.c_str(), rect.x+10, rect.y-15, rect.w, rect.h, Fl_Align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP));
```
with:
```cpp
gfc_gl_font(FL_HELVETICA + FL_BOLD, textDisplaySize);
textRenderer().setColor(textDisplayColor, textDisplayColor, textDisplayColor, textDisplayOpacity);
gfc_gl_draw(labelString.c_str(), rect.x+10, rect.y-15, rect.w, rect.h, FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP);
```

- [ ] **Step 3: Replace remote pointer text (line ~885)**

Convert `glRasterPos3f(x, y, 0) + gl_draw(str)` to `gfc_gl_draw(str, x, y)`:
```cpp
// Before:
gl_font(FL_HELVETICA, remotePointerFontSize);
glRasterPos3f(pIter->x+remotePointerSize, pIter->y+remotePointerSize, 0);
gl_draw(pIter->name.c_str());
// After:
gfc_gl_font(FL_HELVETICA, remotePointerFontSize);
textRenderer().setColor(1, 1, 1, 1); // or whatever color is set
gfc_gl_draw(pIter->name.c_str(), pIter->x+remotePointerSize, pIter->y+remotePointerSize);
```

- [ ] **Step 4: Replace AOI coordinate labels (lines ~1878-1910)**

Replace all `gl_font` → `gfc_gl_font`, `gl_draw` → `gfc_gl_draw`. Set color with `textRenderer().setColor()` before draw calls.

- [ ] **Step 5: Replace hardware warning text (lines ~1195, 2151-2157)**

Same pattern — replace `gl_font` and `gl_draw` with wrappers.

- [ ] **Step 6: Replace drawTextureRectangleWarning() text (lines ~2151)**

Same replacement pattern.

- [ ] **Step 7: Build and test**

```bash
cmake --build build && ./build/jefecheck
```
Load an image, verify text appears in 1x1 mode. Switch to 2x1 — text should NOT be squashed.

- [ ] **Step 8: Commit**

```bash
git commit -am "refactor: migrate gfcPlate.cpp text rendering to GfcTextRenderer"
```

---

### Task 5: Migrate gfcplatemanager.cpp (~10 active calls)

**Files:**
- Modify: `src/gfcplatemanager.cpp`

- [ ] **Step 1: Add include and replace calls**

Key changes:
- Line 95-97: LUT filename label — `gl_font` → `gfc_gl_font`, `gl_draw` → `gfc_gl_draw`
- Line 120-122: "no layout selected" — convert `glRasterPos3f(0,0,0) + gl_draw(str)` to `gfc_gl_draw(str, 0, 0)`
- Line 1153-1154: Remove duplicate `gl_font(FL_TIMES...)` (dead code), keep Helvetica version as `gfc_gl_font`
- Line 1166: Replace `fl_measure()` with `gfc_gl_measure()`
- Line 1203: Replace `gl_draw` with `gfc_gl_draw`
- Lines 1214-1222: **Manual shadow** — replace two `gl_draw` calls (dark at offset 0, light at offset 1) with single `gfc_gl_draw` call (shadow is baked into atlas)

- [ ] **Step 2: Build and test**

Verify help overlay (press 'H' or whatever toggles it), feedback messages, and LUT preview labels.

- [ ] **Step 3: Commit**

```bash
git commit -am "refactor: migrate gfcplatemanager.cpp to GfcTextRenderer (remove manual shadow)"
```

---

### Task 6: Migrate gfcnetworkmanager.cpp (~10 active calls)

**Files:**
- Modify: `src/gfcnetworkmanager.cpp`

- [ ] **Step 1: Add include and replace calls**

Key changes:
- Lines 308-316: Sync status — remove duplicate `gl_font(FL_TIMES...)`, replace with `gfc_gl_font` + `gfc_gl_draw`
- Lines 366-389: Sync waiting messages — same pattern
- Lines 489-490: Chat font — remove FL_TIMES duplicate
- Line 504: Replace `gl_height()` with `gfc_gl_height()` — this is used for background rect sizing, so verify the value matches
- Line 509: Replace `gl_draw` and `gl_height()` usage with wrappers

Note: `gl_rectf()` at line 504 is NOT migrated — it's a geometry call, not text.

- [ ] **Step 2: Build and test**

Test networking chat overlay if possible, or just verify it compiles and doesn't crash.

- [ ] **Step 3: Commit**

```bash
git commit -am "refactor: migrate gfcnetworkmanager.cpp to GfcTextRenderer"
```

---

### Task 7: Migrate gfchistogram.cpp (~3 active calls)

**Files:**
- Modify: `src/gfchistogram.cpp`

- [ ] **Step 1: Replace calls**

- Line 179-180: Remove duplicate `gl_font(FL_TIMES...)`, use `gfc_gl_font`
- Line 194: Replace `gl_draw("Caching Histogram", 0.0f, 0.0f)` with `gfc_gl_draw("Caching Histogram", 0.0f, 0.0f)`

- [ ] **Step 2: Commit**

```bash
git commit -am "refactor: migrate gfchistogram.cpp to GfcTextRenderer"
```

---

### Task 8: Migrate mtpoly.cpp (2 active calls)

**Files:**
- Modify: `src/mtpoly.cpp`

- [ ] **Step 1: Replace calls**

- Line 167: `gl_font(FL_COURIER, 12)` → `gfc_gl_font(FL_COURIER, 12)`
- Line 168: `gl_draw("Area Of Interest", x, y)` → `gfc_gl_draw("Area Of Interest", x, y)`

- [ ] **Step 2: Commit**

```bash
git commit -am "refactor: migrate mtpoly.cpp to GfcTextRenderer"
```

---

### Task 9: Migrate trilerp.cpp (~24 active calls, manual rewrite)

**Files:**
- Modify: `src/trilerp.cpp`

This is the most complex migration — all calls use `glRasterPos3f(...), gl_draw(tmp)` comma-operator patterns for 3D-positioned labels.

- [ ] **Step 1: Add include**

- [ ] **Step 2: Replace 3D cube corner labels (lines 521-636)**

Pattern conversion:
```cpp
// Before:
gl_font(2, 12);
sprintf(tmp, "(%i,%i,%i)", 0, 0, 0);
glRasterPos3f(cube[0][0][0].x, cube[0][0][0].y, cube[0][0][0].z), gl_draw(tmp);

// After:
gfc_gl_font(FL_HELVETICA, 12);
sprintf(tmp, "(%i,%i,%i)", 0, 0, 0);
textRenderer().draw3D(tmp, cube[0][0][0].x, cube[0][0][0].y, cube[0][0][0].z);
```

Apply to all 4 corner labels in `gfcLUT1D::draw3D()` (lines 524-536) and `gfcLUT1D::drawActual()` (lines 627-636).

- [ ] **Step 3: Replace 1D curve axis labels (lines 572-701)**

These use `gl_font(2, 10*scale)` where `scale` is the zoom level. The font size is dynamic.

```cpp
// Before:
gl_font(2, 10*scale);
sprintf(tmp, "%.2f", maximum1DValue);
glRasterPos3f(-20/scale, maximum1DValue*size*1.2, 0), gl_draw(tmp);

// After:
gfc_gl_font(FL_HELVETICA, (int)(10*scale));
sprintf(tmp, "%.2f", maximum1DValue);
textRenderer().draw3D(tmp, -20/scale, maximum1DValue*size*1.2, 0);
```

Note: Dynamic font size `10*scale` will trigger atlas rebakes during zooming. This is acceptable — the atlas cache will hold the most recent sizes. For smooth zooming, consider clamping to discrete sizes (10, 12, 14, 16, 18, 20, 24).

- [ ] **Step 4: Replace curve sample point labels (lines 594-701)**

Same `glRasterPos3f + gl_draw` to `draw3D` conversion for all sample point labels.

- [ ] **Step 5: Build and test**

Load a LUT, verify 3D cube labels appear at correct positions.

- [ ] **Step 6: Commit**

```bash
git commit -am "refactor: migrate trilerp.cpp to GfcTextRenderer (3D labels)"
```

---

## Chunk 3: Cleanup and Verification (Task 10)

### Task 10: Remove FLTK GL text dependencies and verify

**Files:**
- Modify: `src/main.cpp` — remove `gl_texture_pile_height()` call
- Verify: all 6 migrated files no longer call `gl_font`, `gl_draw`, `gl_height`

- [ ] **Step 1: Remove gl_texture_pile_height from main.cpp**

Remove:
```cpp
gl_texture_pile_height(100);
printf("gl_texture_pile_height=%i\n",gl_texture_pile_height());
```

- [ ] **Step 2: Verify no remaining FLTK GL text calls**

```bash
grep -rn "gl_draw\|gl_font\|gl_height" src/ --include="*.cpp" --include="*.h" | grep -v "//" | grep -v "gfc_gl_"
```

Expected: no active (non-commented) calls remain.

- [ ] **Step 3: Test all layouts**

1. Load an image, verify text in 1x1 mode
2. Switch to 2x1 — verify text NOT squashed, correct position
3. Switch to 1x2 — same verification
4. Switch to 2x2 — same verification
5. Resize window in each layout — no crashes
6. Toggle help overlay
7. Check drop shadow visible on light and dark images

- [ ] **Step 4: Test on Linux (VM or CI)**

```bash
git push origin modernize-opensource
```
Verify CI passes on all platforms.

- [ ] **Step 5: Final commit**

```bash
git commit -am "chore: remove FLTK GL text dependencies, cleanup"
```
