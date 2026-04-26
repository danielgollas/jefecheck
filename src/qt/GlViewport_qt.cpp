// Qt skeleton for the OpenGL viewport. SKELETON — most bodies are empty.
#include "GlViewport_qt.h"

#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>

#include <glad/glad.h>

GlViewport_Qt::GlViewport_Qt(QWidget* parent)
    : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
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
        gladLoaded_ = gladLoadGL() != 0;
    }
    if (listener_) listener_->onGLInit();
}

void GlViewport_Qt::resizeGL(int w, int h) {
    if (listener_) listener_->onResize(w, h);
}

void GlViewport_Qt::paintGL() {
    if (listener_) listener_->onDraw();
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
