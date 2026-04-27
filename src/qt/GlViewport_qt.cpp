// Qt implementation of IGLViewport. Holds an embedded GlImageRenderer for
// the standalone "show me an image" path while listener-driven rendering
// is wired up incrementally.
#include "GlViewport_qt.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QUrl>
#include <QWheelEvent>

#include <cstring>

extern bool jefecheck_loadGladGL();

GlViewport_Qt::GlViewport_Qt(QWidget* parent)
    : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
}

GlViewport_Qt::~GlViewport_Qt() {
    // Best-effort GL resource cleanup. If the widget is destroyed before
    // its context, makeCurrent() lets us free the texture cleanly.
    if (renderer_.hasImage()) {
        makeCurrent();
        renderer_.releaseGL();
        doneCurrent();
    }
}

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

void GlViewport_Qt::setImage(const void* bgra8Pixels, int w, int h) {
    if (!bgra8Pixels || w <= 0 || h <= 0) return;

    const size_t bytes = (size_t)w * (size_t)h * 4u;
    pendingPixels_.resize(bytes);
    std::memcpy(pendingPixels_.data(), bgra8Pixels, bytes);
    pendingW_ = w;
    pendingH_ = h;

    if (gladLoaded_) {
        // Context already initialized: upload immediately.
        makeCurrent();
        renderer_.uploadBGRA8(pendingPixels_.data(), pendingW_, pendingH_);
        doneCurrent();
        pendingPixels_.clear();
    }
    update();
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
        return;
    }

    // Standalone path: drain any pending upload, then render the image.
    if (!pendingPixels_.empty()) {
        renderer_.uploadBGRA8(pendingPixels_.data(), pendingW_, pendingH_);
        pendingPixels_.clear();
    }

    const int dpr = (int)devicePixelRatioF();
    renderer_.render(QOpenGLWidget::width() * dpr,
                     QOpenGLWidget::height() * dpr);
}

void GlViewport_Qt::mousePressEvent(QMouseEvent*) {
    if (listener_) listener_->onEvent(jefe::ui::EventType::Push);
}

void GlViewport_Qt::mouseReleaseEvent(QMouseEvent*) {
    if (listener_) listener_->onEvent(jefe::ui::EventType::Release);
}

void GlViewport_Qt::mouseMoveEvent(QMouseEvent* e) {
    auto type = (e->buttons() == Qt::NoButton)
        ? jefe::ui::EventType::Move
        : jefe::ui::EventType::Drag;
    if (listener_) listener_->onEvent(type);
}

void GlViewport_Qt::wheelEvent(QWheelEvent*) {
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
