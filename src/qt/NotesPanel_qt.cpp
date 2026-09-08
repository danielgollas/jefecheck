#include "NotesPanel_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// Item-data roles on each QTreeWidgetItem, beyond the display text.
constexpr int kRoleId         = Qt::UserRole;      // QString: note or revision id
constexpr int kRoleIsRevision = Qt::UserRole + 1;   // bool: true for a revision header row
constexpr int kRoleFromFrame  = Qt::UserRole + 2;   // int: note's `from` frame (note rows only)

QString noteTypeName(int typeIndex) {
    switch (typeIndex) {
        case jefe::qt::NOTETOOL_ARROW: return QStringLiteral("Arrow");
        case jefe::qt::NOTETOOL_BOX:   return QStringLiteral("Box");
        case jefe::qt::NOTETOOL_TEXT:  return QStringLiteral("Text");
        default:                       return QStringLiteral("Freehand");
    }
}

QString noteRowLabel(const jefe::qt::NoteRow& nr) {
    QString frameRange = nr.always
        ? QStringLiteral("always")
        : (nr.from == nr.to ? QString::number(nr.from)
                            : QStringLiteral("%1-%2").arg(nr.from).arg(nr.to));
    return QStringLiteral("%1 — %2 (%3)")
        .arg(noteTypeName(nr.typeIndex))
        .arg(QString::fromStdString(nr.author))
        .arg(frameRange);
}

QString revisionRowLabel(const jefe::qt::RevisionRow& rr) {
    const QDateTime when = QDateTime::fromSecsSinceEpoch(rr.created);
    QString label = QStringLiteral("%1 — %2 (%3 note%4)")
        .arg(QString::fromStdString(rr.author))
        .arg(when.toString(QStringLiteral("yyyy-MM-dd HH:mm")))
        .arg(rr.notes.size())
        .arg(rr.notes.size() == 1 ? QString() : QStringLiteral("s"));
    if (rr.locked) label += QStringLiteral("  [locked]");
    return label;
}

}  // namespace

NotesPanel_Qt::NotesPanel_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("notes.panel");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName("notes.status.label");
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    // Tool picker: freehand / arrow / box / text, exactly one active.
    auto* toolRow = new QHBoxLayout();
    toolRow->setSpacing(4);
    toolFreehand_ = new QToolButton(this);
    toolFreehand_->setObjectName("notes.tool.freehand");
    toolFreehand_->setText(tr("Freehand"));
    toolFreehand_->setCheckable(true);
    toolFreehand_->setAccessibleName("Freehand note tool");
    toolArrow_ = new QToolButton(this);
    toolArrow_->setObjectName("notes.tool.arrow");
    toolArrow_->setText(tr("Arrow"));
    toolArrow_->setCheckable(true);
    toolArrow_->setAccessibleName("Arrow note tool");
    toolBox_ = new QToolButton(this);
    toolBox_->setObjectName("notes.tool.box");
    toolBox_->setText(tr("Box"));
    toolBox_->setCheckable(true);
    toolBox_->setAccessibleName("Box note tool");
    toolText_ = new QToolButton(this);
    toolText_->setObjectName("notes.tool.text");
    toolText_->setText(tr("Text"));
    toolText_->setCheckable(true);
    toolText_->setAccessibleName("Text note tool");
    toolRow->addWidget(toolFreehand_);
    toolRow->addWidget(toolArrow_);
    toolRow->addWidget(toolBox_);
    toolRow->addWidget(toolText_);
    layout->addLayout(toolRow);

    auto* toolGroup = new QButtonGroup(this);
    toolGroup->setExclusive(true);
    toolGroup->addButton(toolFreehand_, jefe::qt::NOTETOOL_FREEHAND);
    toolGroup->addButton(toolArrow_,    jefe::qt::NOTETOOL_ARROW);
    toolGroup->addButton(toolBox_,      jefe::qt::NOTETOOL_BOX);
    toolGroup->addButton(toolText_,     jefe::qt::NOTETOOL_TEXT);
    connect(toolGroup, &QButtonGroup::idClicked, this,
            [](int id) { jefe::qt::setActiveNoteTool(id); });
    switch (jefe::qt::activeNoteTool()) {
        case jefe::qt::NOTETOOL_ARROW: toolArrow_->setChecked(true); break;
        case jefe::qt::NOTETOOL_BOX:   toolBox_->setChecked(true); break;
        case jefe::qt::NOTETOOL_TEXT:  toolText_->setChecked(true); break;
        default:                       toolFreehand_->setChecked(true); break;
    }

    // Colour + size, on one row.
    auto* styleRow = new QHBoxLayout();
    styleRow->setSpacing(6);
    styleRow->addWidget(new QLabel(tr("Colour:"), this));
    colorButton_ = new QPushButton(this);
    colorButton_->setObjectName("notes.color.button");
    colorButton_->setFixedSize(40, 22);
    colorButton_->setAccessibleName("Note colour");
    colorButton_->setToolTip(tr("Colour used for the next note drawn with the active tool."));
    connect(colorButton_, &QPushButton::clicked, this, &NotesPanel_Qt::onColorButtonClicked);
    styleRow->addWidget(colorButton_);
    styleRow->addWidget(new QLabel(tr("Size:"), this));
    sizeSpin_ = new QSpinBox(this);
    sizeSpin_->setObjectName("notes.size.spin");
    sizeSpin_->setRange(1, 20);
    sizeSpin_->setValue(jefe::qt::activeNoteSize());
    sizeSpin_->setAccessibleName("Note stroke size");
    connect(sizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &NotesPanel_Qt::onSizeChanged);
    styleRow->addWidget(sizeSpin_);
    styleRow->addStretch(1);
    layout->addLayout(styleRow);
    rebuildColorSwatch();

    // Revision / note tree. Top-level items are revisions; children are the
    // notes drawn on THIS plate within that revision (a review can hold a
    // sibling plate's notes too -- see gfcNote::quadID -- notesForPlate()
    // already filters those out).
    tree_ = new QTreeWidget(this);
    tree_->setObjectName("notes.revision.list");
    tree_->setHeaderHidden(true);
    tree_->setAccessibleName("Revisions and notes");
    connect(tree_, &QTreeWidget::itemClicked, this, &NotesPanel_Qt::onTreeItemClicked);
    connect(tree_, &QTreeWidget::itemSelectionChanged,
            this, &NotesPanel_Qt::updateActionButtons);
    layout->addWidget(tree_, 1);

    auto* actionRow = new QHBoxLayout();
    actionRow->setSpacing(6);
    lockButton_ = new QPushButton(tr("Lock Round"), this);
    lockButton_->setObjectName("notes.lock.button");
    connect(lockButton_, &QPushButton::clicked, this, &NotesPanel_Qt::onLockClicked);
    actionRow->addWidget(lockButton_);
    removeButton_ = new QPushButton(tr("Remove Note"), this);
    removeButton_->setObjectName("notes.remove.button");
    connect(removeButton_, &QPushButton::clicked, this, &NotesPanel_Qt::onRemoveClicked);
    actionRow->addWidget(removeButton_);
    layout->addLayout(actionRow);

    refresh();
}

void NotesPanel_Qt::rebuildColorSwatch() {
    float r = 1.0f, g = 0.2f, b = 0.2f;
    jefe::qt::getActiveNoteColor(r, g, b);
    QColor c;
    c.setRgbF(r, g, b);
    colorButton_->setStyleSheet(
        QStringLiteral("background:%1; border:1px solid #555; border-radius:4px;")
            .arg(c.name()));
}

void NotesPanel_Qt::refresh() {
    const int plate = jefe::qt::getActivePlate();
    const bool hasMedia = plate >= 0 && jefe::qt::notesAvailableForPlate(plate);

    tree_->clear();
    if (hasMedia) {
        for (const auto& rr : jefe::qt::notesForPlate(plate)) {
            auto* revItem = new QTreeWidgetItem(tree_);
            revItem->setText(0, revisionRowLabel(rr));
            revItem->setData(0, kRoleId, QString::fromStdString(rr.id));
            revItem->setData(0, kRoleIsRevision, true);
            // Revision headers are for grouping only -- clicking one has
            // no frame to jump to, and it isn't removable itself.
            revItem->setFlags(revItem->flags() & ~Qt::ItemIsSelectable);
            for (const auto& nr : rr.notes) {
                auto* noteItem = new QTreeWidgetItem(revItem);
                noteItem->setText(0, noteRowLabel(nr));
                noteItem->setData(0, kRoleId, QString::fromStdString(nr.id));
                noteItem->setData(0, kRoleIsRevision, false);
                noteItem->setData(0, kRoleFromFrame, nr.from);
                QColor c;
                c.setRgbF(nr.colorR, nr.colorG, nr.colorB);
                noteItem->setForeground(0, c);
            }
            revItem->setExpanded(true);
        }
    }

    statusLabel_->setText(hasMedia
        ? tr("Notes for the active plate.")
        : tr("No media loaded on the active plate."));

    toolFreehand_->setEnabled(hasMedia);
    toolArrow_->setEnabled(hasMedia);
    toolBox_->setEnabled(hasMedia);
    toolText_->setEnabled(hasMedia);
    colorButton_->setEnabled(hasMedia);
    sizeSpin_->setEnabled(hasMedia);
    tree_->setEnabled(hasMedia);

    updateActionButtons();
}

void NotesPanel_Qt::updateActionButtons() {
    const int plate = jefe::qt::getActivePlate();
    const bool hasMedia = plate >= 0 && jefe::qt::notesAvailableForPlate(plate);

    if (!hasMedia) {
        lockButton_->setText(tr("Lock Round"));
        lockButton_->setEnabled(false);
        removeButton_->setEnabled(false);
        return;
    }

    // A locked revision refuses edits in the model (gfcRevision::addNote /
    // removeNote return false) -- but per the plan, the UI must refuse them
    // too, not just rely on that: a disabled control beats a silently
    // ignored click. Lock/Unlock is also gated to the session host (or solo
    // use, no session) -- jefe::qt::isNotesHost().
    const int lockState = jefe::qt::noteLockState(plate);
    const bool isHost = jefe::qt::isNotesHost();
    if (lockState == jefe::qt::NOTELOCK_LOCKED) {
        lockButton_->setText(tr("Unlock Round"));
        lockButton_->setEnabled(isHost);
    } else {
        lockButton_->setText(tr("Lock Round"));
        lockButton_->setEnabled(isHost && lockState == jefe::qt::NOTELOCK_OPEN);
    }

    QTreeWidgetItem* sel = tree_->currentItem();
    bool canRemove = false;
    if (sel && !sel->data(0, kRoleIsRevision).toBool()) {
        const std::string id = sel->data(0, kRoleId).toString().toStdString();
        canRemove = jefe::qt::canRemoveNote(plate, id);
    }
    removeButton_->setEnabled(canRemove);
}

void NotesPanel_Qt::onColorButtonClicked() {
    float r = 1.0f, g = 0.2f, b = 0.2f;
    jefe::qt::getActiveNoteColor(r, g, b);
    QColor start;
    start.setRgbF(r, g, b);
    const QColor chosen = QColorDialog::getColor(start, this, tr("Note Colour"));
    if (chosen.isValid()) {
        jefe::qt::setActiveNoteColor(float(chosen.redF()), float(chosen.greenF()),
                                      float(chosen.blueF()));
        rebuildColorSwatch();
    }
}

void NotesPanel_Qt::onSizeChanged(int value) {
    jefe::qt::setActiveNoteSize(value);
}

void NotesPanel_Qt::onTreeItemClicked(QTreeWidgetItem* item, int /*column*/) {
    if (!item || item->data(0, kRoleIsRevision).toBool()) return;
    jefe::qt::seekToFrame(item->data(0, kRoleFromFrame).toInt());
    emit viewportRepaintRequested();
}

void NotesPanel_Qt::onLockClicked() {
    const int plate = jefe::qt::getActivePlate();
    if (plate < 0) return;
    bool ok = false;
    switch (jefe::qt::noteLockState(plate)) {
        case jefe::qt::NOTELOCK_OPEN:   ok = jefe::qt::lockOpenRevision(plate); break;
        case jefe::qt::NOTELOCK_LOCKED: ok = jefe::qt::unlockLatestRevision(plate); break;
        default: break;
    }
    if (ok) {
        refresh();
        emit viewportRepaintRequested();
    }
}

void NotesPanel_Qt::onRemoveClicked() {
    const int plate = jefe::qt::getActivePlate();
    QTreeWidgetItem* sel = tree_->currentItem();
    if (plate < 0 || !sel || sel->data(0, kRoleIsRevision).toBool()) return;
    const std::string id = sel->data(0, kRoleId).toString().toStdString();
    if (jefe::qt::removeNoteFromPlate(plate, id)) {
        refresh();
        emit viewportRepaintRequested();
    }
}
