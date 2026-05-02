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

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

class PlaylistPanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistPanel_Qt(QWidget* parent = nullptr);

public slots:
    void refreshList();

private:
    void onAddClicked();
    void onRemoveClicked();
    void onUpClicked();
    void onDownClicked();
    void onClearClicked();
    void onItemDoubleClicked(QListWidgetItem* item);

    QListWidget* list_ = nullptr;
    QPushButton* addBtn_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
    QPushButton* upBtn_ = nullptr;
    QPushButton* downBtn_ = nullptr;
    QPushButton* clearBtn_ = nullptr;
    QLabel* status_ = nullptr;
};

#endif
