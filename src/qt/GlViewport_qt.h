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

#include <QElapsedTimer>
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

    // Toggled by MainWindow_Qt when the Load Sequence Manager opens/closes.
    // While true, every plate renders its track's previewFrame
    // (deterministic, no per-track "was-touched" state).
    void setLoadWindowOpen(bool open);
    bool isLoadWindowOpen() const { return loadWindowOpen_; }

signals:
    // Legacy single-arg signal — kept so any existing connections that
    // don't care about scale (e.g. future logging hooks) still work.
    // Emitted alongside fileDroppedWithScale(path, 1.0) on plain drops.
    void fileDropped(const QString& path);

    // Scale is the load-time downsample factor read from
    // keyboardModifiers in dropEvent: plain = 1.0, Shift = 0.5,
    // Shift+Cmd = 0.25. MainWindow_Qt threads this through to
    // jefe::qt::loadFileIntoPlate.
    void fileDroppedWithScale(const QString& path, float scale);

    // Emitted when the viewport mutates plate state outside the plate
    // cards — drag pan, wheel zoom, keyboard layout/fit/flip/flop, and
    // track-cycle. The Plate Manager dock listens and refreshes its
    // spinboxes so the user can read back the values they just edited.
    void plateStateChanged();

    // Lightweight version emitted continuously during pan/zoom drag.
    // Wired to a per-card slot that only refreshes the four transform
    // spinboxes (zoom, panX, panY, rotation) on the dragged plate,
    // bypassing the full refreshAllCards + FXParamPanel cascade.
    //
    // For Alt-drag gang-transform: emit this once per affected plate
    // index — the queued connection coalesces in the event loop and
    // each card's refreshTransformOnly is independently delta-gated
    // against its cache. 4 emits per frame at 60Hz is still cheap.
    void plateTransformChanged(int plateIdx);

    // Emitted only when the Load Sequence Manager is open. plateIdx is
    // the plate the drop is targeting (today: always 0; future PR will
    // route to plate-under-cursor). path is the local file path.
    void fileDroppedWhileLoadWindowOpen(int plateIdx, const QString& path);

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

    bool loadWindowOpen_ = false;

    // mouseMoveEvent gates plateStateChanged emissions to ~60Hz so the
    // plate-card spinboxes and FX-panel reflect the live drag without
    // the AppKit layout cascade overhead of firing per-pixel.
    QElapsedTimer dragEmitTimer_;
    bool dragEmittedAny_ = false;
};

#endif
