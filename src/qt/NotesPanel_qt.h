// Notes dock: lists the revisions and notes on the active plate's footage,
// with a tool/colour/size picker, click-to-jump-to-frame and a Lock/Unlock Round toggle.
// MUST NOT include rendering-chain headers (gfcreview.h, gfcnote.h, ...); go
// through the jefe::qt::* bridge accessors. See developer_notes.md §1.
#ifndef JEFECHECK_QT_NOTES_PANEL_H
#define JEFECHECK_QT_NOTES_PANEL_H

#include <QWidget>

class QLabel;
class QPushButton;
class QSpinBox;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

class NotesPanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit NotesPanel_Qt(QWidget* parent = nullptr);

signals:
    // Emitted after an action that should be reflected on screen (frame
    // jump, lock/unlock, remove). Mirrors
    // FXParamPanel_Qt::viewportRepaintRequested -- MainWindow_qt.cpp
    // connects it the same way (viewport_->update()).
    void viewportRepaintRequested();

public slots:
    // Rebuilds the revision/note tree for the currently active plate.
    // Connected to GlViewport_Qt::plateStateChanged (active-plate switch)
    // and invoked after pumpNetwork() applies an inbound note-sync event --
    // the same two triggers FXParamPanel_Qt::refresh() responds to.
    void refresh();

private slots:
    void onColorButtonClicked();
    void onSizeChanged(int value);
    void onTreeItemClicked(QTreeWidgetItem* item, int column);
    void onLockClicked();
    void onRemoveClicked();

private:
    void rebuildColorSwatch();
    void syncToolButtons();       // checked = armed && that tool is active
    void updateActionButtons();   // lock/unlock + remove enabled state

    QLabel* statusLabel_ = nullptr;
    QToolButton* toolFreehand_ = nullptr;
    QToolButton* toolArrow_ = nullptr;
    QToolButton* toolBox_ = nullptr;
    QToolButton* toolText_ = nullptr;
    QPushButton* colorButton_ = nullptr;
    QSpinBox* sizeSpin_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QPushButton* lockButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
};

#endif
