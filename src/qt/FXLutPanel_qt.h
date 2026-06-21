// FX Stack and LUT panels. Each is a QWidget hosted inside its own
// QDockWidget; the two docks are stacked into a tab group at startup.
// FX Stack lists loaded effects (browser) and the active plate's
// stack; double-click or Add adds an effect, Remove deletes from the
// stack. LUTPanel_Qt is real: lists loaded LUTs from lutManager,
// accepts dropped LUT files, and applies a selection to the active
// plate.
#ifndef JEFECHECK_QT_FX_LUT_PANEL_H
#define JEFECHECK_QT_FX_LUT_PANEL_H

#include <QWidget>

class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class QPushButton;
class QCheckBox;
class LUTPreview_Qt;

class FXStackPanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit FXStackPanel_Qt(QWidget* parent = nullptr);

signals:
    // Emitted after add / remove mutates the active plate's FX stack.
    // The FX param dock listens so its read-only view stays in sync —
    // plateStateChanged from GlViewport_Qt only fires for viewport-side
    // edits (drag, zoom, keyboard), not for stack mutations.
    void stackChanged();

public slots:
    // Pulls the available-FX list from fxManager and the active
    // plate's gfcFXStack; rebuilds both QListWidgets. Called at
    // construction, after FX autoload, and on every add/remove.
    void refreshLists();

private:
    void addSelected();
    void removeSelected();

    QListWidget* available_ = nullptr;
    QListWidget* stack_ = nullptr;
    QPushButton* addBtn_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
    QLabel* status_ = nullptr;
};

class LUTPanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit LUTPanel_Qt(QWidget* parent = nullptr);

public slots:
    // Pulls the LUT name list from lutManager (via the bridge) and
    // refreshes the visible list. Called at construction and after
    // every drop. Cheap; no need to call from the per-tick.
    void refreshList();

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void applySelected();
    void updatePreview();
    int  selectedGuiIndex() const;   // gui LUT index of the selected row (0 = none)

    QTreeWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
    QCheckBox*     previewToggle_ = nullptr;
    LUTPreview_Qt* preview_       = nullptr;
};

#endif
