// Notes dock for the Qt port (JEF-39 Task 6) -- lists the revisions and
// notes drawn on the active plate's footage, a tool/colour/size picker for
// a future drawing hook, click-to-jump-to-frame, and a "Lock Round" button
// (toggles to "Unlock Round" -- one button, mirrors the transport
// play/pause toggle rather than shipping two buttons).
//
// This panel MUST NOT include gfcreview.h / gfcrevision.h / gfcnote.h, or
// any other rendering-chain header -- only SequenceLoadBridge_qt.cpp may
// (developer_notes.md §1: glad and QOpenGLWidget can't share a TU on
// macOS). Everything here goes through the jefe::qt::* accessors added to
// that bridge for this task (NoteRow, RevisionRow, notesForPlate(), ...).
//
// Mouse drawing on the viewport is NOT wired here -- see the plan's
// file-ownership map (Task 6 owns this panel, the bridge accessors, and
// the dock/menu/shortcut wiring, not gfcPlate.cpp). The tool/colour/size
// pickers only set state in the bridge for a future viewport hook to read.
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
