#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "gfcTextRenderer.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>

#ifdef __APPLE__
#include <OpenGL/glu.h>
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
        printf("GfcTextRenderer: loaded font %s\n", fontPath.c_str());
        return true;
    }
    printf("GfcTextRenderer: failed to load font %s\n", fontPath.c_str());
    return false;
}

bool GfcTextRenderer::loadBoldFont(const std::string &fontPath) {
    if (readFileToVector(fontPath, boldFontData)) {
        boldFontLoaded = true;
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

// ---------------------------------------------------------------------------
// Atlas baking
// ---------------------------------------------------------------------------

GfcFontAtlas GfcTextRenderer::bakeAtlas(const std::vector<unsigned char> &data, float pixelSize) {
    GfcFontAtlas atlas;
    const int TEX_W = 2048;
    const int TEX_H = 2048;
    atlas.texWidth = TEX_W;
    atlas.texHeight = TEX_H;
    atlas.pixelSize = pixelSize;
    atlas.textureID = 0;
    atlas.valid = false;

    unsigned char *bitmap = new unsigned char[TEX_W * TEX_H];
    memset(bitmap, 0, TEX_W * TEX_H);

    stbtt_pack_context pc;
    stbtt_PackBegin(&pc, bitmap, TEX_W, TEX_H, 0, 2, nullptr);
    stbtt_PackSetOversampling(&pc, 1, 1);

    stbtt_packedchar pdata[96];
    stbtt_PackFontRange(&pc, data.data(), 0, pixelSize, 32, 96, pdata);
    stbtt_PackEnd(&pc);

    // Get font metrics
    stbtt_fontinfo fontInfo;
    if (stbtt_InitFont(&fontInfo, data.data(), 0)) {
        int iAscent, iDescent, iLineGap;
        stbtt_GetFontVMetrics(&fontInfo, &iAscent, &iDescent, &iLineGap);
        float scale = stbtt_ScaleForPixelHeight(&fontInfo, pixelSize);
        atlas.ascent = iAscent * scale;
        atlas.descent = iDescent * scale;
        atlas.lineGap = iLineGap * scale;
        atlas.lineHeight = atlas.ascent - atlas.descent + atlas.lineGap;
    } else {
        atlas.ascent = pixelSize * 0.8f;
        atlas.descent = -pixelSize * 0.2f;
        atlas.lineGap = 0;
        atlas.lineHeight = pixelSize;
    }

    // Flip bitmap vertically for GL's bottom-up convention
    for (int y = 0; y < TEX_H / 2; y++) {
        int y2 = TEX_H - 1 - y;
        for (int x = 0; x < TEX_W; x++) {
            std::swap(bitmap[y * TEX_W + x], bitmap[y2 * TEX_W + x]);
        }
    }

    // Convert stbtt_packedchar to GfcBakedGlyph
    float invW = 1.0f / TEX_W;
    float invH = 1.0f / TEX_H;
    for (int i = 0; i < 96; i++) {
        GfcBakedGlyph &g = atlas.glyphs[i];
        g.x0 = pdata[i].xoff;
        g.y0 = pdata[i].yoff;
        g.x1 = pdata[i].xoff2;
        g.y1 = pdata[i].yoff2;
        g.u0 = pdata[i].x0 * invW;
        g.u1 = pdata[i].x1 * invW;
        g.v0 = 1.0f - pdata[i].y0 * invH;
        g.v1 = 1.0f - pdata[i].y1 * invH;
        g.xadvance = pdata[i].xadvance;
    }

    // Pure GL_ALPHA texture — shadow is drawn as a separate pass
    glGenTextures(1, &atlas.textureID);
    glBindTexture(GL_TEXTURE_2D, atlas.textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, TEX_W, TEX_H, 0,
                 GL_ALPHA, GL_UNSIGNED_BYTE, bitmap);
    glGenerateMipmap(GL_TEXTURE_2D);

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
    cache[key] = bakeAtlas(data, currentSize * dpiScale);
    return cache[key];
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// Emit quads for a line of text at (x, y) baseline. No GL state changes.
static void emitQuads(const GfcBakedGlyph *glyphs, const char *str, int len,
                      float x, float y, float offX, float offY) {
    glBegin(GL_QUADS);
    float cursorX = x + offX;
    float baseY = y + offY;
    for (int i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)str[i];
        if (ch < 32 || ch >= 128) continue;
        const GfcBakedGlyph &g = glyphs[ch - 32];

        float qx0 = floorf(cursorX + g.x0 + 0.5f);
        float qx1 = floorf(cursorX + g.x1 + 0.5f);
        float qy0 = floorf(baseY - g.y0 + 0.5f);
        float qy1 = floorf(baseY - g.y1 + 0.5f);

        glTexCoord2f(g.u0, g.v0); glVertex2f(qx0, qy0);
        glTexCoord2f(g.u1, g.v0); glVertex2f(qx1, qy0);
        glTexCoord2f(g.u1, g.v1); glVertex2f(qx1, qy1);
        glTexCoord2f(g.u0, g.v1); glVertex2f(qx0, qy1);

        cursorX += g.xadvance;
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

    // Pass 1: shadow (offset, dark color)
    if (shadowEnabled) {
        glColor4f(shadowR, shadowG, shadowB, shadowA * colorA);
        emitQuads(atlas.glyphs, str, len, x, y, shadowOffX, shadowOffY);
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
