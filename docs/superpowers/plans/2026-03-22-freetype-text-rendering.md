# FreeType Text Rendering Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace stb_truetype's rasterizer with FreeType in `bakeAtlas()` for hinted glyph rendering, improving text sharpness on all displays.

**Architecture:** FreeType replaces only the glyph rasterization step in `bakeAtlas()`. The rest of the pipeline — atlas texture, GL upload, pixel-exact ortho projection, two-pass shadow, word wrapping, wrapper functions — stays identical. FreeType's hinting snaps stems to pixel boundaries, producing crisp text that matches native UI quality.

**Tech Stack:** FreeType 2.x (FreeType License / BSD-like), CMake `find_package(Freetype)`, existing OpenGL pipeline

**Spec:** `docs/superpowers/specs/2026-03-22-freetype-text-rendering-design.md`

---

## Task 1: Add FreeType dependency to build system

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/build.yml`

- [ ] **Step 1: Add find_package and link to CMakeLists.txt**

After `find_package(ZLIB REQUIRED)`, add:
```cmake
find_package(Freetype REQUIRED)
```

In the main `target_link_libraries` block, add `Freetype::Freetype`:
```cmake
target_link_libraries(jefecheck PRIVATE
    glad
    cli11
    OpenGL::GL
    Freetype::Freetype
    ${FLTK_LIBRARIES}
    ...
)
```

- [ ] **Step 2: Add FreeType dev packages to CI**

**Linux** (`.github/workflows/build.yml` line 18-40): add `libfreetype6-dev` to the apt install list.

**macOS** (line 54): add `freetype` to brew install:
```
brew install fltk openimageio openexr curl zlib cmake freetype
```

**Windows/MSYS2** (line 75-85): add `mingw-w64-x86_64-freetype` to the install list.

- [ ] **Step 3: Verify build still compiles**

```bash
cmake -B build && cmake --build build
```

Expected: builds successfully, no linker errors. FreeType is found but not yet used.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt .github/workflows/build.yml
git commit -m "build: add FreeType dependency for hinted text rendering"
```

---

## Task 2: Replace stb_truetype with FreeType in bakeAtlas()

**Files:**
- Modify: `src/gfcTextRenderer.cpp` (lines 1-2, 108-187)
- Modify: `src/gfcTextRenderer.h` (add dpiScale parameter to bakeAtlas)

- [ ] **Step 1: Replace stb include with FreeType includes**

In `src/gfcTextRenderer.cpp`, replace lines 1-2:
```cpp
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
```
with:
```cpp
#include <ft2build.h>
#include FT_FREETYPE_H
```

- [ ] **Step 2: Add static FT_Library initialization**

Add a static helper at the top of the atlas baking section (after the includes):
```cpp
static FT_Library ftLibrary() {
    static FT_Library lib = nullptr;
    if (!lib) {
        if (FT_Init_FreeType(&lib)) {
            printf("GfcTextRenderer: FreeType initialization failed\n");
            lib = nullptr;
        }
    }
    return lib;
}
```

- [ ] **Step 3: Update bakeAtlas signature to receive dpiScale**

In `src/gfcTextRenderer.h`, change the private declaration:
```cpp
GfcFontAtlas bakeAtlas(const std::vector<unsigned char> &data, float pixelSize, float dpiScale);
```

Update the call in `getAtlas()` (src/gfcTextRenderer.cpp):
```cpp
cache[key] = bakeAtlas(data, currentSize * dpiScale, dpiScale);
```

- [ ] **Step 4: Rewrite bakeAtlas() with FreeType rasterization**

Replace the entire `bakeAtlas()` function body (lines 111-187) with:

```cpp
GfcFontAtlas GfcTextRenderer::bakeAtlas(const std::vector<unsigned char> &data, float pixelSize, float dpiScale) {
    GfcFontAtlas atlas;
    const int TEX_W = 2048;
    const int TEX_H = 2048;
    const int PADDING = 2;
    atlas.texWidth = TEX_W;
    atlas.texHeight = TEX_H;
    atlas.pixelSize = pixelSize;
    atlas.textureID = 0;
    atlas.valid = false;

    FT_Library lib = ftLibrary();
    if (!lib) return atlas;

    FT_Face face;
    if (FT_New_Memory_Face(lib, data.data(), (FT_Long)data.size(), 0, &face)) {
        printf("GfcTextRenderer: FreeType failed to load font\n");
        return atlas;
    }
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pixelSize);

    // Hinting mode: light for Retina, full for standard DPI
    FT_Int32 loadFlags = FT_LOAD_RENDER;
    if (dpiScale >= 2.0f)
        loadFlags |= FT_LOAD_TARGET_LIGHT;
    else
        loadFlags |= FT_LOAD_TARGET_NORMAL;

    unsigned char *bitmap = new unsigned char[TEX_W * TEX_H];
    memset(bitmap, 0, TEX_W * TEX_H);

    // Row-by-row bin packing
    int penX = PADDING, penY = PADDING, rowHeight = 0;

    for (int i = 0; i < 96; i++) {
        FT_UInt ch = 32 + i;
        if (FT_Load_Char(face, ch, loadFlags)) {
            // Glyph load failed — leave as empty
            GfcBakedGlyph &g = atlas.glyphs[i];
            g.x0 = g.y0 = g.x1 = g.y1 = 0;
            g.u0 = g.v0 = g.u1 = g.v1 = 0;
            g.xadvance = pixelSize * 0.5f;
            continue;
        }

        FT_GlyphSlot glyph = face->glyph;
        int bw = glyph->bitmap.width;
        int bh = glyph->bitmap.rows;

        // Advance to next row if glyph doesn't fit
        if (penX + bw + PADDING > TEX_W) {
            penX = PADDING;
            penY += rowHeight + PADDING;
            rowHeight = 0;
        }
        if (penY + bh + PADDING > TEX_H) {
            printf("GfcTextRenderer: atlas full at glyph %d\n", ch);
            break;
        }

        // Copy glyph bitmap into atlas
        for (int row = 0; row < bh; row++) {
            memcpy(&bitmap[(penY + row) * TEX_W + penX],
                   &glyph->bitmap.buffer[row * glyph->bitmap.pitch],
                   bw);
        }

        // Fill GfcBakedGlyph — offsets relative to baseline cursor
        GfcBakedGlyph &g = atlas.glyphs[i];
        g.x0 = (float)glyph->bitmap_left;
        g.y0 = -(float)glyph->bitmap_top;          // FreeType Y-up → stb Y-down convention
        g.x1 = (float)(glyph->bitmap_left + bw);
        g.y1 = -(float)(glyph->bitmap_top - bh);
        g.u0 = penX / (float)TEX_W;
        g.u1 = (penX + bw) / (float)TEX_W;
        // Flip v for GL bottom-up convention (applied after bitmap flip below)
        g.v0 = 1.0f - penY / (float)TEX_H;
        g.v1 = 1.0f - (penY + bh) / (float)TEX_H;
        g.xadvance = glyph->advance.x / 64.0f;     // 26.6 fixed-point

        penX += bw + PADDING;
        if (bh > rowHeight) rowHeight = bh;
    }

    // Font-level metrics (26.6 fixed-point)
    atlas.ascent = face->size->metrics.ascender / 64.0f;
    atlas.descent = face->size->metrics.descender / 64.0f;
    atlas.lineGap = 0;
    atlas.lineHeight = face->size->metrics.height / 64.0f;

    FT_Done_Face(face);

    // Flip bitmap vertically for GL's bottom-up convention
    for (int y = 0; y < TEX_H / 2; y++) {
        int y2 = TEX_H - 1 - y;
        for (int x = 0; x < TEX_W; x++) {
            std::swap(bitmap[y * TEX_W + x], bitmap[y2 * TEX_W + x]);
        }
    }

    // Upload as GL_ALPHA texture
    glGenTextures(1, &atlas.textureID);
    glBindTexture(GL_TEXTURE_2D, atlas.textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, TEX_W, TEX_H, 0,
                 GL_ALPHA, GL_UNSIGNED_BYTE, bitmap);

    delete[] bitmap;
    atlas.valid = true;
    return atlas;
}
```

- [ ] **Step 5: Build and test**

```bash
cmake -B build && cmake --build build
./build/jefecheck
```

Load an image, verify text renders. Compare diagonal strokes (`/`, `7`) against the stb_truetype version — stems should be noticeably crisper with hinting.

- [ ] **Step 6: Commit**

```bash
git add src/gfcTextRenderer.cpp src/gfcTextRenderer.h
git commit -m "feat: replace stb_truetype with FreeType for hinted glyph rendering"
```

---

## Task 3: Remove stb_truetype include path

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Remove stb include path from CMakeLists.txt**

Remove `third_party/stb/` from `target_include_directories`:
```cmake
target_include_directories(jefecheck PRIVATE
    src/
)
```

(Keep `third_party/stb/stb_truetype.h` in the repo for reference, just don't compile against it.)

- [ ] **Step 2: Build and verify**

```bash
cmake -B build && cmake --build build
```

Expected: builds cleanly — no file includes stb_truetype.h anymore.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "chore: remove stb_truetype include path (replaced by FreeType)"
```

---

## Task 4: Test all platforms and push

- [ ] **Step 1: Test all text rendering scenarios on macOS**

1. Load an image — verify plate label text renders correctly
2. Switch to 2x2 layout — text not squashed, correct positions
3. Press 'h' — help overlay renders, centered, word-wrapped
4. Check diagonal characters (`/`, `7`, `k`) — should be crisper than before
5. Check drop shadow visible on light backgrounds
6. Resize window — no crashes

- [ ] **Step 2: Push and verify CI**

```bash
git push origin modernize-opensource
```

Check GitHub Actions — all three platforms (macOS, Linux, Windows) should build successfully with FreeType.

- [ ] **Step 3: Final commit if any CI fixes needed**

Fix any platform-specific issues (include paths, package names) and push.
