// One plate's controls as a self-contained card. Lives inside the Plate
// Manager dock. Drives a stateful gfcPlateGUI_Qt — either an externally
// owned one borrowed from gfcPlateManager (the rendering chain's GUI for
// that plate, so widget edits drive real rendering) or, when no external
// GUI is supplied, a locally owned fallback used by the no-rendering-
// chain test path.
//
// State flows both ways: widgets → gfcPlateGUI_Qt via signal handlers,
// gfcPlateGUI_Qt → widgets via refreshFromState() (called when the
// viewport itself mutates plate state, e.g. wheel zoom, drag pan, or
// keyboard shortcuts).
#ifndef JEFECHECK_QT_PLATE_CARD_H
#define JEFECHECK_QT_PLATE_CARD_H

#include <QFrame>

#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;

class gfcPlateGUI_Qt;

class PlateCard_Qt : public QFrame {
    Q_OBJECT
public:
    // `id` is the plate index (0..3). `external` may be null — when
    // null, the card owns a private gfcPlateGUI_Qt (kept for tests /
    // dock-only previews). Otherwise the card uses `external` and never
    // deletes it; ownership stays with whoever created it (typically
    // gfcPlateManager via initializeWidgets()).
    explicit PlateCard_Qt(int id,
                          gfcPlateGUI_Qt* external = nullptr,
                          QWidget* parent = nullptr);
    ~PlateCard_Qt() override;

    gfcPlateGUI_Qt* gui() { return gui_; }

    // Pulls all values from gui_ and pushes them into the spinboxes /
    // combos / toggles, with widget signals temporarily blocked so the
    // refresh doesn't loop back into setters and overwrite floats with
    // their spinbox-rounded representations. Cheap; safe to call on
    // every paint.
    void refreshFromState();

private:
    int id_;
    // Either points into ownedGui_ or borrowed from outside. Never null
    // after construction.
    gfcPlateGUI_Qt* gui_ = nullptr;
    std::unique_ptr<gfcPlateGUI_Qt> ownedGui_;

    // Cached widget pointers — refreshFromState needs them.
    QComboBox* trackBox_ = nullptr;
    QComboBox* aspectBox_ = nullptr;
    QPushButton* cropBtn_ = nullptr;
    QPushButton* flipBtn_ = nullptr;
    QPushButton* flopBtn_ = nullptr;
    QPushButton* rgbaBtn_ = nullptr;
    QDoubleSpinBox* zoomSpin_ = nullptr;
    QDoubleSpinBox* panXSpin_ = nullptr;
    QDoubleSpinBox* panYSpin_ = nullptr;
    QDoubleSpinBox* rotSpin_ = nullptr;
    QComboBox* lutBox_ = nullptr;
    QDoubleSpinBox* gammaSpin_ = nullptr;
    QDoubleSpinBox* exposureSpin_ = nullptr;
    QDoubleSpinBox* contrastSpin_ = nullptr;
    QDoubleSpinBox* brightnessSpin_ = nullptr;
    QDoubleSpinBox* saturationSpin_ = nullptr;
};

#endif
