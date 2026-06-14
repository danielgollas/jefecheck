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

#include <QPointer>
#include <QWidget>

#include <string>
#include <vector>

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
    // Walks the existing editor widgets and updates only those whose
    // current value differs from what the bridge reports. Called from
    // refresh() when the active plate and FX stack haven't changed, so
    // we can skip the full teardown + rebuild. Returns true when the
    // walk succeeded; false signals the cached editor list is stale
    // (mismatched count) and forces a full rebuild.
    bool refreshValuesOnly();

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

    // Last-rebuilt stack fingerprint. plateStateChanged fires for every
    // viewport mouse-move; without this, refresh() tore down + rebuilt
    // every editor widget per pixel, churning QAccessible / AppKit.
    // When the active plate and the stack's FX-name list match the
    // cache, we walk refreshValuesOnly() instead of rebuilding.
    int lastActivePlate_ = -2;
    std::vector<std::string> lastStack_;

    // One entry per param row produced by the last full rebuild. Used by
    // refreshValuesOnly() to skip setValue/setChecked/setCurrentIndex
    // calls when the new bridge value matches the cached one. Editors
    // are held as QPointer so a stray Qt-side teardown (which shouldn't
    // happen but cheap to guard) doesn't dangle. The editor pointer can
    // be null for read-only display rows (Texture/Cube/LUT/Other).
    struct CachedRow {
        int fxIdx = 0;
        std::string group;
        std::string name;
        int paramType = -1;  // FXParamType cast to int
        float value = 0.0f;
        QPointer<QWidget> editor;
    };
    std::vector<CachedRow> rowCache_;
};

#endif
