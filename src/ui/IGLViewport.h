// Abstract OpenGL viewport. Replaces direct use of Fl_Gl_Window in JefeCheck.
// Backend implementations:
//   - src/GlViewport.cpp wraps Fl_Gl_Window (currently inherits — Phase 0D unwraps).
//   - src/qt/GlViewport_qt.cpp wraps QOpenGLWidget.
//
// The viewport owns no business logic. Application code subscribes via the
// Listener interface to react to draw/resize/event callbacks.
#ifndef JEFECHECK_UI_IGLVIEWPORT_H
#define JEFECHECK_UI_IGLVIEWPORT_H

#include "IEventSystem.h"

namespace jefe::ui {

// Application code implements this to receive viewport events.
// EventType is shared with IEventSystem (defined there).
class IGLViewportListener {
public:
    virtual ~IGLViewportListener() = default;

    // Called once after the GL context is current and GLAD is loaded.
    virtual void onGLInit() {}

    // Called each frame to render. The GL context is already current.
    virtual void onDraw() = 0;

    // Called when the widget is resized. New size is in pixels (logical units;
    // multiply by IGLViewport::pixelsPerUnit for backing-store dimensions).
    virtual void onResize(int newWidth, int newHeight) {}

    // Called for every UI event. Return true if handled (stops propagation).
    // Event payload comes from IEventSystem::instance().
    virtual bool onEvent(EventType /*type*/) { return false; }
};

class IGLViewport {
public:
    virtual ~IGLViewport() = default;

    // Mark dirty; the backend redraws on the next event-loop tick.
    virtual void requestRedraw() = 0;

    // Make this viewport's GL context current on the calling thread.
    virtual void makeCurrent() = 0;

    // Swap the front/back buffers (typically auto-handled, exposed for explicit control).
    virtual void swapBuffers() = 0;

    // Geometry in logical units (the units the rest of the UI is laid out in).
    virtual int width() const = 0;
    virtual int height() const = 0;

    // Backing-store scale factor. 1.0 on normal displays, 2.0 on Retina/HiDPI.
    // logical_pixel * pixelsPerUnit == backing_store_pixel
    virtual float pixelsPerUnit() const = 0;

    // Listener wiring. The viewport doesn't own the listener.
    virtual void setListener(IGLViewportListener* listener) = 0;

    // Cursor visibility (used during transparent overlays, fullscreen playback).
    virtual void setCursorVisible(bool visible) = 0;
};

}  // namespace jefe::ui

#endif
