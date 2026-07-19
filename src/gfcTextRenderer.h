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

// Default text-rendering preferences — the single source shared by the
// GfcTextRenderer constructor and the Preferences → Text page / applyTextPrefs
// QSettings fallbacks, so a first run (no Text/* keys) causes zero visual
// change and the sites can't drift apart.
namespace GfcTextDefaults {
    inline constexpr float kLabelSize     = 12.0f;   // on-plate label size (px)
    inline constexpr float kColorR = 1.0f, kColorG = 1.0f, kColorB = 1.0f;  // white
    inline constexpr int   kHintMode      = 0;       // HINT_LIGHT
    inline constexpr bool  kFilterNearest = true;    // GL_NEAREST
    inline constexpr float kGamma         = 0.65f;
    inline constexpr bool  kShadowEnabled = true;
    inline constexpr float kShadowOffX = 1.0f, kShadowOffY = -1.0f;
    inline constexpr float kShadowBlur    = 0.0f;
    inline constexpr float kShadowColorR = 0.0f, kShadowColorG = 0.0f,
                           kShadowColorB = 0.0f, kShadowColorA = 0.5f;
}

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
    // Physical-pixels-per-logical-pixel (2.0 on Retina). gfc_gl_measure
    // returns LOGICAL pixels; callers laying out in a physical-pixel ortho
    // (e.g. the viewport feedback overlay) multiply by this to convert.
    float getDPIScale() const { return dpiScale; }

    // Color
    void setColor(float r, float g, float b, float a);

    // Plate-label text style — the on-plate filename/info overlay drawn by
    // gfcPlate::drawText(). Set from the Preferences → Text page so its Size and
    // Color controls actually drive visible text (the transient setSize/setColor
    // above are overwritten every frame by each draw site, so they can't).
    void setLabelSize(float pixelSize) { labelSize_ = pixelSize; }
    void setLabelColor(float r, float g, float b) { labelR_ = r; labelG_ = g; labelB_ = b; }
    float labelSize() const { return labelSize_; }
    void labelColor(float& r, float& g, float& b) const { r = labelR_; g = labelG_; b = labelB_; }

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

    // Texture IDs orphaned by a cache invalidation that happened with no GL
    // context current (e.g. setHintMode/setGamma called live from the
    // Preferences dialog). Deleted lazily at the top of getAtlas(), which is
    // only reached from drawLine() during paintGL where the context is
    // guaranteed current.
    std::vector<GLuint> pendingTexDeletes_;

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

    // Plate-label style (see setLabelSize/setLabelColor).
    float labelSize_ = GfcTextDefaults::kLabelSize;
    float labelR_ = GfcTextDefaults::kColorR;
    float labelG_ = GfcTextDefaults::kColorG;
    float labelB_ = GfcTextDefaults::kColorB;

    // Get or create atlas for current size/DPI
    GfcFontAtlas& getAtlas();

    // Move all cached texture IDs into pendingTexDeletes_ and clear the
    // caches, WITHOUT calling glDeleteTextures (may run with no GL context
    // current). Actual deletion happens lazily in getAtlas().
    void queueAtlasInvalidation();

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
// Physical/logical pixel ratio of the text renderer (2.0 on Retina).
float gfc_gl_dpiscale();

#endif
