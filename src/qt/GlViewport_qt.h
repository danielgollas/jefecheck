// Qt implementation of IGLViewport, backed by QOpenGLWidget. Forwards
// initializeGL / resizeGL / paintGL to a registered IGLViewportListener
// — the JefeCheck rendering chain (gfcPlateManager, gfcNetworkManager,
// etc.) plugs in via that listener interface (see RenderBridge_qt).
//
// Drag-and-drop: accepts file URL drops anywhere on the viewport and
// emits fileDropped(path). The owning MainWindow handles loading and
// hands the result to gfcSequence / gfcPlate (PR-10+).
#ifndef GLVIEWPORT_QT_H
#define GLVIEWPORT_QT_H

#include "ui/IGLViewport.h"

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

signals:
    void fileDropped(const QString& path);

    // Emitted when the viewport mutates plate state outside the plate
    // cards — drag pan, wheel zoom, keyboard layout/fit/flip/flop, and
    // track-cycle. The Plate Manager dock listens and refreshes its
    // spinboxes so the user can read back the values they just edited.
    void plateStateChanged();

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

    // Drag anchor for pan: the mouse position at the last move/press
    // event in widget-local pixel coordinates. Used to compute per-event
    // deltas without holding a Qt event reference.
    float lastMouseX_ = 0.0f;
    float lastMouseY_ = 0.0f;

    // Plate index hit-tested at the start of the current drag. Mouse
    // motion sticks to whichever plate the press landed on, even if
    // the cursor wanders into a neighboring quadrant mid-drag.
    int dragPlate_ = -1;
};

#endif
