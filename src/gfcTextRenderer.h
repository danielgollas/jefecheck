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
    GfcFontAtlas bakeAtlas(const std::vector<unsigned char> &data, float pixelSize);

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
