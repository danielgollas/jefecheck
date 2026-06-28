// Combined FX panel for the Qt port — the single "effect controls"
// surface for the ACTIVE plate. Replaces the old split between an FX
// browser (FXStackPanel_Qt, removed) and a read-only FX-params view.
//
// Layout (top → bottom):
//   • a "+ Add FX" QToolButton whose hierarchical QMenu is built from
//     each FX's menuName ("Category/Subcategory/Name"). Picking a leaf
//     adds that FX to the active plate's stack.
//   • a QListWidget of per-FX cards in render order. Each card = a
//     header (drag handle + "N. name" + active checkbox + remove button)
//     followed by that FX's inline param editors (float QDoubleSpinBox /
//     bool QCheckBox / choice QComboBox). Texture / cube / LUT slots stay
//     READ-ONLY QLabels.
//   • drag-to-reorder via QListWidget InternalMove → moveFXOnPlate.
//
// All FX autoload at startup; there is no available/loaded-status
// browser. This panel IS the FX UI.
//
// The panel reads/writes through SequenceLoadBridge_qt's jefe::qt::*
// accessors only, so this TU stays Qt-only — gfcFX/gfcFXStack drag glad
// in and cannot share a TU with QtGui on macOS (developer_notes.md §1).
#ifndef JEFECHECK_QT_FX_PARAM_PANEL_H
#define JEFECHECK_QT_FX_PARAM_PANEL_H

#include <QListWidget>
#include <QPointer>
#include <QWidget>

#include <string>
#include <vector>

class QLabel;
class QMenu;
class QToolButton;
class QVBoxLayout;

// QListWidget subclass that reports a single drag-reorder as a
// (from, to) move. QListWidget's InternalMove drop is implemented as a
// remove+insert that doesn't reliably emit rowsMoved across Qt versions,
// so we capture the item order (each item stashes its stack index in
// Qt::UserRole) before and after the base dropEvent and derive the move.
class FXReorderList_Qt : public QListWidget {
    Q_OBJECT
public:
    using QListWidget::QListWidget;

signals:
    // from / to are positions in the pre-drop stack (erase-then-insert
    // semantics — matches jefe::qt::moveFXOnPlate).
    void itemsReordered(int from, int to);

protected:
    void dropEvent(QDropEvent* e) override;
};

class FXParamPanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit FXParamPanel_Qt(QWidget* parent = nullptr);

signals:
    // Emitted after any edit that changes the rendered composite (add /
    // remove / reorder / active-toggle / param edit). MainWindow_Qt
    // connects this to viewport_->update() so the change shows
    // immediately — the idle playback tick skips repaints when nothing
    // is playing, so a stack mutation otherwise wouldn't repaint until
    // the next viewport mouse-move.
    void viewportRepaintRequested();

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

    // (Re)builds the hierarchical "+ Add FX" menu from the bridge's
    // getAvailableFXMenu(). Rebuilt lazily when the menu is about to show
    // so a late FX autoload is reflected.
    void rebuildAddMenu();

    QToolButton* addButton_ = nullptr;
    QMenu* addMenu_ = nullptr;
    FXReorderList_Qt* list_ = nullptr;
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
    // every card per pixel, churning QAccessible / AppKit. When the
    // active plate and the stack's FX-name list match the cache, we walk
    // refreshValuesOnly() instead of rebuilding.
    int lastActivePlate_ = -2;
    std::vector<std::string> lastStack_;

    // One entry per param row produced by the last full rebuild. Used by
    // refreshValuesOnly() to skip setValue/setChecked/setCurrentIndex
    // calls when the new bridge value matches the cached one. Editors
    // are held as QPointer so a stray Qt-side teardown doesn't dangle.
    // The editor pointer can be null for read-only display rows.
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
