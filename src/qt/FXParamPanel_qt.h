// Combined FX panel for the Qt port — the single "effect controls"
// surface for the ACTIVE plate. Replaces the old split between an FX
// browser (FXStackPanel_Qt, removed) and a read-only FX-params view.
//
// Layout (top → bottom):
//   • a "+ Add FX" QToolButton whose hierarchical QMenu is built from
//     each FX's menuName ("Category/Subcategory/Name"). Picking a leaf
//     adds that FX to the active plate's stack.
//   • a QListWidget of per-FX cards in render order. Each card = a 24 px
//     header (drag handle, power toggle, effect name, remove button)
//     followed by that FX's parameters laid out by fxui::layoutRows: a
//     slider row per Slider-style float, and flow rows (knobs, fields,
//     bools, choice/texture/LUT pickers) that share a line between the
//     .jfx file's newLines and wrap when the dock is narrow.
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

#include "SequenceLoadBridge_qt.h"

#include <QListWidget>
#include <QPointer>
#include <QWidget>

#include <cstdint>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

class FXParamEditor_Qt;
class FXSliderRow_Qt;
class QLabel;
class QMenu;
class QTimer;
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
    // Re-read the active plate's FX stack metadata. When the plate and the stack
    // fingerprint are unchanged, only the editor values are updated; otherwise the
    // cards are rebuilt (deferred while an editor is being dragged or typed in).
    void refresh();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Updates the editors from the stack's values, writing only those that changed
    // and never the one being dragged. Returns false when the cached rows no longer
    // match the stack (forces a rebuild).
    bool refreshValuesOnly(const std::vector<jefe::qt::FXMeta>& stack);

    // Builds every card from the stack metadata.
    void rebuild(int active, const std::vector<jefe::qt::FXMeta>& stack);

    // A refresh on the next event-loop turn, never inside the caller's stack: editors
    // emit from focusOut and mouse handlers, so cards must not be deleted under them.
    void scheduleRefresh();

    // True while any param editor is being dragged or typed in.
    bool anyEditorInteracting() const;

    // Card heights follow the list width (flow rows wrap); slider label columns
    // shrink with it. Called after a rebuild and on every viewport resize.
    void relayoutCards();

    // (Re)builds the hierarchical "+ Add FX" menu from the bridge's
    // getAvailableFXMenu(). Rebuilt lazily when the menu is about to show
    // so a late FX autoload is reflected.
    void rebuildAddMenu();

    QToolButton* addButton_ = nullptr;
    QMenu* addMenu_ = nullptr;
    FXReorderList_Qt* list_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* retryTimer_ = nullptr;
    bool refreshQueued_ = false;

    // Reentrancy guard: editor slots ignore signals fired while refresh() writes
    // persisted values into the widgets.
    bool refreshing_ = false;

    // Last-rebuilt stack fingerprint: (instanceId, content hash) per FX. Ids alone
    // are not enough: a loaded stack (favourite, session, .fxs, peer) can reuse ids
    // with different effects. plateStateChanged fires for every viewport mouse-move,
    // so an unchanged fingerprint takes the values-only path.
    int lastActivePlate_ = -2;
    std::vector<std::pair<std::uint32_t, std::string>> lastFingerprint_;

    // One entry per editable/display parameter of the last rebuild, in stack and
    // declaration order (spacers and newLines skipped).
    struct CachedRow {
        std::string group;
        std::string name;
        int paramType = -1;  // FXParamType cast to int
        float value = 0.0f;
        float defaultValue = 0.0f;
        QPointer<FXParamEditor_Qt> floatEditor;  // Float params
        QPointer<QWidget> control;               // check / combo / value label
        QPointer<QLabel> label;                  // non-float rows: accent when modified
        QString fileColour;                      // non-white labelColor, else empty
        int labelState = -1;                     // cache: 0 normal, 1 accent
    };
    std::vector<CachedRow> rowCache_;

    // Slider rows per card, for the label column fitting in relayoutCards().
    struct SliderLabel {
        QPointer<FXSliderRow_Qt> row;
        QString text;  // the full label; shown elided when the column is narrower
    };
    std::vector<std::vector<SliderLabel>> sliderLabels_;
    // Labels of lines holding one non-float control, per card: they share the slider
    // label column so every control in a card starts at the same x.
    struct ColumnLabel {
        QPointer<QLabel> label;
        QPointer<QWidget> control;
        QString text;  // the full label; shown elided when the column is narrower
    };
    std::vector<std::vector<ColumnLabel>> columnLabels_;
    // Each card's header power toggle, in stack order; the fast path syncs them.
    std::vector<QPointer<QToolButton>> powerButtons_;
    int lastCardWidth_ = -1;
    // Collapsed cards, by (plate, FX instance id, content hash): the choice outlives rebuilds.
    using CollapseKey = std::tuple<int, std::uint32_t, std::string>;
    std::set<CollapseKey> collapsed_;
};

#endif
