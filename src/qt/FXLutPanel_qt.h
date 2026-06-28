// LUT panel. A QWidget hosted inside its own QDockWidget. LUTPanel_Qt
// lists loaded LUTs from lutManager, accepts dropped LUT files, and
// applies a selection to the active plate.
//
// (The old FXStackPanel_Qt FX browser lived here too; it was removed
// when the FX UI collapsed into the single combined FXParamPanel_Qt —
// see developer_notes.md §23.)
#ifndef JEFECHECK_QT_FX_LUT_PANEL_H
#define JEFECHECK_QT_FX_LUT_PANEL_H

#include <QWidget>

class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QTreeWidget;
class QPushButton;
class QCheckBox;
class LUTPreview_Qt;

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
