#include "RenderBridge_qt.h"

#include <glad/glad.h>

#include "GLSystemInfo.h"
#include "../AppContext.h"
#include "../gfcplatemanager.h"
#include "../gfcSequence.h"      // extern gfcSettings sett;
#include "../gfcTextRenderer.h"  // gfc_gl_dpiscale()

namespace jefe::qt {

namespace {
// Paints a full-framebuffer checkerboard in place of the flat bgColor
// fill (General prefs → "Checkerboard background"). Immediate-mode /
// glOrtho to match the compatibility-profile drawing gfcplatemanager.cpp
// already uses elsewhere in the rendering chain.
// One checkerboard shade of channel value `b`: nudge away by a fixed delta
// (lighten if dark, darken if light), clamped to [0,1].
inline float checkerShade(float b, bool light) {
    const float d = 0.06f;
    float lo = b - d, hi = b + d;
    if (b < d)        { lo = b; hi = b + 2*d; }
    if (b > 1.0f - d) { hi = b; lo = b - 2*d; }
    lo = lo < 0 ? 0 : lo;  hi = hi > 1 ? 1 : hi;
    return light ? hi : lo;
}

void drawCheckerboardBackground(int wPx, int hPx) {
    glClear(GL_DEPTH_BUFFER_BIT);
    const float r = sett.bgColorR, g = sett.bgColorG, b = sett.bgColorB;

    const int cell = int(24 * gfc_gl_dpiscale());   // ~24 logical px per cell
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, wPx, 0, hPx, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
    glDisable(GL_TEXTURE_2D); glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glDisable(GL_BLEND);
    glBegin(GL_QUADS);
    for (int y = 0; y < hPx; y += cell) {
        for (int x = 0; x < wPx; x += cell) {
            const bool light = ((x / cell) + (y / cell)) & 1;
            glColor3f(checkerShade(r, light), checkerShade(g, light), checkerShade(b, light));
            const float x1 = float(x), y1 = float(y);
            const float x2 = float(x + cell), y2 = float(y + cell);
            glVertex2f(x1,y1); glVertex2f(x2,y1); glVertex2f(x2,y2); glVertex2f(x1,y2);
        }
    }
    glEnd();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
}
}  // namespace

RenderBridge_Qt::RenderBridge_Qt()  = default;
RenderBridge_Qt::~RenderBridge_Qt() = default;

void RenderBridge_Qt::onGLInit() {
    // GLAD is loaded by GlViewport_Qt before the first listener call.
    // The plate manager is initialized by MainWindow_Qt via the
    // SequenceLoadBridge before this fires.
    //
    // Capture GL system specs (version, extensions, max sizes) here —
    // we have a current GL context. The Help → System Specs dialog
    // reads the snapshot rather than re-querying, since glGetString
    // requires the context to be current.
    captureGLSystemInfo();
}

void RenderBridge_Qt::onResize(int newWidth, int newHeight) {
    width_     = newWidth;
    height_    = newHeight;
    sizeDirty_ = true;
}

void RenderBridge_Qt::onDraw() {
    if (width_ == 0 || height_ == 0) {
        return;
    }

    if (sett.bgCheckerboard) {
        drawCheckerboardBackground(width_, height_);   // fills the whole framebuffer
    } else {
        glClearColor(sett.bgColorR, sett.bgColorG, sett.bgColorB, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    AppContext::instance().plates().draw(width_, height_, sizeDirty_);
    sizeDirty_ = false;
}

}  // namespace jefe::qt
