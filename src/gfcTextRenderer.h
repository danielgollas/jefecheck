#ifndef GFCTEXTRENDERER_H
#define GFCTEXTRENDERER_H

#include <glad/glad.h>
#include <string>
#include <map>
#include <vector>

// Alignment flags — must match FLTK's Fl_Align values since call sites pass FL_ALIGN_*
#define GFC_ALIGN_CENTER  0x0000
#define GFC_ALIGN_TOP     0x0001
#define GFC_ALIGN_BOTTOM  0x0002
#define GFC_ALIGN_LEFT    0x0004
#define GFC_ALIGN_RIGHT   0x0008
#define GFC_ALIGN_INSIDE  0x0010
#define GFC_ALIGN_WRAP    0x0080

struct GfcBakedGlyph {
    float x0, y0, x1, y1;   // quad position offset from cursor (in pixels)
    float u0, v0, u1, v1;   // texture coordinates (GL convention: v=0 at bottom)
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

    // Shadow (drawn as a second pass with offset)
    void setShadowEnabled(bool enabled);
    void setShadowOffset(float x, float y);
    void setShadowColor(float r, float g, float b, float a);
    void setShadowBlur(float radius);

    // Rendering options
    enum HintMode { HINT_LIGHT = 0, HINT_NORMAL = 1, HINT_AUTO = 2 };
    void setHintMode(HintMode mode);
    void setFilterNearest(bool nearest);  // true=GL_NEAREST, false=GL_LINEAR
    void setGamma(float gamma);           // gamma correction for atlas (0.5-1.0)

    // Drawing — all coordinates are in the caller's GL coordinate space (Y-up)
    // draw(str, x, y): x,y is the baseline-left position
    void draw(const char *str, float x, float y);
    // draw(str, x, y, w, h, align): (x,y) is lower-left of bounding box, w/h is size
    void draw(const char *str, float x, float y, float w, float h, int align);
    // draw3D: project 3D point to screen, render in pixel-exact ortho
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
    float shadowOffX, shadowOffY;
    float shadowR, shadowG, shadowB, shadowA;
    float shadowBlurRadius;

    HintMode hintMode;
    bool filterNearest;
    float gammaValue;

    // Get or create atlas for current size/DPI
    GfcFontAtlas& getAtlas();

    // Bake a new atlas
    GfcFontAtlas bakeAtlas(const std::vector<unsigned char> &data, float pixelSize, float dpiScale);

    // Word wrapping
    struct TextLine {
        const char *start;
        int length;
        float width;
    };
    std::vector<TextLine> wrapText(const char *str, float maxWidth);

    // Draw a single line of text as quads (y is the baseline in Y-up GL coords)
    void drawLine(const char *str, int len, float x, float y);
};

// Global singleton
GfcTextRenderer& textRenderer();

// Enumerate TrueType fonts on the system. Returns pairs of (display name, file path).
std::vector<std::pair<std::string, std::string>> enumerateSystemFonts();

// FLTK-compatible wrapper functions
void gfc_gl_font(int face, int size);
void gfc_gl_draw(const char *str);
void gfc_gl_draw(const char *str, float x, float y);
void gfc_gl_draw(const char *str, int x, int y, int w, int h, int align);
float gfc_gl_height();
void gfc_gl_measure(const char *str, int &w, int &h, int wrap = 0);

// Suppress all gfc_gl_draw calls when true. The Qt viewport sets this
// during pan/zoom drags so paintGL skips the per-plate text overlays
// (labels, frame number, AOI corner readouts) that otherwise pile up
// glyph atlas lookups + per-glyph quad draws per frame. Cleared on
// mouseRelease so the labels reappear immediately after the drag.
void gfc_gl_set_suppressed(bool suppress);
bool gfc_gl_is_suppressed();

#endif
