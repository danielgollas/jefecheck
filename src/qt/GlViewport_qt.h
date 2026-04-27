// Qt implementation of IGLViewport, backed by QOpenGLWidget. Currently
// supports two modes:
//   1. Listener-driven (full app pipeline): forwards initializeGL/paintGL
//      to the registered IGLViewportListener.
//   2. Standalone image preview: when no listener is set, paintGL falls
//      back to GlImageRenderer to display whatever was last uploaded via
//      setImage(). This is what the Qt build uses today while the rest of
//      the rendering pipeline is still on FLTK.
//
// Drag-and-drop: accepts file URL drops anywhere on the viewport and emits
// fileDropped(path). MainWindow_Qt connects that signal to the OIIO load
// path and pushes pixels back through setImage().
#ifndef GLVIEWPORT_QT_H
#define GLVIEWPORT_QT_H

#include "ui/IGLViewport.h"
#include "GlImageRenderer_qt.h"

#include <QOpenGLWidget>
#include <QString>

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

    // Upload BGRA8 pixels (row 0 = top) and request a repaint. Safe to
    // call from the UI thread; we makeCurrent/doneCurrent around the
    // upload so the caller doesn't need a context.
    void setImage(const void* bgra8Pixels, int w, int h);

signals:
    void fileDropped(const QString& path);

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

    // Drag-and-drop for image files.
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    jefe::ui::IGLViewportListener* listener_ = nullptr;
    bool gladLoaded_ = false;

    GlImageRenderer renderer_;
    // Pending upload buffered until we have a current GL context.
    std::vector<unsigned char> pendingPixels_;
    int pendingW_ = 0;
    int pendingH_ = 0;
};

#endif
