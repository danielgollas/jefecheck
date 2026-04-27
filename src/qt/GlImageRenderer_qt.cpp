// Implementation lives in its own TU to keep glad isolated from Qt's
// OpenGL widget headers (see header comment for the macOS collision).
#include "GlImageRenderer_qt.h"

#include <glad/glad.h>

GlImageRenderer::GlImageRenderer() = default;

GlImageRenderer::~GlImageRenderer() {
    // Don't issue GL calls from a destructor — the context that owned this
    // texture may already be gone. The caller is responsible for invoking
    // releaseGL() while the context is current.
}

void GlImageRenderer::releaseGL() {
    if (texture_) {
        GLuint t = texture_;
        glDeleteTextures(1, &t);
        texture_ = 0;
    }
    imgW_ = imgH_ = 0;
}

void GlImageRenderer::uploadBGRA8(const void* pixels, int w, int h) {
    if (!pixels || w <= 0 || h <= 0) return;

    if (!texture_) {
        GLuint t = 0;
        glGenTextures(1, &t);
        texture_ = t;
    }

    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, texture_);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, w, h, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, pixels);

    imgW_ = w;
    imgH_ = h;
}

void GlImageRenderer::render(int vpW, int vpH) {
    if (vpW <= 0 || vpH <= 0) return;

    glViewport(0, 0, vpW, vpH);
    glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!texture_ || imgW_ <= 0 || imgH_ <= 0) return;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // Pixel-space ortho, y up.
    glOrtho(0.0, (double)vpW, 0.0, (double)vpH, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Aspect-fit the image inside the viewport.
    const float imgAR = (float)imgW_ / (float)imgH_;
    const float vpAR  = (float)vpW   / (float)vpH;
    float qw, qh;
    if (imgAR > vpAR) {
        qw = (float)vpW;
        qh = qw / imgAR;
    } else {
        qh = (float)vpH;
        qw = qh * imgAR;
    }
    const float qx = ((float)vpW - qw) * 0.5f;
    const float qy = ((float)vpH - qh) * 0.5f;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_RECTANGLE_ARB);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, texture_);
    glColor3f(1.0f, 1.0f, 1.0f);

    // OIIO writes top-down (row 0 = top of image). Mapping (0,0) of the
    // texture rect to the *top* of the screen quad displays the image
    // right-side-up under glOrtho with y-up.
    const float fW = (float)imgW_;
    const float fH = (float)imgH_;
    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(qx,      qy + qh);
        glTexCoord2f(fW,   0.0f); glVertex2f(qx + qw, qy + qh);
        glTexCoord2f(fW,   fH);   glVertex2f(qx + qw, qy);
        glTexCoord2f(0.0f, fH);   glVertex2f(qx,      qy);
    glEnd();

    glDisable(GL_TEXTURE_RECTANGLE_ARB);
}
