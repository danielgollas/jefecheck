#include "FXLutPanel_qt.h"
#include "SequenceLoadBridge_qt.h"
#include "LUTPreview_qt.h"

#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

namespace {
// Roles for sortable/lookup data carried on each LUT row.
constexpr int kGuiIndexRole = Qt::UserRole;       // panel gui index (col 0)
constexpr int kSortIntRole  = Qt::UserRole + 1;    // numeric sort key per column

// QTreeWidgetItem that sorts the Size/Depth columns numerically (by the int
// stashed in kSortIntRole) instead of lexically by display text.
class LutRow : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other) const override {
        const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
        const QVariant a = data(col, kSortIntRole);
        const QVariant b = other.data(col, kSortIntRole);
        if (a.isValid() && b.isValid()) return a.toInt() < b.toInt();
        return text(col).localeAwareCompare(other.text(col)) < 0;
    }
};
}  // namespace

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
    emit stackChanged();
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
    emit stackChanged();
    if (status_) {
        status_->setText(QString("Removed: %1").arg(removedName));
    }
}

LUTPanel_Qt::LUTPanel_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("lut.panel");
    setAccessibleName("LUT browser");
    setAcceptDrops(true);

    table_ = new QTreeWidget(this);
    table_->setObjectName("lut.list");
    table_->setAccessibleName("LUT list");
    table_->setColumnCount(4);
    table_->setHeaderLabels({"Name", "Type", "Size", "Depth"});
    table_->setRootIsDecorated(false);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->setSortingEnabled(true);
    table_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->header()->setStretchLastSection(false);
    // Double-click is "apply"; matches the FLTK LUT browser's UX.
    connect(table_, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem*, int) { applySelected(); });

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

    previewToggle_ = new QCheckBox("Preview", this);
    previewToggle_->setChecked(true);
    previewToggle_->setObjectName("lut.preview.toggle");
    previewToggle_->setAccessibleName("Show LUT preview");

    preview_ = new LUTPreview_Qt(this);

    // Browser part (status + list + buttons) goes in the top of a vertical
    // splitter; the preview pane in the bottom — so the user can drag the
    // divider to give the cube room.
    auto* browser = new QWidget(this);
    auto* browserL = new QVBoxLayout(browser);
    browserL->setContentsMargins(0, 0, 0, 0);
    browserL->setSpacing(6);
    browserL->addWidget(status_);
    browserL->addWidget(table_, /*stretch*/ 1);
    browserL->addLayout(row);
    browserL->addWidget(previewToggle_);   // toggle sits under the list/buttons

    table_->setMinimumHeight(60);  // let the table shrink so the splitter has travel

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->setObjectName("lut.splitter");
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(8);   // grabbable handle (macOS default is too thin)
    splitter->addWidget(browser);
    splitter->addWidget(preview_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);   // bias initial space toward the cube
    splitter->setSizes({160, 320});
    // A visible handle so it's clear the panes are draggable.
    splitter->setStyleSheet(
        "QSplitter::handle { background: #3a3a3a; }"
        "QSplitter::handle:hover { background: #555; }");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);
    outer->addWidget(splitter, /*stretch*/ 1);

    refreshList();

    connect(previewToggle_, &QCheckBox::toggled, this, [this](bool on) {
        preview_->setVisible(on);   // collapses the preview pane in the splitter
        if (on) updatePreview();
    });
    connect(table_, &QTreeWidget::itemSelectionChanged, this, [this]() {
        updatePreview();
    });
    updatePreview();
}

int LUTPanel_Qt::selectedGuiIndex() const {
    const auto items = table_->selectedItems();
    if (items.isEmpty()) return -1;
    return items.first()->data(0, kGuiIndexRole).toInt();
}

void LUTPanel_Qt::refreshList() {
    const bool wasSorting = table_->isSortingEnabled();
    table_->setSortingEnabled(false);    // don't re-sort mid-populate
    table_->clear();

    // Row 0 = the implicit "No LUT" slot (gui index 0). No metadata columns.
    auto* none = new LutRow(table_);
    none->setText(0, "(No LUT)");
    none->setData(0, kGuiIndexRole, 0);

    const auto sums = jefe::qt::getLutSummaries();
    for (int i = 0; i < (int)sums.size(); ++i) {
        const auto& s = sums[i];
        auto* it = new LutRow(table_);
        it->setText(0, QString::fromStdString(s.name));
        it->setText(1, s.is3D ? "3D" : "1D");
        it->setText(2, s.is3D ? QString("%1³").arg(s.size)
                              : QString("%1").arg(s.size));
        it->setText(3, QString("%1→%2").arg(s.fromBits).arg(s.toBits));
        it->setData(0, kGuiIndexRole, i + 1);     // gui index (row 0 was none)
        it->setData(1, kSortIntRole, s.is3D ? 1 : 0);
        it->setData(2, kSortIntRole, s.size);
        it->setData(3, kSortIntRole, s.toBits);
    }
    table_->setSortingEnabled(wasSorting);

    // Pre-select the LUT currently on the active plate so the user sees
    // what's applied. Match by the stored gui index (row order may be sorted).
    const int active = jefe::qt::getLUTOnActivePlate();
    for (int r = 0; r < table_->topLevelItemCount(); ++r) {
        auto* it = table_->topLevelItem(r);
        if (it->data(0, kGuiIndexRole).toInt() == active) {
            table_->setCurrentItem(it);
            break;
        }
    }
    updatePreview();
}

void LUTPanel_Qt::updatePreview() {
    // Null-guarded: refreshList() runs in the ctor before preview_ exists.
    if (!preview_ || !previewToggle_ || !previewToggle_->isChecked()) return;
    preview_->setLut(jefe::qt::getLutPreview(selectedGuiIndex()));
}

void LUTPanel_Qt::applySelected() {
    const int idx = selectedGuiIndex();
    if (idx < 0) return;
    jefe::qt::applyLUTToActivePlate(idx);
    if (status_) {
        const auto items = table_->selectedItems();
        status_->setText(!items.isEmpty()
            ? QString("Applied: %1").arg(items.first()->text(0))
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
