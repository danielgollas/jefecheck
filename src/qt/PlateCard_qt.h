// One plate's controls as a self-contained card. Lives inside the Plate
// Manager dock. Owns a stateful gfcPlateGUI_Qt; widget signals push the
// user's edits into that interface, which is the same interface
// gfcPlate reads its rendering state from. So once gfcPlate is wired
// into the Qt build (PR-9), these controls drive real rendering with
// no further plumbing.
#ifndef JEFECHECK_QT_PLATE_CARD_H
#define JEFECHECK_QT_PLATE_CARD_H

#include <QFrame>

#include <memory>

class gfcPlateGUI_Qt;

class PlateCard_Qt : public QFrame {
    Q_OBJECT
public:
    // `id` is the plate index (0..3). Used for the title (1/2/3/4) and
    // for tagging debug output coming from the GUI state object.
    explicit PlateCard_Qt(int id, QWidget* parent = nullptr);
    ~PlateCard_Qt() override;

    gfcPlateGUI_Qt* gui() { return gui_.get(); }

private:
    int id_;
    std::unique_ptr<gfcPlateGUI_Qt> gui_;
};

#endif
