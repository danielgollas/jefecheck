// Qt skeleton for the OpenGL viewport. Implements IGLViewport via QOpenGLWidget.
//
// SKELETON ONLY. The real port needs to:
//   - Forward Qt mouse/key/wheel/enter/leave events through IEventSystem_Qt
//   - Wire up paintGL → listener->onDraw() with the GL context current
//   - Handle Retina/HiDPI via devicePixelRatioF()
//   - Initialize GLAD on first paintGL (or initializeGL)
//
// See docs/MIGRATION.md for the rest of the work.
#ifndef GLVIEWPORT_QT_H
#define GLVIEWPORT_QT_H

#include "ui/IGLViewport.h"
#include <QOpenGLWidget>

class GlViewport_Qt : public QOpenGLWidget, public jefe::ui::IGLViewport {
    Q_OBJECT

public:
    explicit GlViewport_Qt(QWidget* parent = nullptr);
    ~GlViewport_Qt() override;

    // IGLViewport
    void requestRedraw() override;
    void makeCurrent() override;
    void swapBuffers() override;
    int width() const override;
    int height() const override;
    float pixelsPerUnit() const override;
    void setListener(jefe::ui::IGLViewportListener* listener) override;
    void setCursorVisible(bool visible) override;

protected:
    // QOpenGLWidget hooks
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // Event forwarding (SKELETON — todo)
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    jefe::ui::IGLViewportListener* listener_ = nullptr;
    bool gladLoaded_ = false;
};

#endif
