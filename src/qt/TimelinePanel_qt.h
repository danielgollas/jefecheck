// Timeline + transport dock content. Top row is a transport bar
// (rewind / step-back / play-pause / step-fwd / fast-fwd / loop mode /
// frame counter / FPS). Bottom is the multitrack scrubber, drawn via a
// custom QWidget (TimelineTracks_Qt). Bodies are placeholders for now.
#ifndef JEFECHECK_QT_TIMELINE_PANEL_H
#define JEFECHECK_QT_TIMELINE_PANEL_H

#include <QWidget>

class QPaintEvent;

class TimelineTracks_Qt : public QWidget {
    Q_OBJECT
public:
    explicit TimelineTracks_Qt(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* e) override;
};

class TimelinePanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit TimelinePanel_Qt(QWidget* parent = nullptr);
};

#endif
