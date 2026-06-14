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
#include <QString>

#include <memory>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;

class AspectCropCombo_Qt;
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
    int id() const { return id_; }

    // Pulls all values from gui_ and pushes them into the spinboxes /
    // combos / toggles, with widget signals temporarily blocked so the
    // refresh doesn't loop back into setters and overwrite floats with
    // their spinbox-rounded representations. Cheap; safe to call on
    // every paint.
    void refreshFromState();

    // Fast path used during viewport drag: only refresh the four
    // transform spinboxes (zoom, panX, panY, rotation), skipping every
    // other widget. Each is gated on cache delta, same as
    // refreshFromState — typical cost during pan is 2-4 setValue calls
    // on this card only, with no QSignalBlocker scope churn for the
    // other dozen widgets and no FX param panel touch.
    void refreshTransformOnly();

    // Drag-friendly partial refresh — updates only the gamma, exposure,
    // contrast, brightness, and saturation spinboxes on this card,
    // gated on per-field cache delta. Used by the W/E/Q/D/S key+drag
    // color-correction handler in GlViewport_Qt so the spinbox under
    // the user's eye reflects the live value without paying the full
    // refreshAllCards + FXParamPanel cascade.
    void refreshColorOnly();

    // Toggles the active-plate styling. Called by the parent dock when
    // it knows the active plate index has changed (either from a click
    // on a different card or via plateManager.setActiveQuad()).
    void setActiveHighlight(bool on);

signals:
    // Emitted on left-click anywhere on the card body that isn't
    // already a child widget. The dock listens and sets this card's
    // plate as the active plate.
    void clicked(int id);

protected:
    void mousePressEvent(QMouseEvent* e) override;

private:
    int id_;
    // Either points into ownedGui_ or borrowed from outside. Never null
    // after construction.
    gfcPlateGUI_Qt* gui_ = nullptr;
    std::unique_ptr<gfcPlateGUI_Qt> ownedGui_;

    // Cached widget pointers — refreshFromState needs them.
    QComboBox* trackBox_ = nullptr;
    QComboBox* layerBox_ = nullptr;
    // Combined aspect-ratio combo with a folded-in crop checkbox (replaces
    // the old standalone aspectBox_ QComboBox + cropBtn_ toggle).
    AspectCropCombo_Qt* aspectCrop_ = nullptr;
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

    // Last-seen state from gui_/bridge. refreshFromState() short-circuits
    // each widget write when its source value matches the cached one.
    // Without this, every plateStateChanged emission (which fires on
    // every viewport mouse-move during drag) walks 13+ setValue/setText/
    // setChecked/setCurrentIndex calls per card × 4 cards through
    // QAccessible / AppKit / AttributeGraph, dominating drag CPU.
    struct CachedState {
        bool  valid       = false;
        int   track       = -1;
        QString aspect;
        bool  crop        = false;
        bool  flip        = false;
        bool  flop        = false;
        int   rgba        = -1;
        float scale       = 0.0f;
        float tx          = 0.0f;
        float ty          = 0.0f;
        float rz          = 0.0f;
        int   lut         = -1;
        std::vector<std::string> lutOptions;
        float gamma       = 0.0f;
        float exposure    = 0.0f;
        float contrast    = 0.0f;
        float brightness  = 0.0f;
        float saturation  = 0.0f;
        // Layer combo state — both the populated item list and the
        // current selection. Visibility is derived from layers being
        // non-empty (named) so it's cached implicitly via layers.size.
        std::vector<std::string> layers;
        std::string activeLayer;
        bool layerVisible = false;
    };
    CachedState lastShown_;
};

#endif
