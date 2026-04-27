#include "RenderBridge_qt.h"

#include <glad/glad.h>

#include "../gfcplatemanager.h"

extern gfcPlateManager plateManager;

namespace jefe::qt {

RenderBridge_Qt::RenderBridge_Qt()  = default;
RenderBridge_Qt::~RenderBridge_Qt() = default;

void RenderBridge_Qt::onGLInit() {
    // GLAD is loaded by GlViewport_Qt before the first listener call.
    // The plate manager is initialized lazily — its initializeWidgets
    // path is FLTK-only and gated out, so the Qt build relies on plates
    // being added later (PR-10) when sequences are loaded.
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
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    plateManager.draw(width_, height_, sizeDirty_);
    sizeDirty_ = false;
}

}  // namespace jefe::qt
