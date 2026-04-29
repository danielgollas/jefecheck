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
    setObjectName("fxstack.panel");
    setAccessibleName("FX Stack");

    // Available list: every FX fxManager has loaded. Stack list: the
    // FXs currently on the active plate, top-down in render order.
    auto* availableLabel = new QLabel("Available FXs", this);
    availableLabel->setStyleSheet("color: #888;");
    availableLabel->setObjectName("fxstack.available.label");

    available_ = new QListWidget(this);
    available_->setSelectionMode(QAbstractItemView::SingleSelection);
    available_->setAlternatingRowColors(true);
    available_->setObjectName("fxstack.available.list");
    available_->setAccessibleName("Available FXs");
    connect(available_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { addSelected(); });

    auto* stackLabel = new QLabel("On active plate", this);
    stackLabel->setStyleSheet("color: #888;");
    stackLabel->setObjectName("fxstack.stack.label");

    stack_ = new QListWidget(this);
    stack_->setSelectionMode(QAbstractItemView::SingleSelection);
    stack_->setAlternatingRowColors(true);
    stack_->setObjectName("fxstack.stack.list");
    stack_->setAccessibleName("Active plate FX stack");

    addBtn_ = new QPushButton("Add to active plate", this);
    addBtn_->setObjectName("fxstack.add.button");
    addBtn_->setAccessibleName("Add FX to active plate");
    connect(addBtn_, &QPushButton::clicked,
            this, [this]() { addSelected(); });

    removeBtn_ = new QPushButton("Remove from stack", this);
    removeBtn_->setObjectName("fxstack.remove.button");
    removeBtn_->setAccessibleName("Remove FX from stack");
    connect(removeBtn_, &QPushButton::clicked,
            this, [this]() { removeSelected(); });

    auto* refreshBtn = new QPushButton("Refresh", this);
    refreshBtn->setObjectName("fxstack.refresh.button");
    refreshBtn->setAccessibleName("Refresh FX list");
    connect(refreshBtn, &QPushButton::clicked,
            this, &FXStackPanel_Qt::refreshLists);

    status_ = new QLabel(this);
    status_->setStyleSheet("color: #888; font-style: italic;");
    status_->setObjectName("fxstack.status.label");
    status_->setAccessibleName("FX status");

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(addBtn_);
    row->addWidget(removeBtn_);
    row->addWidget(refreshBtn);
    row->addStretch(1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);
    outer->addWidget(availableLabel);
    outer->addWidget(available_, /*stretch*/ 1);
    outer->addWidget(stackLabel);
    outer->addWidget(stack_, /*stretch*/ 1);
    outer->addLayout(row);
    outer->addWidget(status_);

    refreshLists();
}

void FXStackPanel_Qt::refreshLists() {
    // Available FXs come from fxManager (loaded via initializeInstallFXs).
    available_->clear();
    for (const auto& name : jefe::qt::getAvailableFXNames()) {
        available_->addItem(QString::fromStdString(name));
    }

    // Stack always reflects the *active* plate so the panel feels
    // tied to whichever quadrant the user is editing — same UX as
    // the LUT browser's "Apply to active plate".
    stack_->clear();
    const int active = jefe::qt::getActivePlate();
    if (active >= 0) {
        for (const auto& name : jefe::qt::getFXStackOnPlate(active)) {
            stack_->addItem(QString::fromStdString(name));
        }
    }

    // No fancy state on the buttons — the bridge tolerates an
    // empty selection (returns early). Cheap, fewer signals to wire.
}

void FXStackPanel_Qt::addSelected() {
    const int row = available_->currentRow();
    if (row < 0) return;
    jefe::qt::addFXToActivePlate(row);
    refreshLists();
    if (status_ && available_->item(row)) {
        status_->setText(QString("Added: %1")
                             .arg(available_->item(row)->text()));
    }
}

void FXStackPanel_Qt::removeSelected() {
    const int row = stack_->currentRow();
    if (row < 0) return;
    const int active = jefe::qt::getActivePlate();
    if (active < 0) return;
    const auto* item = stack_->item(row);
    const QString removedName = item ? item->text() : QString();
    jefe::qt::removeFXFromPlate(active, row);
    refreshLists();
    if (status_) {
        status_->setText(QString("Removed: %1").arg(removedName));
    }
}

LUTPanel_Qt::LUTPanel_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("lut.panel");
    setAccessibleName("LUT browser");
    setAcceptDrops(true);

    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setAlternatingRowColors(true);
    list_->setObjectName("lut.list");
    list_->setAccessibleName("LUT list");
    // Double-click is "apply"; matches the FLTK LUT browser's UX.
    connect(list_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { applySelected(); });

    auto* applyBtn = new QPushButton("Apply to active plate", this);
    applyBtn->setObjectName("lut.apply.button");
    applyBtn->setAccessibleName("Apply LUT to active plate");
    connect(applyBtn, &QPushButton::clicked, this, [this]() { applySelected(); });

    auto* refreshBtn = new QPushButton("Refresh", this);
    refreshBtn->setObjectName("lut.refresh.button");
    refreshBtn->setAccessibleName("Refresh LUT list");
    connect(refreshBtn, &QPushButton::clicked, this, &LUTPanel_Qt::refreshList);

    status_ = new QLabel("Drop .lut / .cube / .cub files here to load.", this);
    status_->setStyleSheet("color: #888; font-style: italic;");
    status_->setObjectName("lut.status.label");
    status_->setAccessibleName("LUT status");

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
