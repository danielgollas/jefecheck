// Qt implementation of IGLViewport. Pure listener relay — paintGL,
// initializeGL, and resizeGL are forwarded to the registered
// IGLViewportListener (typically RenderBridge_Qt). The widget itself
// holds no rendering state.
#include "GlViewport_qt.h"
#include "SequenceLoadBridge_qt.h"

// FRAMING*_ID constants for layout shortcuts. UIConstants.h has no
// FLTK dependencies, so it's safe to pull into the Qt TU directly.
#include "../UIConstants.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QUrl>
#include <QWheelEvent>

extern bool jefecheck_loadGladGL();

GlViewport_Qt::GlViewport_Qt(QWidget* parent)
    : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    setObjectName("viewport");
    setAccessibleName("Viewport");
    setAccessibleDescription("OpenGL plate viewport");
}

GlViewport_Qt::~GlViewport_Qt() = default;

void GlViewport_Qt::requestRedraw() {
    update();
}

void GlViewport_Qt::makeCurrent() {
    QOpenGLWidget::makeCurrent();
}

void GlViewport_Qt::swapBuffers() {
    // QOpenGLWidget swaps internally on paintGL completion.
}

int GlViewport_Qt::width() const {
    return QOpenGLWidget::width();
}

int GlViewport_Qt::height() const {
    return QOpenGLWidget::height();
}

float GlViewport_Qt::pixelsPerUnit() const {
    return static_cast<float>(devicePixelRatioF());
}

void GlViewport_Qt::setListener(jefe::ui::IGLViewportListener* listener) {
    listener_ = listener;
}

void GlViewport_Qt::setCursorVisible(bool visible) {
    setCursor(visible ? Qt::ArrowCursor : Qt::BlankCursor);
}

void GlViewport_Qt::initializeGL() {
    if (!gladLoaded_) {
        gladLoaded_ = jefecheck_loadGladGL();
    }
    if (listener_) listener_->onGLInit();
}

void GlViewport_Qt::resizeGL(int w, int h) {
    // Qt passes the size in logical pixels here. The QOpenGLWidget's
    // backing framebuffer is allocated at logical * devicePixelRatio
    // (2× on macOS Retina), so glViewport-bound rendering needs the
    // framebuffer dimensions — using the raw w/h would leave only the
    // bottom-left quarter of the FBO rendered.
    const float dpr = devicePixelRatioF();
    if (listener_) listener_->onResize(int(w * dpr), int(h * dpr));
}

void GlViewport_Qt::paintGL() {
    if (listener_) {
        listener_->onDraw();
    } else {
        // No listener attached — clear the framebuffer so we don't show
        // garbage from an uninitialized backing store.
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}

void GlViewport_Qt::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        lastMouseX_ = e->position().x();
        lastMouseY_ = e->position().y();
        // Hit-test which plate the click landed on so drag pan and
        // (in single mode) keyboard shortcuts target it. The current
        // viewport size matches what plateManager.draw saw last, so
        // the partitioning lines up.
        dragPlate_ = jefe::qt::plateAtViewportPos(
            int(lastMouseX_), int(lastMouseY_), width(), height());
        if (dragPlate_ >= 0) {
            // Sync the active-plate concept (used by plate-card highlight
            // and a couple of remaining keyboard paths) with the user's
            // most recent click.
            jefe::qt::setActivePlate(dragPlate_);
            emit plateStateChanged();
        }
    }
    if (listener_) listener_->onEvent(jefe::ui::EventType::Push);
}

void GlViewport_Qt::mouseReleaseEvent(QMouseEvent*) {
    dragPlate_ = -1;
    if (listener_) listener_->onEvent(jefe::ui::EventType::Release);
}

void GlViewport_Qt::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton && dragPlate_ >= 0) {
        // FLTK's GlViewport pans by (prevX - eventX, prevY - eventY) so
        // dragging right shifts the plate left. Match that sign so the
        // mouse behaves the same in both backends. Multiply by dpr —
        // gfcPlate's pan is in world-space units which are now in
        // framebuffer pixels, but Qt mouse positions are logical, so
        // a 1-px drag would otherwise move the image only 1/dpr px on
        // Retina.
        const float dpr = devicePixelRatioF();
        const float dx = (lastMouseX_ - float(e->position().x())) * dpr;
        const float dy = (lastMouseY_ - float(e->position().y())) * dpr;
        jefe::qt::panPlate(dragPlate_, dx, dy);
        update();
        emit plateStateChanged();
        lastMouseX_ = e->position().x();
        lastMouseY_ = e->position().y();
    }
    auto type = (e->buttons() == Qt::NoButton)
        ? jefe::ui::EventType::Move
        : jefe::ui::EventType::Drag;
    if (listener_) listener_->onEvent(type);
}

void GlViewport_Qt::wheelEvent(QWheelEvent* e) {
    // macOS trackpads report pixelDelta (smooth scroll); discrete-notch
    // mice report angleDelta (eighths of a degree, 120 per notch).
    // Convert both to FLTK-style "wheelDeltaY" units (~±1 per notch)
    // before scaling by zoomSpeed.
    float deltaY = 0.0f;
    if (!e->pixelDelta().isNull()) {
        deltaY = static_cast<float>(e->pixelDelta().y()) / 120.0f;
    } else if (!e->angleDelta().isNull()) {
        deltaY = static_cast<float>(e->angleDelta().y()) / 120.0f;
    }
    if (deltaY != 0.0f) {
        // Wheel zooms whichever plate the cursor is over, regardless of
        // active state. Click sets active separately.
        const int plate = jefe::qt::plateAtViewportPos(
            int(e->position().x()), int(e->position().y()), width(), height());
        // 0.1 matches FLTK's default zoomSpeed for the un-shifted wheel.
        jefe::qt::zoomPlate(plate, deltaY * 0.1f);
        update();
        emit plateStateChanged();
    }
    if (listener_) listener_->onEvent(jefe::ui::EventType::Wheel);
}

void GlViewport_Qt::keyPressEvent(QKeyEvent* e) {
    // Plate-control shortcuts. Any unhandled key falls through to the
    // listener and Qt's default propagation so menu mnemonics, dialog
    // accelerators, etc. still work.
    const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
    const bool ctrl  = e->modifiers().testFlag(Qt::ControlModifier);
    const bool alt   = e->modifiers().testFlag(Qt::AltModifier);

    auto handled = [&]() {
        update();
        emit plateStateChanged();
        if (listener_) listener_->onEvent(jefe::ui::EventType::KeyDown);
    };

    switch (e->key()) {
        // Layout cycling: Ctrl+1..4 → single / horiz-split / vert-split / quad.
        case Qt::Key_1:
            if (ctrl) { jefe::qt::setFramingMode(FRAMINGSINGLE_ID);     handled(); return; }
            break;
        case Qt::Key_2:
            if (ctrl) { jefe::qt::setFramingMode(FRAMINGDOUBLE_ID);     handled(); return; }
            break;
        case Qt::Key_3:
            if (ctrl) { jefe::qt::setFramingMode(FRAMINGDOUBLEVERT_ID); handled(); return; }
            break;
        case Qt::Key_4:
            if (ctrl) { jefe::qt::setFramingMode(FRAMINGQUAD_ID);       handled(); return; }
            break;

        // F / Shift+F (fit), H / Shift+H (flop), V / Shift+V (flip),
        // T / Alt+T (text mode) all live as ApplicationShortcut bindings
        // in MainWindow_Qt — they need to fire regardless of whether the
        // viewport, a dock widget, a spinbox, or the menu bar has focus.
        // Keeping a duplicate handler here would either steal focus-
        // dependent keystrokes from the focused input widget OR fire
        // both the QShortcut and the viewport handler.

        // Track cycling on active plate (matches FLTK's Up/Down handler).
        case Qt::Key_Up:
            if (!ctrl && !alt) {
                jefe::qt::cycleTrackOnActivePlate(-1);
                handled();
                return;
            }
            break;
        case Qt::Key_Down:
            if (!ctrl && !alt) {
                jefe::qt::cycleTrackOnActivePlate(+1);
                handled();
                return;
            }
            break;

        // Playback. Space pauses; arrows step a frame. Frame stepping
        // is a no-op until the Qt build runs the playback loop, but
        // wiring it here keeps the surface symmetric with FLTK.
        case Qt::Key_Space:
            if (!ctrl && !alt) {
                jefe::qt::pausePlayback();
                handled();
                return;
            }
            break;
        case Qt::Key_Left:
            if (!ctrl && !alt) {
                jefe::qt::stepFrame(-1);
                handled();
                return;
            }
            break;
        case Qt::Key_Right:
            if (!ctrl && !alt) {
                jefe::qt::stepFrame(+1);
                handled();
                return;
            }
            break;

        default:
            break;
    }

    // Unhandled — pass through to listener / parent.
    if (listener_) listener_->onEvent(jefe::ui::EventType::KeyDown);
    QOpenGLWidget::keyPressEvent(e);
}

void GlViewport_Qt::keyReleaseEvent(QKeyEvent*) {
    if (listener_) listener_->onEvent(jefe::ui::EventType::KeyUp);
}

void GlViewport_Qt::enterEvent(QEnterEvent*) {
    if (listener_) listener_->onEvent(jefe::ui::EventType::Enter);
}

void GlViewport_Qt::leaveEvent(QEvent*) {
    if (listener_) listener_->onEvent(jefe::ui::EventType::Leave);
}

void GlViewport_Qt::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) {
        for (const QUrl& u : e->mimeData()->urls()) {
            if (u.isLocalFile()) {
                e->acceptProposedAction();
                return;
            }
        }
    }
    e->ignore();
}

void GlViewport_Qt::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
    } else {
        e->ignore();
    }
}

void GlViewport_Qt::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasUrls()) {
        e->ignore();
        return;
    }
    for (const QUrl& u : e->mimeData()->urls()) {
        if (u.isLocalFile()) {
            emit fileDropped(u.toLocalFile());
            e->acceptProposedAction();
            return;
        }
    }
    e->ignore();
}
