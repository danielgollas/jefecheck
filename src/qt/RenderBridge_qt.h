// IGLViewportListener that drives the JefeCheck rendering chain. Each
// paintGL call invokes plateManager.draw() (and friends) with the current
// viewport size. Lives behind the same interface FLTK's GlViewport uses,
// so swapping backends is purely a matter of which listener owns the
// chain.
//
// Until PR-10 wires content loading through gfcSequence/gfcPlate, the
// plate stack is empty and the bridge's draw call is a no-op visually —
// the screen clears to the configured background color and stays there.
#ifndef JEFECHECK_QT_RENDER_BRIDGE_H
#define JEFECHECK_QT_RENDER_BRIDGE_H

#include "ui/IGLViewport.h"

namespace jefe::qt {

class RenderBridge_Qt : public jefe::ui::IGLViewportListener {
public:
    RenderBridge_Qt();
    ~RenderBridge_Qt() override;

    void onGLInit() override;
    void onDraw() override;
    void onResize(int newWidth, int newHeight) override;

private:
    int  width_     = 0;
    int  height_    = 0;
    bool sizeDirty_ = true;
};

}  // namespace jefe::qt

#endif
