// Qt implementation of IGLViewport. Pure listener relay — paintGL,
// initializeGL, and resizeGL are forwarded to the registered
// IGLViewportListener (typically RenderBridge_Qt). The widget itself
// holds no rendering state.
#include "GlViewport_qt.h"
#include "SequenceLoadBridge_qt.h"

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
    if (listener_) listener_->onResize(w, h);
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
        // Anchor for the drag delta we'll feed to panActivePlate.
        lastMouseX_ = e->position().x();
        lastMouseY_ = e->position().y();
    }
    if (listener_) listener_->onEvent(jefe::ui::EventType::Push);
}

void GlViewport_Qt::mouseReleaseEvent(QMouseEvent*) {
    if (listener_) listener_->onEvent(jefe::ui::EventType::Release);
}

void GlViewport_Qt::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) {
        // FLTK's GlViewport pans by (prevX - eventX, prevY - eventY) so
        // dragging right shifts the plate left. Match that sign here so
        // the keyboard/mouse shortcuts behave the same in both backends.
        const float dx = static_cast<float>(lastMouseX_ - e->position().x());
        const float dy = static_cast<float>(lastMouseY_ - e->position().y());
        jefe::qt::panActivePlate(dx, dy);
        update();
        lastMouseX_ = e->position().x();
        lastMouseY_ = e->position().y();
    }
    auto type = (e->buttons() == Qt::NoButton)
        ? jefe::ui::EventType::Move
        : jefe::ui::EventType::Drag;
    if (listener_) listener_->onEvent(type);
}

void GlViewport_Qt::wheelEvent(QWheelEvent* e) {
    // angleDelta() is in eighths of a degree. FLTK's wheelDeltaY is
    // typically ±1 per notch; divide by 120 to get the same units.
    const int notches = e->angleDelta().y() / 120;
    if (notches != 0) {
        // 0.1 matches FLTK's default zoomSpeed for the un-shifted wheel.
        jefe::qt::zoomActivePlate(static_cast<float>(notches) * 0.1f);
        update();
    }
    if (listener_) listener_->onEvent(jefe::ui::EventType::Wheel);
}

void GlViewport_Qt::keyPressEvent(QKeyEvent*) {
    if (listener_) listener_->onEvent(jefe::ui::EventType::KeyDown);
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
