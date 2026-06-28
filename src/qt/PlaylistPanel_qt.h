// Playlist dock for the Qt port. PR-40 ships a `QDockWidget` hosting
// a `QListWidget` of playlist entries plus add / remove / up / down /
// clear buttons. Double-clicking an entry calls
// `trackManager.setPlaylistItem(playlistManager.getItem(idx))` —
// same path as the FLTK `playlistItemCB`.
//
// PR-40b will add: drag-and-drop reorder, multi-track items per
// row (current MVP creates one-track items via Add), session-save
// integration, and the FLTK "compact view" / "show full paths" /
// "scale override" submenu items.
#ifndef JEFECHECK_QT_PLAYLIST_PANEL_H
#define JEFECHECK_QT_PLAYLIST_PANEL_H

#include <QWidget>
#include <QString>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QToolButton;
class QVBoxLayout;

// One playlist row: header (drag handle, index, name, track chips, collapse
// chevron) + a collapsible per-track detail body. Dumb widget — it emits
// intent signals; PlaylistPanel_Qt does the bridge calls.
class PlaylistItemCard : public QWidget {
    Q_OBJECT
public:
    PlaylistItemCard(int index, const QString& name, bool expanded,
                     bool fullPaths, QWidget* parent = nullptr);
    void setExpanded(bool on);
    bool isExpanded() const { return expanded_; }
    void setSelectedHighlight(bool on);

signals:
    void loadRequested(int index);
    void removeRequested(int index);
    void toggleExpandRequested(int index);

protected:
    void mouseDoubleClickEvent(QMouseEvent* ev) override;  // emits loadRequested

private:
    void rebuildDetail();
    int index_;
    bool expanded_;
    bool fullPaths_;
    QToolButton* chevron_ = nullptr;
    QWidget* detail_ = nullptr;
    QVBoxLayout* detailLayout_ = nullptr;
};

class QCheckBox;
class QComboBox;

class PlaylistPanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistPanel_Qt(QWidget* parent = nullptr);

public slots:
    void refreshList();
    void advanceToNext();   // called by the idle tick when auto-advance fires

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;  // list keyboard + drop

private:
    void onAddCurrent();
    void onAddFiles();
    void onRemoveClicked();
    void onUpClicked();
    void onDownClicked();
    void onClearClicked();
    void onSaveClicked();
    void onLoadClicked();
    void loadRow(int row);
    void applyScaleOverride();
    void showContextMenu(const QPoint& pos);
    int  selectedRow() const;

    QListWidget* list_ = nullptr;
    QCheckBox* compactCheck_ = nullptr;
    QCheckBox* fullPathsCheck_ = nullptr;
    QCheckBox* autoAdvanceCheck_ = nullptr;
    QCheckBox* loopCheck_ = nullptr;
    QCheckBox* scaleOverrideCheck_ = nullptr;
    QComboBox* scaleCombo_ = nullptr;
    QLabel* status_ = nullptr;
};

#endif
