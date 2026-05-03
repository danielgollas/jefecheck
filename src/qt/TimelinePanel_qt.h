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

class QComboBox;
class QDoubleSpinBox;
class QPaintEvent;
class QMouseEvent;
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

    void setTimelineRange(int from, int to);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    int from_ = 1;
    int to_ = 1;
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
};

#endif
