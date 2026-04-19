#include <ft2build.h>
#include FT_FREETYPE_H

#include "gfcTextRenderer.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>

#ifdef __APPLE__
#include <OpenGL/glu.h>
#elif defined(_WIN32)
#include <windows.h>
#include <GL/glu.h>
#else
#include <GL/glu.h>
#endif

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

GfcTextRenderer& textRenderer() {
    static GfcTextRenderer instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

GfcTextRenderer::GfcTextRenderer()
    : fontLoaded(false)
    , boldFontLoaded(false)
    , currentSize(14.0f)
    , dpiScale(1.0f)
    , currentBold(false)
    , colorR(1.0f), colorG(1.0f), colorB(1.0f), colorA(1.0f)
    , shadowEnabled(true)
    , shadowOffX(1.0f), shadowOffY(-1.0f)
    , shadowR(0.0f), shadowG(0.0f), shadowB(0.0f), shadowA(0.5f)
    , shadowBlurRadius(0.0f)
    , hintMode(HINT_LIGHT)
    , filterNearest(true)
    , gammaValue(0.65f)
{
}

GfcTextRenderer::~GfcTextRenderer() {
    for (auto &pair : atlasCache) {
        if (pair.second.textureID)
            glDeleteTextures(1, &pair.second.textureID);
    }
    for (auto &pair : boldAtlasCache) {
        if (pair.second.textureID)
            glDeleteTextures(1, &pair.second.textureID);
    }
}

// ---------------------------------------------------------------------------
// Font loading
// ---------------------------------------------------------------------------

static bool readFileToVector(const std::string &path, std::vector<unsigned char> &out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    auto size = f.tellg();
    if (size <= 0) return false;
    out.resize((size_t)size);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), size);
    return f.good();
}

bool GfcTextRenderer::loadFont(const std::string &fontPath) {
    if (readFileToVector(fontPath, fontData)) {
        fontLoaded = true;
        // Invalidate all cached atlases so they rebake with the new font
        for (auto &pair : atlasCache) {
            if (pair.second.textureID) glDeleteTextures(1, &pair.second.textureID);
        }
        atlasCache.clear();
        printf("GfcTextRenderer: loaded font %s\n", fontPath.c_str());
        return true;
    }
    printf("GfcTextRenderer: failed to load font %s\n", fontPath.c_str());
    return false;
}

bool GfcTextRenderer::loadBoldFont(const std::string &fontPath) {
    if (readFileToVector(fontPath, boldFontData)) {
        boldFontLoaded = true;
        for (auto &pair : boldAtlasCache) {
            if (pair.second.textureID) glDeleteTextures(1, &pair.second.textureID);
        }
        boldAtlasCache.clear();
        printf("GfcTextRenderer: loaded bold font %s\n", fontPath.c_str());
        return true;
    }
    printf("GfcTextRenderer: failed to load bold font %s\n", fontPath.c_str());
    return false;
}

// ---------------------------------------------------------------------------
// State setters
// ---------------------------------------------------------------------------

void GfcTextRenderer::setSize(float pixelSize) { currentSize = pixelSize; }
void GfcTextRenderer::setBold(bool bold) { currentBold = bold; }
void GfcTextRenderer::setDPIScale(float scale) { dpiScale = scale; }
void GfcTextRenderer::setColor(float r, float g, float b, float a) {
    colorR = r; colorG = g; colorB = b; colorA = a;
}
void GfcTextRenderer::setShadowEnabled(bool enabled) { shadowEnabled = enabled; }
void GfcTextRenderer::setShadowOffset(float x, float y) { shadowOffX = x; shadowOffY = y; }
void GfcTextRenderer::setShadowColor(float r, float g, float b, float a) {
    shadowR = r; shadowG = g; shadowB = b; shadowA = a;
}
void GfcTextRenderer::setShadowBlur(float radius) { shadowBlurRadius = radius; }

void GfcTextRenderer::setHintMode(HintMode mode) {
    if (hintMode != mode) {
        hintMode = mode;
        // Invalidate caches — need to rebake with new hinting
        for (auto &p : atlasCache) { if (p.second.textureID) glDeleteTextures(1, &p.second.textureID); }
        for (auto &p : boldAtlasCache) { if (p.second.textureID) glDeleteTextures(1, &p.second.textureID); }
        atlasCache.clear();
        boldAtlasCache.clear();
    }
}

void GfcTextRenderer::setFilterNearest(bool nearest) { filterNearest = nearest; }

void GfcTextRenderer::setGamma(float gamma) {
    if (gammaValue != gamma) {
        gammaValue = gamma;
        for (auto &p : atlasCache) { if (p.second.textureID) glDeleteTextures(1, &p.second.textureID); }
        for (auto &p : boldAtlasCache) { if (p.second.textureID) glDeleteTextures(1, &p.second.textureID); }
        atlasCache.clear();
        boldAtlasCache.clear();
    }
}

// ---------------------------------------------------------------------------
// Atlas baking
// ---------------------------------------------------------------------------

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

// Round up to next power of 2
static int nextPow2(int v) {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v + 1;
}

GfcFontAtlas GfcTextRenderer::bakeAtlas(const std::vector<unsigned char> &data, float pixelSize, float dpiScale) {
    GfcFontAtlas atlas;
    const int PADDING = 2;
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

    // Hinting mode from preferences
    FT_Int32 loadFlags = FT_LOAD_RENDER;
    switch (hintMode) {
        case HINT_LIGHT:  loadFlags |= FT_LOAD_TARGET_LIGHT; break;
        case HINT_NORMAL: loadFlags |= FT_LOAD_TARGET_NORMAL; break;
        case HINT_AUTO:   loadFlags |= FT_LOAD_TARGET_NORMAL | FT_LOAD_FORCE_AUTOHINT; break;
    }

    // Pass 1: measure glyph sizes to determine atlas dimensions
    // Use a reasonable width, compute needed height
    int atlasW = std::max(256, nextPow2((int)(pixelSize * 16)));  // ~16 glyphs per row
    if (atlasW > 2048) atlasW = 2048;
    int penX = PADDING, penY = PADDING, rowHeight = 0;

    for (int i = 0; i < 96; i++) {
        if (FT_Load_Char(face, 32 + i, loadFlags)) continue;
        int bw = face->glyph->bitmap.width;
        int bh = face->glyph->bitmap.rows;
        if (penX + bw + PADDING > atlasW) {
            penX = PADDING;
            penY += rowHeight + PADDING;
            rowHeight = 0;
        }
        penX += bw + PADDING;
        if (bh > rowHeight) rowHeight = bh;
    }
    int atlasH = nextPow2(penY + rowHeight + PADDING);
    if (atlasH < 16) atlasH = 16;
    if (atlasH > 2048) atlasH = 2048;

    const int TEX_W = atlasW;
    const int TEX_H = atlasH;
    atlas.texWidth = TEX_W;
    atlas.texHeight = TEX_H;

    printf("GfcTextRenderer: atlas %dx%d for %.0fpx font\n", TEX_W, TEX_H, pixelSize);

    // Pass 2: re-render and pack into the right-sized atlas
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pixelSize);  // reset after pass 1

    unsigned char *bitmap = new unsigned char[TEX_W * TEX_H];
    memset(bitmap, 0, TEX_W * TEX_H);

    penX = PADDING; penY = PADDING; rowHeight = 0;

    for (int i = 0; i < 96; i++) {
        FT_UInt ch = 32 + i;
        if (FT_Load_Char(face, ch, loadFlags)) {
            GfcBakedGlyph &g = atlas.glyphs[i];
            g.x0 = g.y0 = g.x1 = g.y1 = 0;
            g.u0 = g.v0 = g.u1 = g.v1 = 0;
            g.xadvance = pixelSize * 0.5f;
            continue;
        }

        FT_GlyphSlot glyph = face->glyph;
        int bw = glyph->bitmap.width;
        int bh = glyph->bitmap.rows;

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
        g.y0 = -(float)glyph->bitmap_top;
        g.x1 = (float)(glyph->bitmap_left + bw);
        g.y1 = -(float)(glyph->bitmap_top - bh);
        g.u0 = penX / (float)TEX_W;
        g.u1 = (penX + bw) / (float)TEX_W;
        g.v0 = 1.0f - penY / (float)TEX_H;
        g.v1 = 1.0f - (penY + bh) / (float)TEX_H;
        g.xadvance = glyph->advance.x / 64.0f;

        penX += bw + PADDING;
        if (bh > rowHeight) rowHeight = bh;
    }

    // Font-level metrics (26.6 fixed-point)
    atlas.ascent = face->size->metrics.ascender / 64.0f;
    atlas.descent = face->size->metrics.descender / 64.0f;
    atlas.lineGap = 0;
    atlas.lineHeight = face->size->metrics.height / 64.0f;

    FT_Done_Face(face);

    // Gamma boost: darken semi-transparent edge pixels to approximate
    // Core Text's gamma-correct blending. Lower values = bolder text.
    if (gammaValue < 0.99f) {
        for (int i = 0; i < TEX_W * TEX_H; i++) {
            if (bitmap[i] == 0 || bitmap[i] == 255) continue;  // skip fully transparent/opaque
            float v = bitmap[i] / 255.0f;
            v = powf(v, gammaValue);
            bitmap[i] = (unsigned char)(v * 255.0f + 0.5f);
        }
    }

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
    GLint texFilter = filterNearest ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, TEX_W, TEX_H, 0,
                 GL_ALPHA, GL_UNSIGNED_BYTE, bitmap);

    delete[] bitmap;
    atlas.valid = true;
    return atlas;
}

GfcFontAtlas& GfcTextRenderer::getAtlas() {
    int key = (int)(currentSize * dpiScale * 100.0f);
    auto &cache = currentBold ? boldAtlasCache : atlasCache;
    auto it = cache.find(key);
    if (it != cache.end()) {
        if (it->second.valid && glIsTexture(it->second.textureID))
            return it->second;
        if (it->second.textureID)
            glDeleteTextures(1, &it->second.textureID);
        cache.erase(it);
    }

    const auto &data = currentBold && boldFontLoaded ? boldFontData : fontData;
    cache[key] = bakeAtlas(data, currentSize * dpiScale, dpiScale);
    return cache[key];
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// Emit quads for a line of text at (x, y) baseline. No GL state changes.
static void emitQuads(const GfcBakedGlyph *glyphs, const char *str, int len,
                      float x, float y, float offX, float offY) {
    // Snap baseline and cursor origin to integer pixels FIRST,
    // then add glyph offsets. This prevents oversampling's sub-pixel
    // shifts from causing inconsistent rounding between glyphs.
    float baseX = floorf(x + offX + 0.5f);
    float baseY = floorf(y + offY + 0.5f);

    glBegin(GL_QUADS);
    float cursorX = baseX;
    for (int i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)str[i];
        if (ch < 32 || ch >= 128) continue;
        const GfcBakedGlyph &g = glyphs[ch - 32];

        // Use integer glyph dimensions from the snapped cursor/baseline
        float qx0 = floorf(cursorX + g.x0);
        float qx1 = qx0 + floorf(g.x1 - g.x0 + 0.5f);  // preserve integer glyph width
        float qy0 = baseY - floorf(g.y0 + 0.5f);
        float qy1 = baseY - floorf(g.y1 + 0.5f);

        glTexCoord2f(g.u0, g.v0); glVertex2f(qx0, qy0);
        glTexCoord2f(g.u1, g.v0); glVertex2f(qx1, qy0);
        glTexCoord2f(g.u1, g.v1); glVertex2f(qx1, qy1);
        glTexCoord2f(g.u0, g.v1); glVertex2f(qx0, qy1);

        cursorX = floorf(cursorX + g.xadvance + 0.5f);  // snap advance too
    }
    glEnd();
}

void GfcTextRenderer::drawLine(const char *str, int len, float x, float y) {
    if (!fontLoaded) return;
    GfcFontAtlas &atlas = getAtlas();
    if (!atlas.valid) return;

    glPushAttrib(GL_ALL_ATTRIB_BITS);

    GLhandleARB prevProgram = glGetHandleARB(GL_PROGRAM_OBJECT_ARB);
    if (prevProgram) glUseProgramObjectARB(0);

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glBindTexture(GL_TEXTURE_2D, atlas.textureID);

    // Pass 1: shadow (offset, dark color, optional blur via multi-pass)
    if (shadowEnabled) {
        if (shadowBlurRadius > 0.1f) {
            // Draw shadow at 5 sample points (center + 4 cardinal offsets)
            // with reduced alpha per sample for a soft blur effect
            float a = shadowA * colorA * 0.34f;  // ~1/3 per sample, overlap adds up
            glColor4f(shadowR, shadowG, shadowB, a);
            emitQuads(atlas.glyphs, str, len, x, y, shadowOffX, shadowOffY);
            emitQuads(atlas.glyphs, str, len, x, y, shadowOffX - shadowBlurRadius, shadowOffY);
            emitQuads(atlas.glyphs, str, len, x, y, shadowOffX + shadowBlurRadius, shadowOffY);
            emitQuads(atlas.glyphs, str, len, x, y, shadowOffX, shadowOffY - shadowBlurRadius);
            emitQuads(atlas.glyphs, str, len, x, y, shadowOffX, shadowOffY + shadowBlurRadius);
        } else {
            glColor4f(shadowR, shadowG, shadowB, shadowA * colorA);
            emitQuads(atlas.glyphs, str, len, x, y, shadowOffX, shadowOffY);
        }
    }

    // Pass 2: foreground text
    glColor4f(colorR, colorG, colorB, colorA);
    emitQuads(atlas.glyphs, str, len, x, y, 0, 0);

    if (prevProgram) glUseProgramObjectARB(prevProgram);
    glPopAttrib();
}

// Helper: set up pixel-exact ortho projection for the current viewport.
// Returns the viewport bounds. Caller must pop both matrices when done.
static void beginPixelOrtho(GLint viewport[4]) {
    glGetIntegerv(GL_VIEWPORT, viewport);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(viewport[0], viewport[0] + viewport[2],
            viewport[1], viewport[1] + viewport[3], -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
}

static void endPixelOrtho() {
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}

// Project a 2D point (z=0) from the caller's GL coordinate space to screen pixels.
static void projectToScreen(float x, float y, float &sx, float &sy) {
    GLdouble modelview[16], projection[16];
    GLint viewport[4];
    GLdouble dsx, dsy, dsz;
    glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
    glGetDoublev(GL_PROJECTION_MATRIX, projection);
    glGetIntegerv(GL_VIEWPORT, viewport);
    gluProject(x, y, 0, modelview, projection, viewport, &dsx, &dsy, &dsz);
    sx = (float)dsx;
    sy = (float)dsy;
}

void GfcTextRenderer::draw(const char *str, float x, float y) {
    if (!str || !*str || !fontLoaded) return;

    // Project the caller's (x, y) to screen pixel coordinates
    float sx, sy;
    projectToScreen(x, y, sx, sy);

    // Render in pixel-exact ortho — 1:1 texel-to-pixel mapping
    GLint viewport[4];
    beginPixelOrtho(viewport);
    drawLine(str, (int)strlen(str), sx, sy);
    endPixelOrtho();
}

void GfcTextRenderer::draw(const char *str, float x, float y, float w, float h, int align) {
    if (!str || !*str || !fontLoaded) return;

    // Project the bounding box corners to screen pixel coordinates
    float sx0, sy0, sx1, sy1;
    projectToScreen(x, y, sx0, sy0);          // lower-left
    projectToScreen(x + w, y + h, sx1, sy1);  // upper-right

    float screenW = sx1 - sx0;
    float screenH = sy1 - sy0;

    // Word wrap uses screen-pixel widths for correct line breaking
    auto lines = wrapText(str, (align & GFC_ALIGN_WRAP) ? screenW : 0.0f);
    if (lines.empty()) return;

    GfcFontAtlas &atlas = getAtlas();
    // Use atlas pixel sizes (= physical pixels in our pixel-exact ortho)
    float lhPx = atlas.lineHeight;
    float totalHeightPx = lines.size() * lhPx;

    // Vertical positioning in screen pixels (Y-up: top = sy1, bottom = sy0)
    float topY;
    if (align & GFC_ALIGN_BOTTOM) {
        topY = sy0 + totalHeightPx;
    } else if (align & GFC_ALIGN_TOP) {
        topY = sy1;
    } else {
        topY = sy0 + (screenH + totalHeightPx) / 2.0f;
    }

    // Set up pixel-exact ortho for rendering
    GLint viewport[4];
    beginPixelOrtho(viewport);

    for (size_t i = 0; i < lines.size(); i++) {
        float lineX = sx0;
        if (align & GFC_ALIGN_LEFT) {
            lineX = sx0;
        } else if (align & GFC_ALIGN_RIGHT) {
            lineX = sx0 + screenW - lines[i].width;
        } else {
            lineX = sx0 + (screenW - lines[i].width) / 2.0f;
        }
        float baselineY = topY - atlas.ascent - i * lhPx;
        drawLine(lines[i].start, lines[i].length, lineX, baselineY);
    }

    endPixelOrtho();
}

void GfcTextRenderer::draw3D(const char *str, float x, float y, float z) {
    if (!str || !*str || !fontLoaded) return;

    GLdouble modelview[16], projection[16];
    GLint viewport[4];
    GLdouble sx, sy, sz;
    glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
    glGetDoublev(GL_PROJECTION_MATRIX, projection);
    glGetIntegerv(GL_VIEWPORT, viewport);
    gluProject(x, y, z, modelview, projection, viewport, &sx, &sy, &sz);

    beginPixelOrtho(viewport);
    drawLine(str, (int)strlen(str), (float)sx, (float)sy);
    endPixelOrtho();
}

// ---------------------------------------------------------------------------
// Measurement
// ---------------------------------------------------------------------------

float GfcTextRenderer::textWidth(const char *str) {
    if (!str || !fontLoaded) return 0;
    GfcFontAtlas &atlas = getAtlas();
    float w = 0;
    while (*str) {
        unsigned char ch = (unsigned char)*str;
        if (ch >= 32 && ch < 128)
            w += atlas.glyphs[ch - 32].xadvance / dpiScale;
        str++;
    }
    return w;
}

float GfcTextRenderer::lineHeight() {
    if (!fontLoaded) return 14.0f;
    return getAtlas().lineHeight / dpiScale;
}

void GfcTextRenderer::measure(const char *str, int &w, int &h, int wrapWidth) {
    if (!str || !fontLoaded) { w = 0; h = 0; return; }
    if (wrapWidth > 0) {
        // wrapText works in atlas pixels; convert wrapWidth from logical to atlas pixels
        auto lines = wrapText(str, (float)wrapWidth * dpiScale);
        float maxW = 0;
        for (auto &l : lines) maxW = std::max(maxW, l.width);
        // Convert back to logical pixels for external callers
        w = (int)(maxW / dpiScale);
        h = (int)(lines.size() * lineHeight());
    } else {
        w = (int)textWidth(str);
        h = (int)lineHeight();
    }
}

// ---------------------------------------------------------------------------
// Word wrapping
// ---------------------------------------------------------------------------

std::vector<GfcTextRenderer::TextLine> GfcTextRenderer::wrapText(const char *str, float maxWidth) {
    std::vector<TextLine> lines;
    if (!str || !*str) return lines;

    const char *lineStart = str;
    const char *wordStart = str;
    float lineWidth = 0;
    float wordWidth = 0;

    GfcFontAtlas &atlas = getAtlas();

    while (true) {
        unsigned char ch = (unsigned char)*str;

        if (ch == '\n' || ch == '\0') {
            if (str > lineStart) {
                TextLine tl;
                tl.start = lineStart;
                tl.length = (int)(str - lineStart);
                tl.width = lineWidth + wordWidth;
                lines.push_back(tl);
            } else {
                TextLine tl;
                tl.start = lineStart;
                tl.length = 0;
                tl.width = 0;
                lines.push_back(tl);
            }
            if (ch == '\0') break;
            str++;
            lineStart = str;
            wordStart = str;
            lineWidth = 0;
            wordWidth = 0;
            continue;
        }

        float charW = 0;
        if (ch >= 32 && ch < 128)
            charW = atlas.glyphs[ch - 32].xadvance;

        if (ch == ' ') {
            lineWidth += wordWidth + charW;
            wordWidth = 0;
            str++;
            wordStart = str;
            continue;
        }

        wordWidth += charW;

        if (maxWidth > 0 && lineWidth + wordWidth > maxWidth && lineWidth > 0) {
            TextLine tl;
            tl.start = lineStart;
            tl.length = (int)(wordStart - lineStart);
            while (tl.length > 0 && tl.start[tl.length - 1] == ' ')
                tl.length--;
            tl.width = lineWidth;
            lines.push_back(tl);
            lineStart = wordStart;
            lineWidth = 0;
        }

        str++;
    }

    return lines;
}

// ---------------------------------------------------------------------------
// FLTK-compatible wrapper functions
// ---------------------------------------------------------------------------

void gfc_gl_font(int face, int size) {
    // FL_HELVETICA=0, FL_BOLD=1, FL_ITALIC=2
    bool bold = (face & 1) != 0;
    textRenderer().setBold(bold);
    textRenderer().setSize((float)size);
}

void gfc_gl_draw(const char *str) {
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

// ---------------------------------------------------------------------------
// System font enumeration
// ---------------------------------------------------------------------------

#include <dirent.h>
#include <sys/stat.h>

static void scanFontDir(const std::string &dirPath,
                        std::vector<std::pair<std::string, std::string>> &results) {
    DIR *dir = opendir(dirPath.c_str());
    if (!dir) return;

    FT_Library lib = ftLibrary();
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        // Check for .ttf or .otf extension
        size_t len = name.size();
        if (len < 5) continue;
        std::string ext = name.substr(len - 4);
        // Convert to lowercase for comparison
        for (auto &c : ext) c = tolower(c);
        if (ext != ".ttf" && ext != ".otf") continue;

        std::string fullPath = dirPath + "/" + name;

        // Use FreeType to get the font family name
        FT_Face face;
        if (lib && FT_New_Face(lib, fullPath.c_str(), 0, &face) == 0) {
            std::string displayName = face->family_name ? face->family_name : name;
            if (face->style_name && std::string(face->style_name) != "Regular") {
                displayName += " ";
                displayName += face->style_name;
            }
            FT_Done_Face(face);
            results.push_back({displayName, fullPath});
        }
    }
    closedir(dir);
}

std::vector<std::pair<std::string, std::string>> enumerateSystemFonts() {
    std::vector<std::pair<std::string, std::string>> fonts;

#ifdef __APPLE__
    scanFontDir("/System/Library/Fonts", fonts);
    scanFontDir("/System/Library/Fonts/Supplemental", fonts);
    scanFontDir("/Library/Fonts", fonts);
    // User fonts
    const char *home = getenv("HOME");
    if (home) scanFontDir(std::string(home) + "/Library/Fonts", fonts);
#elif defined(_WIN32)
    const char *windir = getenv("WINDIR");
    if (windir) scanFontDir(std::string(windir) + "\\Fonts", fonts);
#else
    scanFontDir("/usr/share/fonts/truetype", fonts);
    scanFontDir("/usr/share/fonts/TTF", fonts);
    scanFontDir("/usr/share/fonts/opentype", fonts);
    scanFontDir("/usr/local/share/fonts", fonts);
    const char *home = getenv("HOME");
    if (home) scanFontDir(std::string(home) + "/.local/share/fonts", fonts);
#endif

    // Also include our bundled fonts (relative to working directory)
    scanFontDir("fonts", fonts);

    // Sort by display name
    std::sort(fonts.begin(), fonts.end());

    // Remove duplicates (same display name)
    fonts.erase(std::unique(fonts.begin(), fonts.end(),
        [](const auto &a, const auto &b) { return a.first == b.first; }),
        fonts.end());

    return fonts;
}
