// Read-only viewer for the FX stack's per-FX parameters. PR-38 ships
// the panel as a right-side QDockWidget tabified with FX Stack and LUTs;
// PR-38b will add per-row editors (slider / spinbox / combobox / check)
// and round-trip writes through a setFXParam bridge.
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
};

#endif
