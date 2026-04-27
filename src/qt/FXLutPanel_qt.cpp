#include "FXLutPanel_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

FXStackPanel_Qt::FXStackPanel_Qt(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(new QLabel("FX Stack — placeholder", this));
    layout->addStretch(1);
}

LUTPanel_Qt::LUTPanel_Qt(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);

    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setAlternatingRowColors(true);
    // Double-click is "apply"; matches the FLTK LUT browser's UX.
    connect(list_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { applySelected(); });

    auto* applyBtn = new QPushButton("Apply to active plate", this);
    connect(applyBtn, &QPushButton::clicked, this, [this]() { applySelected(); });

    auto* refreshBtn = new QPushButton("Refresh", this);
    connect(refreshBtn, &QPushButton::clicked, this, &LUTPanel_Qt::refreshList);

    status_ = new QLabel("Drop .lut / .cube / .cub files here to load.", this);
    status_->setStyleSheet("color: #888; font-style: italic;");

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(applyBtn);
    row->addWidget(refreshBtn);
    row->addStretch(1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);
    outer->addWidget(status_);
    outer->addWidget(list_, /*stretch*/ 1);
    outer->addLayout(row);

    refreshList();
}

void LUTPanel_Qt::refreshList() {
    list_->clear();
    // Index 0 is the implicit "No LUT" slot in plate.myGUI->getLUT() —
    // mirror that here so item 0 selects "no LUT" rather than the
    // first loaded LUT (which would be index 1 elsewhere).
    list_->addItem("(No LUT)");
    const auto names = jefe::qt::getLutNames();
    for (const auto& n : names) {
        list_->addItem(QString::fromStdString(n));
    }

    // Pre-select the LUT currently on the active plate so the user can
    // see what's applied without clicking. Index in lutManager's name
    // list maps 1:1 onto plate.LUT (after the offset).
    const int active = jefe::qt::getLUTOnActivePlate();
    if (active >= 0 && active < list_->count()) {
        list_->setCurrentRow(active);
    }
}

void LUTPanel_Qt::applySelected() {
    const int row = list_->currentRow();
    if (row < 0) return;
    jefe::qt::applyLUTToActivePlate(row);
    if (status_) {
        const auto* item = list_->currentItem();
        status_->setText(item
            ? QString("Applied: %1").arg(item->text())
            : QString("LUT cleared"));
    }
}

void LUTPanel_Qt::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
    } else {
        e->ignore();
    }
}

void LUTPanel_Qt::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasUrls()) {
        e->ignore();
        return;
    }
    int loaded = 0;
    for (const QUrl& u : e->mimeData()->urls()) {
        if (!u.isLocalFile()) continue;
        if (jefe::qt::loadLUTFile(u.toLocalFile().toStdString())) {
            ++loaded;
        }
    }
    if (loaded > 0) {
        refreshList();
        if (status_) {
            status_->setText(QString("Loaded %1 LUT file(s)").arg(loaded));
        }
    }
    e->acceptProposedAction();
}
