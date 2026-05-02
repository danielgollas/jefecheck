// Per-FX parameter panel for the Qt port. Each FX on the active
// plate's stack expands into a group of editor controls — float
// params get a QDoubleSpinBox, bools get a QCheckBox, choice/enum
// params get a QComboBox. Edits round-trip through
// `jefe::qt::setFXParamValueOnPlate` into gfcFXStack so the next
// composite picks up the new value.
//
// Texture / cube / LUT slots are intentionally read-only here — they
// rebind GL texture handles and need their own sub-panel work, which
// PR-38c will tackle.
//
// The panel reads through SequenceLoadBridge_qt's getFXStackMetaOnPlate
// so this TU stays Qt-only — gfcFX/gfcFXStack drag glad in.
#ifndef JEFECHECK_QT_FX_PARAM_PANEL_H
#define JEFECHECK_QT_FX_PARAM_PANEL_H

#include <QWidget>

class QLabel;
class QVBoxLayout;
class QScrollArea;

class FXParamPanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit FXParamPanel_Qt(QWidget* parent = nullptr);

public slots:
    // Re-read the active plate's FX stack metadata and rebuild the
    // panel contents. Cheap when the stack is empty / unchanged.
    void refresh();

private:
    QScrollArea* scroll_ = nullptr;
    QWidget* contentWidget_ = nullptr;
    QVBoxLayout* contentLayout_ = nullptr;
    QLabel* status_ = nullptr;

    // Reentrancy guard. setValue on a spin/check/combo emits its
    // valueChanged signal even when the change came from refresh()
    // setting the widget to its persisted value, which would otherwise
    // turn into a redundant bridge round-trip — and worse, fire while
    // the panel is mid-rebuild. The guard is set during refresh and
    // checked by every editor slot.
    bool refreshing_ = false;
};

#endif
