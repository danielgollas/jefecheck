#include "PlaylistPanel_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
constexpr const char* kLastDirSettingKey = "Playlist/lastAddDir";
}

PlaylistPanel_Qt::PlaylistPanel_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("playlist.panel");

    list_ = new QListWidget(this);
    list_->setObjectName("playlist.list");
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setAlternatingRowColors(true);
    connect(list_, &QListWidget::itemDoubleClicked,
            this, &PlaylistPanel_Qt::onItemDoubleClicked);

    addBtn_ = new QPushButton("Add…", this);
    addBtn_->setObjectName("playlist.add.button");
    addBtn_->setToolTip("Add a file or sequence to the playlist");
    connect(addBtn_, &QPushButton::clicked,
            this, &PlaylistPanel_Qt::onAddClicked);

    removeBtn_ = new QPushButton("Remove", this);
    removeBtn_->setObjectName("playlist.remove.button");
    connect(removeBtn_, &QPushButton::clicked,
            this, &PlaylistPanel_Qt::onRemoveClicked);

    upBtn_ = new QPushButton("↑", this);
    upBtn_->setObjectName("playlist.up.button");
    upBtn_->setToolTip("Move selected entry up");
    connect(upBtn_, &QPushButton::clicked,
            this, &PlaylistPanel_Qt::onUpClicked);

    downBtn_ = new QPushButton("↓", this);
    downBtn_->setObjectName("playlist.down.button");
    downBtn_->setToolTip("Move selected entry down");
    connect(downBtn_, &QPushButton::clicked,
            this, &PlaylistPanel_Qt::onDownClicked);

    clearBtn_ = new QPushButton("Clear", this);
    clearBtn_->setObjectName("playlist.clear.button");
    connect(clearBtn_, &QPushButton::clicked,
            this, &PlaylistPanel_Qt::onClearClicked);

    status_ = new QLabel(this);
    status_->setObjectName("playlist.status.label");
    status_->setStyleSheet("color: #888; font-style: italic;");
    status_->setText("Playlist is empty.");

    auto* btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->addWidget(addBtn_);
    btnRow->addWidget(removeBtn_);
    btnRow->addWidget(upBtn_);
    btnRow->addWidget(downBtn_);
    btnRow->addWidget(clearBtn_);
    btnRow->addStretch(1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);
    outer->addWidget(list_, /*stretch*/ 1);
    outer->addLayout(btnRow);
    outer->addWidget(status_);

    refreshList();
}

void PlaylistPanel_Qt::refreshList() {
    const int prevSelected = list_->currentRow();
    list_->clear();
    for (const auto& name : jefe::qt::getPlaylistItemNames()) {
        list_->addItem(QString::fromStdString(name));
    }
    if (list_->count() == 0) {
        status_->setText("Playlist is empty.");
    } else {
        status_->setText(
            QString("%1 entries — double-click to load")
                .arg(list_->count()));
    }
    if (prevSelected >= 0 && prevSelected < list_->count()) {
        list_->setCurrentRow(prevSelected);
    }
}

void PlaylistPanel_Qt::onAddClicked() {
    QSettings s;
    const QString seed = s.value(kLastDirSettingKey, QDir::homePath()).toString();
    const QString chosen = QFileDialog::getOpenFileName(
        this,
        "Add to playlist",
        seed,
        "Image sequences (*.dpx *.exr *.jpg *.jpeg *.png *.tif *.tiff *.tga *.bmp);;"
        "All files (*)");
    if (chosen.isEmpty()) return;
    s.setValue(kLastDirSettingKey, QFileInfo(chosen).absolutePath());

    jefe::qt::addPlaylistFile(chosen.toStdString());
    refreshList();
    list_->setCurrentRow(list_->count() - 1);
}

void PlaylistPanel_Qt::onRemoveClicked() {
    const int row = list_->currentRow();
    if (row < 0) return;
    jefe::qt::removePlaylistItem(row);
    refreshList();
}

void PlaylistPanel_Qt::onUpClicked() {
    const int row = list_->currentRow();
    if (row <= 0) return;
    jefe::qt::movePlaylistItem(row, -1);
    refreshList();
    list_->setCurrentRow(row - 1);
}

void PlaylistPanel_Qt::onDownClicked() {
    const int row = list_->currentRow();
    if (row < 0 || row >= list_->count() - 1) return;
    jefe::qt::movePlaylistItem(row, +1);
    refreshList();
    list_->setCurrentRow(row + 1);
}

void PlaylistPanel_Qt::onClearClicked() {
    jefe::qt::clearPlaylist();
    refreshList();
}

void PlaylistPanel_Qt::onItemDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    const int row = list_->row(item);
    if (row < 0) return;
    jefe::qt::loadPlaylistItem(row);
}
