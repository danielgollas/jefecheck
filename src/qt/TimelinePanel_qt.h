// Timeline + transport dock content. Top row is the transport bar
// (rewind / step-back / play-pause / step-fwd / fast-fwd / loop mode /
// frame counter / in / out / FPS). Bottom is a custom scrubber widget
// (TimelineScrubber_Qt) that paints the play range, in/out markers,
// and a draggable playhead.
//
// Widgets push edits into gfcPlaybackManager (via the bridge) and a
// refreshFromState() slot pulls back the manager's current values so
// playback advances tracked by the QTimer tick are visible. Using the
// usual Qt signal-block dance to avoid feedback loops between a setter
// and a programmatic widget update.
#ifndef JEFECHECK_QT_TIMELINE_PANEL_H
#define JEFECHECK_QT_TIMELINE_PANEL_H

#include <QWidget>

#include <array>

#include "SequenceLoadBridge_qt.h"   // jefe::qt::TrackTimelineState

class QComboBox;
class QDoubleSpinBox;
class QPaintEvent;
class QMouseEvent;
class QContextMenuEvent;
class QDragEnterEvent;
class QDropEvent;
class QPushButton;
class QSpinBox;

class TimelineScrubber_Qt : public QWidget {
    Q_OBJECT
public:
    explicit TimelineScrubber_Qt(QWidget* parent = nullptr);

    void setRange(int from, int to);
    void setInOut(int in, int out);
    void setCurrentFrame(int frame);

signals:
    // Emitted on click or drag inside the scrubber. The frame value is
    // already clamped to [from, to]; the parent panel forwards it to
    // playbackManager.setCurrentFrame.
    void seek(int frame);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    int frameFromX(int x) const;
    int xFromFrame(int frame) const;

    int from_ = 1;
    int to_ = 1;
    int in_ = 1;
    int out_ = 1;
    int current_ = 1;
};

class TimelineTracks_Qt : public QWidget {
    Q_OBJECT
public:
    explicit TimelineTracks_Qt(QWidget* parent = nullptr);

    // Pull global range, current frame, and the 4 per-track states from
    // the bridge; cache-compare and repaint only on change. Driven from
    // TimelinePanel_Qt::refreshFromPlayback. Cheap when idle.
    void refresh();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    int laneTopY(int track) const;   // y of lane `track`'s top edge
    int laneHeight() const;          // per-lane height
    int trackAtY(int y) const;       // which lane (0..3) contains y
    double pxPerFrame() const;       // pixels per timeline frame

    int from_ = 1;
    int to_ = 1;
    int current_ = 1;
    std::array<jefe::qt::TrackTimelineState, 4> states_{};

    // Drag-to-offset state. Left-drag accumulates dx (vs dragPrevX_);
    // each whole-frame worth of motion steps the dragged track's offset
    // by +/-1. dragTrack_ == -1 means no gesture in progress.
    int    dragTrack_ = -1;
    double dragAccumPx_ = 0.0;
    int    dragPrevX_ = 0;
};

class TimelinePanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePanel_Qt(QWidget* parent = nullptr);

public slots:
    // Pulls current frame, in/out, range, FPS, loop mode, playing state
    // from gfcPlaybackManager and pushes them into the widgets. Driven
    // from MainWindow_Qt's QTimer tick so the playhead advances during
    // playback. Cheap; signal-blocked so it doesn't loop back into the
    // setters.
    void refreshFromPlayback();

private:
    TimelineScrubber_Qt* scrubber_ = nullptr;
    TimelineTracks_Qt* tracks_ = nullptr;
    QPushButton* rewBtn_ = nullptr;
    QPushButton* stepBackBtn_ = nullptr;
    QPushButton* playBtn_ = nullptr;
    QPushButton* stepFwdBtn_ = nullptr;
    QPushButton* ffwdBtn_ = nullptr;
    QComboBox* loopMode_ = nullptr;
    QSpinBox* frameSpin_ = nullptr;
    QSpinBox* inSpin_ = nullptr;
    QSpinBox* outSpin_ = nullptr;
    QDoubleSpinBox* fpsSpin_ = nullptr;

    // Last-seen playback values; refreshFromPlayback compares against
    // these and skips widget setters when nothing changed. Without this
    // the 60Hz tick fires setValue/setText/setRange unconditionally and
    // each one cascades through QAccessible/AppKit even when the value
    // is identical, dominating idle CPU sampling.
    int   lastFrom_     = -1;
    int   lastTo_       = -1;
    int   lastCur_      = -1;
    int   lastIn_       = -1;
    int   lastOut_      = -1;
    int   lastLoop_     = -1;
    float lastFps_      = -1.0f;
    bool  lastPlaying_  = false;
    bool  lastCacheValid_ = false;
};

#endif
