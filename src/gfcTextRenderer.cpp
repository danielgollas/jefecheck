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

// ---------------------------------------------------------------------------
// Atlas baking
// ---------------------------------------------------------------------------

void GfcTextRenderer::bakeShadow(unsigned char *bitmap, int w, int h,
                                  int shadowOffX, int shadowOffY, int blurRadius)
{
    // Create a copy for the shadow layer
    std::vector<unsigned char> shadow(w * h, 0);

    // Offset copy of glyph bitmap into shadow buffer
    for (int y = 0; y < h; y++) {
        int sy = y - shadowOffY;
        if (sy < 0 || sy >= h) continue;
        for (int x = 0; x < w; x++) {
            int sx = x - shadowOffX;
            if (sx < 0 || sx >= w) continue;
            shadow[y * w + x] = bitmap[sy * w + sx];
        }
    }

    // Box blur the shadow (simple 2-pass separable blur)
    if (blurRadius > 0) {
        std::vector<unsigned char> temp(w * h, 0);
        int kernelSize = blurRadius * 2 + 1;

        // Horizontal pass
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int sum = 0;
                for (int k = -blurRadius; k <= blurRadius; k++) {
                    int sx = std::clamp(x + k, 0, w - 1);
                    sum += shadow[y * w + sx];
                }
                temp[y * w + x] = (unsigned char)(sum / kernelSize);
            }
        }

        // Vertical pass
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int sum = 0;
                for (int k = -blurRadius; k <= blurRadius; k++) {
                    int sy = std::clamp(y + k, 0, h - 1);
                    sum += temp[sy * w + x];
                }
                shadow[y * w + x] = (unsigned char)(sum / kernelSize);
            }
        }
    }

    // Composite: shadow underneath, glyph on top
    // Result stored back in bitmap
    // We'll also need a separate alpha channel, but since we're using
    // GL_LUMINANCE_ALPHA, we need to create a 2-channel texture later.
    // For now, just max the shadow into the bitmap so we can use it as alpha.
    for (int i = 0; i < w * h; i++) {
        bitmap[i] = std::max(bitmap[i], shadow[i]);
    }
}

GfcFontAtlas GfcTextRenderer::bakeAtlas(const std::vector<unsigned char> &data, float pixelSize) {
    GfcFontAtlas atlas;
    atlas.texWidth = 1024;
    atlas.texHeight = 1024;
    atlas.pixelSize = pixelSize;
    atlas.textureID = 0;
    atlas.valid = false;

    stbtt_bakedchar cdata[96];
    unsigned char *bitmap = new unsigned char[1024 * 1024];
    memset(bitmap, 0, 1024 * 1024);

    int result = stbtt_BakeFontBitmap(data.data(), 0, pixelSize,
                                       bitmap, 1024, 1024, 32, 96, cdata);
    if (result <= 0 && result != 0) {
        // result is negative if not all chars fit, but we still get partial results
        // Only truly fail if we got nothing
    }

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

    // Convert stbtt_bakedchar to GfcBakedGlyph
    for (int i = 0; i < 96; i++) {
        GfcBakedGlyph &g = atlas.glyphs[i];
        g.x0 = cdata[i].xoff;
        g.y0 = cdata[i].yoff;
        g.x1 = cdata[i].xoff + (cdata[i].x1 - cdata[i].x0);
        g.y1 = cdata[i].yoff + (cdata[i].y1 - cdata[i].y0);
        g.u0 = cdata[i].x0 / 1024.0f;
        g.v0 = cdata[i].y0 / 1024.0f;
        g.u1 = cdata[i].x1 / 1024.0f;
        g.v1 = cdata[i].y1 / 1024.0f;
        g.xadvance = cdata[i].xadvance;
    }

    // Bake shadow into bitmap
    if (shadowEnabled) {
        bakeShadow(bitmap, 1024, 1024, 1, -1, 2);
    }

    // Create GL_LUMINANCE_ALPHA texture
    // For each pixel: luminance = 255 (white for glyph tinting), alpha = bitmap value
    // But we want shadow to be dark, so we need two channels:
    // We'll use a different approach: RGBA texture where:
    //   - Original glyph pixels get full white + alpha from bitmap (before shadow)
    //   - Shadow pixels get black + alpha from shadow
    // Actually, simpler: use GL_ALPHA texture and let glColor handle the tinting.
    // The shadow baked into the alpha makes it render as the current color.
    // For proper shadow (dark behind light text), we need two passes or a 2-channel approach.
    //
    // Simplest working approach: just use GL_ALPHA texture.
    // Shadow will be same color as text but softer. This is acceptable for v1.
    // The manual shadow in the old code was also same-color (dark text behind light text
    // via two draw calls). Our baked shadow achieves similar effect when text is light on dark.

    glGenTextures(1, &atlas.textureID);
    glBindTexture(GL_TEXTURE_2D, atlas.textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 1024, 1024, 0,
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
        // Validate texture still exists
        if (it->second.valid && glIsTexture(it->second.textureID))
            return it->second;
        // Invalid — rebake
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

void GfcTextRenderer::drawLine(const char *str, int len, float x, float y) {
    if (!fontLoaded) return;
    GfcFontAtlas &atlas = getAtlas();
    if (!atlas.valid) return;

    glPushAttrib(GL_ALL_ATTRIB_BITS);

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    // Disable GL_TEXTURE_RECTANGLE_ARB which may be active from image rendering
    glDisable(GL_TEXTURE_RECTANGLE_ARB);

    glBindTexture(GL_TEXTURE_2D, atlas.textureID);
    glColor4f(colorR, colorG, colorB, colorA);

    glBegin(GL_QUADS);
    float cursorX = x;
    for (int i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)str[i];
        if (ch < 32 || ch >= 128) continue;
        const GfcBakedGlyph &g = atlas.glyphs[ch - 32];

        float qx0 = cursorX + g.x0 / dpiScale;
        float qy0 = y + g.y0 / dpiScale;
        float qx1 = cursorX + g.x1 / dpiScale;
        float qy1 = y + g.y1 / dpiScale;

        glTexCoord2f(g.u0, g.v0); glVertex2f(qx0, qy0);
        glTexCoord2f(g.u1, g.v0); glVertex2f(qx1, qy0);
        glTexCoord2f(g.u1, g.v1); glVertex2f(qx1, qy1);
        glTexCoord2f(g.u0, g.v1); glVertex2f(qx0, qy1);

        cursorX += g.xadvance / dpiScale;
    }
    glEnd();

    glPopAttrib();
}

void GfcTextRenderer::draw(const char *str, float x, float y) {
    if (!str || !*str || !fontLoaded) return;
    GfcFontAtlas &atlas = getAtlas();
    // Position y at baseline: caller passes top-left, we add ascent
    drawLine(str, (int)strlen(str), x, y + atlas.ascent / dpiScale);
}

void GfcTextRenderer::draw(const char *str, float x, float y, float w, float h, int align) {
    if (!str || !*str || !fontLoaded) return;

    auto lines = wrapText(str, (align & GFC_ALIGN_WRAP) ? w : 0.0f);
    if (lines.empty()) return;

    float lh = lineHeight();
    float totalHeight = lines.size() * lh;

    // Vertical alignment
    float startY;
    if (align & GFC_ALIGN_BOTTOM) {
        startY = y + h - totalHeight;
    } else {
        startY = y; // top
    }

    GfcFontAtlas &atlas = getAtlas();

    for (size_t i = 0; i < lines.size(); i++) {
        float lineX = x;
        if (align & GFC_ALIGN_CENTER) {
            lineX = x + (w - lines[i].width) / 2.0f;
        }
        float lineY = startY + i * lh + atlas.ascent / dpiScale;
        drawLine(lines[i].start, lines[i].length, lineX, lineY);
    }
}

void GfcTextRenderer::draw3D(const char *str, float x, float y, float z) {
    if (!str || !*str || !fontLoaded) return;

    // Project 3D point to 2D screen coordinates
    GLdouble modelview[16], projection[16];
    GLint viewport[4];
    GLdouble sx, sy, sz;
    glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
    glGetDoublev(GL_PROJECTION_MATRIX, projection);
    glGetIntegerv(GL_VIEWPORT, viewport);
    gluProject(x, y, z, modelview, projection, viewport, &sx, &sy, &sz);

    // Save current matrices, set up pixel-exact ortho for this viewport
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(viewport[0], viewport[0] + viewport[2],
            viewport[1], viewport[1] + viewport[3], -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    drawLine(str, (int)strlen(str), (float)sx, (float)sy);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
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
            // End current line
            if (str > lineStart) {
                TextLine tl;
                tl.start = lineStart;
                tl.length = (int)(str - lineStart);
                tl.width = lineWidth + wordWidth;
                lines.push_back(tl);
            } else {
                // Empty line
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
            charW = atlas.glyphs[ch - 32].xadvance / dpiScale;

        if (ch == ' ') {
            lineWidth += wordWidth + charW;
            wordWidth = 0;
            str++;
            wordStart = str;
            continue;
        }

        wordWidth += charW;

        // Check if we need to wrap
        if (maxWidth > 0 && lineWidth + wordWidth > maxWidth && lineWidth > 0) {
            // Wrap before this word
            TextLine tl;
            tl.start = lineStart;
            tl.length = (int)(wordStart - lineStart);
            // Trim trailing space
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

static bool s_currentBold = false;
static int s_currentSize = 12;

void gfc_gl_font(int face, int size) {
    // FL_HELVETICA=0, FL_BOLD=1, FL_ITALIC=2
    s_currentBold = (face & 1) != 0;
    s_currentSize = size;
    textRenderer().setBold(s_currentBold);
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
