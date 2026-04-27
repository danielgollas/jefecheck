// One plate's controls as a self-contained card. Lives inside the Plate
// Manager dock, arranged by FlowLayout. Currently a placeholder with the
// FLTK plate panel's controls visible but not wired — the wiring lands in
// later 2E sub-PRs once gfcPlate is reachable from the Qt build.
#ifndef JEFECHECK_QT_PLATE_CARD_H
#define JEFECHECK_QT_PLATE_CARD_H

#include <QFrame>

class PlateCard_Qt : public QFrame {
    Q_OBJECT
public:
    // `id` is the plate index (0..3). Used for the title (A/B/C/D) until we
    // have the real gfcPlate model available in the Qt build.
    explicit PlateCard_Qt(int id, QWidget* parent = nullptr);
};

#endif
