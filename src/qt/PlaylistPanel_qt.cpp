#include "PlaylistPanel_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr const char* kCompactKey   = "Playlist/compactView";
constexpr const char* kFullPathsKey = "Playlist/showFullPaths";
constexpr const char* kAutoAdvKey   = "Playlist/autoAdvance";
constexpr const char* kLoopKey      = "Playlist/loop";
constexpr const char* kScaleOnKey   = "Playlist/scaleOverrideOn";
constexpr const char* kScaleValKey  = "Playlist/scaleOverridePct";
constexpr const char* kLastDirKey   = "Playlist/lastAddDir";
}

// ---------------------------------------------------------------------------
// PlaylistItemCard implementation
// ---------------------------------------------------------------------------

PlaylistItemCard::PlaylistItemCard(int index, const QString& name, bool expanded,
                                   bool fullPaths, QWidget* parent)
    : QWidget(parent), index_(index), expanded_(expanded), fullPaths_(fullPaths) {
    setObjectName(QString("playlist.card.%1").arg(index));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 2, 4, 2);
    outer->setSpacing(2);

    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(6);

    auto* handle = new QLabel("\xe2\x98\xb0", this);  // ☰ drag affordance
    handle->setToolTip("Drag to reorder");
    handle->setStyleSheet("color:#888;");
    header->addWidget(handle);

    auto* idx = new QLabel(QString::number(index + 1), this);
    idx->setMinimumWidth(18);
    header->addWidget(idx);

    auto* nameLab = new QLabel(name, this);
    nameLab->setObjectName(QString("playlist.card.%1.name").arg(index));
    nameLab->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    nameLab->setToolTip(name);
    header->addWidget(nameLab, /*stretch*/ 1);

    chevron_ = new QToolButton(this);
    chevron_->setObjectName(QString("playlist.card.%1.chevron").arg(index));
    chevron_->setAutoRaise(true);
    chevron_->setText(expanded_ ? "\xe2\x96\xbe" : "\xe2\x96\xb8");  // ▾ / ▸
    connect(chevron_, &QToolButton::clicked, this,
            [this]() { emit toggleExpandRequested(index_); });
    header->addWidget(chevron_);

    auto* removeBtn = new QToolButton(this);
    removeBtn->setObjectName(QString("playlist.card.%1.remove").arg(index));
    removeBtn->setText("\xe2\x9c\x95");  // ✕
    removeBtn->setAutoRaise(true);
    removeBtn->setToolTip("Remove from playlist");
    connect(removeBtn, &QToolButton::clicked, this,
            [this]() { emit removeRequested(index_); });
    header->addWidget(removeBtn);

    outer->addLayout(header);

    detail_ = new QWidget(this);
    detailLayout_ = new QVBoxLayout(detail_);
    detailLayout_->setContentsMargins(28, 0, 0, 0);
    detailLayout_->setSpacing(0);
    outer->addWidget(detail_);

    rebuildDetail();
    detail_->setVisible(expanded_);
}

void PlaylistItemCard::rebuildDetail() {
    // Clear existing rows.
    QLayoutItem* it;
    while ((it = detailLayout_->takeAt(0)) != nullptr) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    for (const auto& d : jefe::qt::getPlaylistItemDetail(index_)) {
        QString path = QString::fromStdString(d.path);
        if (!fullPaths_) path = QFileInfo(path).fileName();
        QString line = QString("%1  %2  %3-%4 (%5)  %6%%  %7%8  %9")
            .arg(QString::fromStdString(d.letter))
            .arg(path)
            .arg(d.fromFrame).arg(d.toFrame).arg(d.totalFrames)
            .arg(d.scalePct)
            .arg(QString::fromStdString(d.filter))
            .arg(d.crop ? "  crop" : "")
            .arg(QString::fromStdString(d.bitDepth));
        auto* row = new QLabel(line, detail_);
        row->setStyleSheet("color:#aaa; font-size:11px;");
        detailLayout_->addWidget(row);
    }
}

void PlaylistItemCard::setExpanded(bool on) {
    expanded_ = on;
    if (chevron_) chevron_->setText(on ? "\xe2\x96\xbe" : "\xe2\x96\xb8");
    if (detail_) detail_->setVisible(on);
    updateGeometry();
}

void PlaylistItemCard::setSelectedHighlight(bool on) {
    setStyleSheet(on ? "background:#33405a; border-radius:3px;" : "");
}

void PlaylistItemCard::mouseDoubleClickEvent(QMouseEvent* ev) {
    emit loadRequested(index_);
    QWidget::mouseDoubleClickEvent(ev);
}

// ---------------------------------------------------------------------------
// PlaylistPanel_Qt implementation
// ---------------------------------------------------------------------------

PlaylistPanel_Qt::PlaylistPanel_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("playlist.panel");
    QSettings s;

    list_ = new QListWidget(this);
    list_->setObjectName("playlist.list");
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setDragDropMode(QAbstractItemView::DropOnly);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->setAcceptDrops(true);
    list_->viewport()->setAcceptDrops(true);
    list_->installEventFilter(this);
    list_->viewport()->installEventFilter(this);
    connect(list_, &QListWidget::customContextMenuRequested,
            this, &PlaylistPanel_Qt::showContextMenu);

    auto mkBtn = [this](const char* text, const char* obj, const char* tip,
                        void (PlaylistPanel_Qt::*slot)()) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(obj);
        if (tip) b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, slot);
        return b;
    };
    auto* addCurBtn = mkBtn("Add Current", "playlist.button.addcurrent",
        "Snapshot the current setup as a playlist item",
        &PlaylistPanel_Qt::onAddCurrent);
    auto* addFilesBtn = mkBtn("Add Files…", "playlist.button.addfiles",
        "Build an item from one or more files", &PlaylistPanel_Qt::onAddFiles);
    auto* removeBtn = mkBtn("Remove", "playlist.button.remove", nullptr,
        &PlaylistPanel_Qt::onRemoveClicked);
    auto* upBtn = mkBtn("↑", "playlist.button.up", "Move selected up",
        &PlaylistPanel_Qt::onUpClicked);
    auto* downBtn = mkBtn("↓", "playlist.button.down", "Move selected down",
        &PlaylistPanel_Qt::onDownClicked);
    auto* clearBtn = mkBtn("Clear", "playlist.button.clear", nullptr,
        &PlaylistPanel_Qt::onClearClicked);
    auto* loadBtn = mkBtn("Load…", "playlist.button.load",
        "Load a .jpl playlist", &PlaylistPanel_Qt::onLoadClicked);
    auto* saveBtn = mkBtn("Save…", "playlist.button.save",
        "Save the playlist to a .jpl", &PlaylistPanel_Qt::onSaveClicked);

    compactCheck_ = new QCheckBox("Compact", this);
    compactCheck_->setObjectName("playlist.check.compact");
    compactCheck_->setChecked(s.value(kCompactKey, true).toBool());
    connect(compactCheck_, &QCheckBox::toggled, this, [this](bool on) {
        QSettings st; st.setValue(kCompactKey, on); refreshList();
    });

    fullPathsCheck_ = new QCheckBox("Full paths", this);
    fullPathsCheck_->setObjectName("playlist.check.fullpaths");
    fullPathsCheck_->setChecked(s.value(kFullPathsKey, false).toBool());
    connect(fullPathsCheck_, &QCheckBox::toggled, this, [this](bool on) {
        QSettings st; st.setValue(kFullPathsKey, on); refreshList();
    });

    autoAdvanceCheck_ = new QCheckBox("Auto-advance", this);
    autoAdvanceCheck_->setObjectName("playlist.check.autoadvance");
    autoAdvanceCheck_->setChecked(s.value(kAutoAdvKey, false).toBool());
    connect(autoAdvanceCheck_, &QCheckBox::toggled, this, [](bool on) {
        QSettings st; st.setValue(kAutoAdvKey, on);
    });

    loopCheck_ = new QCheckBox("Loop playlist", this);
    loopCheck_->setObjectName("playlist.check.loop");
    loopCheck_->setChecked(s.value(kLoopKey, false).toBool());
    connect(loopCheck_, &QCheckBox::toggled, this, [](bool on) {
        QSettings st; st.setValue(kLoopKey, on);
    });

    scaleOverrideCheck_ = new QCheckBox("Scale override", this);
    scaleOverrideCheck_->setObjectName("playlist.check.scaleoverride");
    scaleOverrideCheck_->setChecked(s.value(kScaleOnKey, false).toBool());
    scaleCombo_ = new QComboBox(this);
    scaleCombo_->setObjectName("playlist.combo.scale");
    scaleCombo_->addItem("100", 100);
    scaleCombo_->addItem("50", 50);
    scaleCombo_->addItem("25", 25);
    {
        int idx = scaleCombo_->findData(s.value(kScaleValKey, 100).toInt());
        scaleCombo_->setCurrentIndex(idx < 0 ? 0 : idx);
    }
    connect(scaleOverrideCheck_, &QCheckBox::toggled, this, [this](bool on) {
        QSettings st; st.setValue(kScaleOnKey, on); applyScaleOverride();
    });
    connect(scaleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        QSettings st; st.setValue(kScaleValKey, scaleCombo_->currentData().toInt());
        applyScaleOverride();
    });

    status_ = new QLabel(this);
    status_->setObjectName("playlist.status.label");
    status_->setStyleSheet("color:#888; font-style:italic;");

    auto* row1 = new QHBoxLayout();
    row1->setContentsMargins(0, 0, 0, 0);
    row1->addWidget(addCurBtn);
    row1->addWidget(addFilesBtn);
    row1->addWidget(removeBtn);
    row1->addWidget(upBtn);
    row1->addWidget(downBtn);
    row1->addWidget(clearBtn);
    row1->addStretch(1);
    row1->addWidget(loadBtn);
    row1->addWidget(saveBtn);

    auto* row2 = new QHBoxLayout();
    row2->setContentsMargins(0, 0, 0, 0);
    row2->addWidget(compactCheck_);
    row2->addWidget(fullPathsCheck_);
    row2->addStretch(1);
    row2->addWidget(scaleOverrideCheck_);
    row2->addWidget(scaleCombo_);

    auto* row3 = new QHBoxLayout();
    row3->setContentsMargins(0, 0, 0, 0);
    row3->addWidget(autoAdvanceCheck_);
    row3->addWidget(loopCheck_);
    row3->addStretch(1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);
    outer->addWidget(list_, 1);
    outer->addLayout(row1);
    outer->addLayout(row2);
    outer->addLayout(row3);
    outer->addWidget(status_);

    applyScaleOverride();   // seed override from restored state
    refreshList();
}

void PlaylistPanel_Qt::applyScaleOverride() {
    const int pct = scaleOverrideCheck_->isChecked()
                    ? scaleCombo_->currentData().toInt() : 0;
    jefe::qt::setPlaylistScaleOverride(pct);
}

int PlaylistPanel_Qt::selectedRow() const {
    return list_ ? list_->currentRow() : -1;
}

void PlaylistPanel_Qt::refreshList() {
    const int prev = list_->currentRow();
    list_->clear();
    const auto names = jefe::qt::getPlaylistItemNames();
    const bool expanded = !compactCheck_->isChecked();
    const bool fullPaths = fullPathsCheck_->isChecked();
    const int selected = jefe::qt::getSelectedPlaylistItem();
    for (int i = 0; i < (int)names.size(); ++i) {
        auto* card = new PlaylistItemCard(
            i, QString::fromStdString(names[i]), expanded, fullPaths, list_);
        card->setSelectedHighlight(i == selected);
        connect(card, &PlaylistItemCard::loadRequested,
                this, &PlaylistPanel_Qt::loadRow);
        connect(card, &PlaylistItemCard::removeRequested, this, [this](int r) {
            jefe::qt::removePlaylistItem(r); refreshList();
        });
        connect(card, &PlaylistItemCard::toggleExpandRequested, this,
                [this](int r) {
            if (auto* it = list_->item(r))
                if (auto* c = qobject_cast<PlaylistItemCard*>(
                        list_->itemWidget(it))) {
                    c->setExpanded(!c->isExpanded());
                    it->setSizeHint(QSize(0, c->sizeHint().height()));
                }
        });
        auto* it = new QListWidgetItem(list_);
        it->setSizeHint(QSize(0, card->sizeHint().height()));
        list_->setItemWidget(it, card);
    }
    status_->setText(names.empty()
        ? QString("Playlist is empty.")
        : QString("%1 items").arg(names.size()));
    if (prev >= 0 && prev < list_->count()) list_->setCurrentRow(prev);
}

void PlaylistPanel_Qt::onAddCurrent() {
    jefe::qt::addCurrentAsPlaylistItem();
    refreshList();
    list_->setCurrentRow(list_->count() - 1);
}

void PlaylistPanel_Qt::onAddFiles() {
    QSettings s;
    const QString seed = s.value(kLastDirKey, QDir::homePath()).toString();
    const QStringList chosen = QFileDialog::getOpenFileNames(
        this, "Add files to playlist", seed,
        "Image sequences (*.dpx *.exr *.jpg *.jpeg *.png *.tif *.tiff *.tga *.bmp);;"
        "All files (*)");
    if (chosen.isEmpty()) return;
    s.setValue(kLastDirKey, QFileInfo(chosen.first()).absolutePath());
    std::vector<std::string> paths;
    for (const auto& p : chosen) paths.push_back(p.toStdString());
    jefe::qt::addPlaylistFiles(paths);
    refreshList();
    list_->setCurrentRow(list_->count() - 1);
}

void PlaylistPanel_Qt::onRemoveClicked() {
    const int r = selectedRow();
    if (r < 0) return;
    jefe::qt::removePlaylistItem(r);
    refreshList();
}

void PlaylistPanel_Qt::onUpClicked() {
    const int r = selectedRow();
    if (r <= 0) return;
    jefe::qt::movePlaylistItem(r, -1);
    refreshList();
    list_->setCurrentRow(r - 1);
}

void PlaylistPanel_Qt::onDownClicked() {
    const int r = selectedRow();
    if (r < 0 || r >= list_->count() - 1) return;
    jefe::qt::movePlaylistItem(r, +1);
    refreshList();
    list_->setCurrentRow(r + 1);
}

void PlaylistPanel_Qt::onClearClicked() {
    jefe::qt::clearPlaylist();
    refreshList();
}

void PlaylistPanel_Qt::onSaveClicked() {
    QSettings s;
    const QString seed = s.value(kLastDirKey, QDir::homePath()).toString();
    QString chosen = QFileDialog::getSaveFileName(
        this, "Save playlist", seed, "JefeCheck playlist (*.jpl)");
    if (chosen.isEmpty()) return;
    s.setValue(kLastDirKey, QFileInfo(chosen).absolutePath());
    jefe::qt::savePlaylistFile(chosen.toStdString());
    status_->setText(QString("Saved %1 items").arg(list_->count()));
}

void PlaylistPanel_Qt::onLoadClicked() {
    QSettings s;
    const QString seed = s.value(kLastDirKey, QDir::homePath()).toString();
    const QString chosen = QFileDialog::getOpenFileName(
        this, "Load playlist", seed, "JefeCheck playlist (*.jpl);;All files (*)");
    if (chosen.isEmpty()) return;
    s.setValue(kLastDirKey, QFileInfo(chosen).absolutePath());
    jefe::qt::loadPlaylistFile(chosen.toStdString());
    refreshList();
}

void PlaylistPanel_Qt::loadRow(int row) {
    if (row < 0 || row >= list_->count()) return;
    jefe::qt::loadPlaylistItem(row);
    refreshList();   // refresh highlight
}

void PlaylistPanel_Qt::advanceToNext() {
    if (!autoAdvanceCheck_ || !autoAdvanceCheck_->isChecked()) return;
    const int cur = jefe::qt::getSelectedPlaylistItem();
    const int count = (int)jefe::qt::getPlaylistItemNames().size();
    if (count <= 0) { jefe::qt::pausePlaybackIfPlaying(); return; }
    int next = cur + 1;
    if (next >= count) {
        if (loopCheck_->isChecked()) next = 0;
        else { jefe::qt::pausePlaybackIfPlaying(); return; }
    }
    if (next < 0) next = 0;
    jefe::qt::loadPlaylistItemAndPlay(next);
    refreshList();
}

void PlaylistPanel_Qt::showContextMenu(const QPoint& pos) {
    const int r = list_->currentRow();
    QMenu menu(this);
    QAction* load = menu.addAction("Load");
    QAction* append = menu.addAction("Append tracks…");
    QAction* remove = menu.addAction("Remove");
    menu.addSeparator();
    QAction* up = menu.addAction("Move up");
    QAction* down = menu.addAction("Move down");
    menu.addSeparator();
    QAction* full = menu.addAction("Show full paths");
    full->setCheckable(true); full->setChecked(fullPathsCheck_->isChecked());
    QAction* compact = menu.addAction("Compact view");
    compact->setCheckable(true); compact->setChecked(compactCheck_->isChecked());
    QAction* picked = menu.exec(list_->viewport()->mapToGlobal(pos));
    if (!picked) return;
    if (picked == load && r >= 0) loadRow(r);
    else if (picked == remove && r >= 0) { jefe::qt::removePlaylistItem(r); refreshList(); }
    else if (picked == up) onUpClicked();
    else if (picked == down) onDownClicked();
    else if (picked == full) fullPathsCheck_->setChecked(full->isChecked());
    else if (picked == compact) compactCheck_->setChecked(compact->isChecked());
    else if (picked == append && r >= 0) {
        QSettings s;
        const QString seed = s.value(kLastDirKey, QDir::homePath()).toString();
        const QStringList files = QFileDialog::getOpenFileNames(
            this, "Append tracks", seed, "All files (*)");
        if (files.isEmpty()) return;
        std::vector<std::string> paths;
        for (const auto& p : files) paths.push_back(p.toStdString());
        jefe::qt::appendTracksToPlaylistItem(r, paths);
        refreshList();
    }
}

bool PlaylistPanel_Qt::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::KeyPress &&
        (obj == list_ || obj == list_->viewport())) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        const int r = selectedRow();
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (r >= 0) { loadRow(r); return true; }
        } else if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
            if (r >= 0) { jefe::qt::removePlaylistItem(r); refreshList(); return true; }
        } else if ((ke->modifiers() & Qt::ShiftModifier) &&
                   ke->key() == Qt::Key_Up) {
            onUpClicked(); return true;
        } else if ((ke->modifiers() & Qt::ShiftModifier) &&
                   ke->key() == Qt::Key_Down) {
            onDownClicked(); return true;
        }
    }
    // DropOnly's model rejects URL mime by default, which would suppress the
    // Drop event entirely — accept drag-enter/move for URL payloads so the
    // Drop below actually fires for Finder/Explorer drags.
    if ((ev->type() == QEvent::DragEnter || ev->type() == QEvent::DragMove) &&
        (obj == list_ || obj == list_->viewport())) {
        auto* de = static_cast<QDragMoveEvent*>(ev);
        if (de->mimeData()->hasUrls()) {
            de->acceptProposedAction();
            return true;
        }
    }
    if (ev->type() == QEvent::Drop &&
        (obj == list_ || obj == list_->viewport())) {
        auto* de = static_cast<QDropEvent*>(ev);
        if (de->mimeData()->hasUrls()) {
            QStringList media;
            QString jpl;
            for (const auto& u : de->mimeData()->urls()) {
                const QString lf = u.toLocalFile();
                if (lf.endsWith(".jpl", Qt::CaseInsensitive)) jpl = lf;
                else if (!lf.isEmpty()) media << lf;
            }
            if (!jpl.isEmpty()) {
                jefe::qt::loadPlaylistFile(jpl.toStdString());
                refreshList(); de->acceptProposedAction(); return true;
            }
            if (!media.isEmpty()) {
                // Drop on a card -> append to that item; else new item.
                QListWidgetItem* it = list_->itemAt(
                    de->position().toPoint());
                std::vector<std::string> paths;
                for (const auto& p : media) paths.push_back(p.toStdString());
                if (it) jefe::qt::appendTracksToPlaylistItem(
                            list_->row(it), paths);
                else jefe::qt::addPlaylistFiles(paths);
                refreshList(); de->acceptProposedAction(); return true;
            }
        }
    }
    return QWidget::eventFilter(obj, ev);
}
