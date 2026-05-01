#include "RenderBridge_qt.h"

#include <glad/glad.h>

#include "GLSystemInfo.h"
#include "../gfcplatemanager.h"

extern gfcPlateManager plateManager;

namespace jefe::qt {

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
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    plateManager.draw(width_, height_, sizeDirty_);
    sizeDirty_ = false;
}

}  // namespace jefe::qt
