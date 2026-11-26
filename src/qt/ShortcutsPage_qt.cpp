// SPDX-License-Identifier: GPL-2.0-or-later
// Preferences -> Shortcuts page. See ShortcutsPage_qt.h.
#include "ShortcutsPage_qt.h"

#include "FXParamEditors_qt.h"      // kFXModifiedColor: the FX panel's "changed" orange
#include "FlowLayout_qt.h"
#include "SequenceLoadBridge_qt.h"  // getDefaultBrowsePath
#include "ShortcutKeyboard_qt.h"
#include "UiMetrics_qt.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <memory>

namespace sc = jefe::shortcuts;
namespace ui = jefe::qt::ui;

namespace {
constexpr int kRoleId = Qt::UserRole + 1;
constexpr int kRoleKind = Qt::UserRole + 2;

// The list's model: command and hold rows can be dragged out (their id as the command mime
// type, for a drop on the keyboard map); groups and the read-only panel rows cannot, and
// nothing can be dropped in.
class CommandListModel : public QStandardItemModel {
public:
    using QStandardItemModel::QStandardItemModel;
    Qt::ItemFlags flags(const QModelIndex& idx) const override
    {
        Qt::ItemFlags f = QStandardItemModel::flags(idx) & ~Qt::ItemIsDropEnabled;
        const int kind = idx.siblingAtColumn(0).data(kRoleKind).toInt();
        if (!idx.isValid() || (kind != 1 && kind != 2)) f &= ~Qt::ItemIsDragEnabled;  // CommandRow, HoldRow
        return f;
    }
    QStringList mimeTypes() const override { return {ShortcutKeyboard_Qt::commandMimeType()}; }
    QMimeData* mimeData(const QModelIndexList& indexes) const override
    {
        for (const QModelIndex& i : indexes) {
            const int kind = i.siblingAtColumn(0).data(kRoleKind).toInt();
            if (kind != 1 && kind != 2) continue;
            auto* m = new QMimeData;
            m->setData(ShortcutKeyboard_Qt::commandMimeType(), i.siblingAtColumn(0).data(kRoleId).toString().toUtf8());
            return m;
        }
        return nullptr;
    }
};
}  // namespace

namespace {

constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kKindRole = Qt::UserRole + 2;

constexpr int kColName = 0;
constexpr int kColKey0 = 1;
constexpr int kColKey1 = 2;
constexpr int kColReset = 3;

const QString kResetModified = QString(QChar(0x25CF));  // the FX panel's reset dot (filled)
const QColor kChanged(jefe::qt::kFXModifiedColor);
const QColor kConflict(0xe0, 0x5a, 0x4a);
const QColor kReadOnly(0x8a, 0x8a, 0x8a);

QString categoryName(sc::Category c)
{
    switch (c) {
    case sc::Category::File: return QStringLiteral("File");
    case sc::Category::Playback: return QStringLiteral("Playback");
    case sc::Category::InOut: return QStringLiteral("In / Out");
    case sc::Category::Timeline: return QStringLiteral("Timeline");
    case sc::Category::Plate: return QStringLiteral("Plate");
    case sc::Category::Color: return QStringLiteral("Color");
    case sc::Category::Layout: return QStringLiteral("Layout");
    case sc::Category::Panels: return QStringLiteral("Panels");
    case sc::Category::Help: return QStringLiteral("Help");
    }
    return QStringLiteral("Other");
}

constexpr sc::Category kCategoryOrder[] = {
    sc::Category::File,  sc::Category::Playback, sc::Category::InOut,  sc::Category::Timeline, sc::Category::Plate,
    sc::Category::Color, sc::Category::Layout,   sc::Category::Panels, sc::Category::Help};

// Keys that mean something only inside one widget (not in the registry; spec "Scopes").
struct PanelKey { const char* id; const char* name; QList<QKeySequence> keys; };
QList<PanelKey> panelKeys()
{
    return {
        {"panel.playlist-load", "Playlist: load the selected item", {QKeySequence(Qt::Key_Return)}},
        {"panel.playlist-remove", "Playlist: remove the selected item",
         {QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)}},
        {"panel.playlist-move", "Playlist: move the selected item up / down",
         {QKeySequence(Qt::SHIFT | Qt::Key_Up), QKeySequence(Qt::SHIFT | Qt::Key_Down)}},
        {"panel.lut-frame", "LUT cube: frame the cube", {QKeySequence(Qt::Key_F)}},
        {"panel.fx-edit", "FX value field: type a value", {QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_F2)}},
        {"panel.fx-commit", "FX value field: commit / cancel typing",
         {QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Escape)}},
    };
}

QString native(const QKeySequence& k) { return k.toString(QKeySequence::NativeText); }

QByteArray idBytes(const QString& id) { return id.toUtf8(); }

QString modifierName(Qt::KeyboardModifier m)
{
#ifdef Q_OS_MACOS
    switch (m) {  // Qt's Control is Cmd on macOS, Meta the Control key
    case Qt::ShiftModifier: return QStringLiteral("Shift");
    case Qt::MetaModifier: return QStringLiteral("Control");
    case Qt::AltModifier: return QStringLiteral("Option");
    case Qt::ControlModifier: return QStringLiteral("Command");
    default: return {};
    }
#else
    switch (m) {
    case Qt::ShiftModifier: return QStringLiteral("Shift");
    case Qt::ControlModifier: return QStringLiteral("Ctrl");
    case Qt::AltModifier: return QStringLiteral("Alt");
    default: return QStringLiteral("Meta");
    }
#endif
}

bool isModifierOnly(int key)
{
    switch (key) {
    case 0:
    case Qt::Key_unknown:
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
    case Qt::Key_Mode_switch:
        return true;
    default:
        return false;
    }
}

// Shift + digit on a US layout arrives as the symbol; the presets store "Shift+1".
int shiftedDigit(int key)
{
    switch (key) {
    case Qt::Key_Exclam: return Qt::Key_1;
    case Qt::Key_At: return Qt::Key_2;
    case Qt::Key_NumberSign: return Qt::Key_3;
    case Qt::Key_Dollar: return Qt::Key_4;
    case Qt::Key_Percent: return Qt::Key_5;
    case Qt::Key_AsciiCircum: return Qt::Key_6;
    case Qt::Key_Ampersand: return Qt::Key_7;
    case Qt::Key_Asterisk: return Qt::Key_8;
    case Qt::Key_ParenLeft: return Qt::Key_9;
    case Qt::Key_ParenRight: return Qt::Key_0;
    default: return 0;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// ShortcutCaptureEdit_Qt
// ---------------------------------------------------------------------------

ShortcutCaptureEdit_Qt::ShortcutCaptureEdit_Qt(QWidget* parent) : QKeySequenceEdit(parent)
{
    setMaximumSequenceLength(1);
    setToolTip(tr("Press the new key. Esc cancels; Backspace unbinds."));
    if (auto* le = findChild<QLineEdit*>()) le->setPlaceholderText(tr("Press a key…"));
}

QKeySequence ShortcutCaptureEdit_Qt::keyFor(int key, Qt::KeyboardModifiers mods)
{
    if (isModifierOnly(key)) return {};
    mods &= (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (key == Qt::Key_Backtab) {  // Shift+Tab arrives as Backtab
        key = Qt::Key_Tab;
        mods |= Qt::ShiftModifier;
    }
    if (mods & Qt::ShiftModifier) {
        if (const int d = shiftedDigit(key)) {
            key = d;
        } else if (key > 0x20 && key < 0x7f && !(key >= Qt::Key_A && key <= Qt::Key_Z) &&
                   !(key >= Qt::Key_0 && key <= Qt::Key_9)) {
            // Other shifted symbols are the symbol itself ("<", "?"), as Qt matches them.
            mods &= ~Qt::ShiftModifier;
        }
    }
    const QKeySequence k(QKeyCombination(mods, Qt::Key(key)));
    if (k.isEmpty() || k[0].key() == Qt::Key_unknown) return {};
    // Only keys whose portable text reads back (what the registry stores and exports).
    const QKeySequence back = QKeySequence::fromString(k.toString(QKeySequence::PortableText),
                                                       QKeySequence::PortableText);
    return back == k ? k : QKeySequence();
}

void ShortcutCaptureEdit_Qt::keyPressEvent(QKeyEvent* e)
{
    e->accept();
    if (done_) return;
    const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
    const int key = e->key();
    if (key == Qt::Key_Escape && mods == Qt::NoModifier) {
        done_ = true;
        emit cancelled();
        return;
    }
    if ((key == Qt::Key_Backspace || key == Qt::Key_Delete) && mods == Qt::NoModifier) {
        done_ = true;
        emit unbound();
        return;
    }
    const QKeySequence k = keyFor(key, mods);
    if (k.isEmpty()) return;  // modifier alone or a key the registry cannot hold: keep waiting
    done_ = true;
    emit captured(k);
}

void ShortcutCaptureEdit_Qt::keyReleaseEvent(QKeyEvent* e) { e->accept(); }

void ShortcutCaptureEdit_Qt::focusOutEvent(QFocusEvent* e)
{
    QKeySequenceEdit::focusOutEvent(e);
    // Clicking elsewhere ends the capture; the window losing focus (a test, a notification) does not.
    if (done_ || e->reason() == Qt::ActiveWindowFocusReason || e->reason() == Qt::PopupFocusReason) return;
    done_ = true;
    emit cancelled();
}

// ---------------------------------------------------------------------------
// Delegate: hosts the capture editor and lets every key reach it (the default delegate
// filter would take Tab, Return and Escape for itself).
// ---------------------------------------------------------------------------

class ShortcutCaptureDelegate_Qt : public QStyledItemDelegate {
public:
    explicit ShortcutCaptureDelegate_Qt(ShortcutsPage_Qt* page) : QStyledItemDelegate(page), page_(page) {}

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        auto* e = new ShortcutCaptureEdit_Qt(parent);
        e->setObjectName(QStringLiteral("prefs.shortcuts.capture"));
        ShortcutsPage_Qt* page = page_;
        page->captureEditor_ = e;
        QObject::connect(e, &ShortcutCaptureEdit_Qt::captured, page, [page](const QKeySequence& k) { page->onCaptured(k); });
        QObject::connect(e, &ShortcutCaptureEdit_Qt::unbound, page, [page] { page->onUnbound(); });
        QObject::connect(e, &ShortcutCaptureEdit_Qt::cancelled, page, [page] { page->finishCapture(); });
        return e;
    }
    void setEditorData(QWidget*, const QModelIndex&) const override {}
    void setModelData(QWidget*, QAbstractItemModel*, const QModelIndex&) const override {}

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (qobject_cast<ShortcutCaptureEdit_Qt*>(o)) return false;
        return QStyledItemDelegate::eventFilter(o, e);
    }

private:
    ShortcutsPage_Qt* page_;
};

// ---------------------------------------------------------------------------
// ShortcutsPage_Qt
// ---------------------------------------------------------------------------

namespace {
QWidget* makeBar(QWidget* parent, const QString& name, QLabel** text, QList<QPushButton*> buttons)
{
    auto* bar = new QWidget(parent);
    bar->setObjectName(name);
    bar->setProperty("shortcutBar", true);
    bar->setAttribute(Qt::WA_StyledBackground, true);
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(ui::kGap, ui::kGapTight, ui::kGapTight, ui::kGapTight);
    row->setSpacing(ui::kGap);
    *text = new QLabel(bar);
    (*text)->setObjectName(name + QStringLiteral(".text"));
    (*text)->setWordWrap(true);
    row->addWidget(*text, 1);
    for (QPushButton* b : buttons) {
        b->setParent(bar);
        row->addWidget(b);
    }
    bar->hide();
    return bar;
}
}  // namespace

ShortcutsPage_Qt::ShortcutsPage_Qt(QWidget* parent) : QWidget(parent), reg_(sc::Registry::instance())
{
    setObjectName(QStringLiteral("prefs.shortcuts.page"));
    setStyleSheet(QStringLiteral("QWidget[shortcutBar=\"true\"] { background:#3a2f22; border:1px solid #5a4630;"
                                 " border-radius:4px; }"
                                 "QWidget[shortcutBar=\"true\"] QLabel { background:transparent; border:none; }"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(ui::kGapTight, 0, 0, 0);
    outer->setSpacing(ui::kGap);

    // Preset, (Custom), Import / Export / Reset: wraps to two lines when the dialog is narrow.
    auto* top = new QWidget(this);
    auto* flow = new FlowLayout_Qt(top, 0, ui::kGap, ui::kGapTight);
    auto* left = new QWidget(top);
    auto* leftRow = new QHBoxLayout(left);
    ui::applyRowMetrics(leftRow);
    auto* presetLabel = new QLabel(tr("Preset"), left);
    preset_ = new QComboBox(left);
    preset_->setObjectName(QStringLiteral("prefs.shortcuts.preset"));
    preset_->addItem(tr("Editing"), QStringLiteral("editing"));
    preset_->addItem(tr("JefeCheck 1.7"), QStringLiteral("classic"));
    preset_->setToolTip(tr("Editing follows Premiere, Resolve and Final Cut (J/K/L, I/O). "
                           "JefeCheck 1.7 is the keys the 1.7 release shipped with."));
    presetLabel->setBuddy(preset_);
    custom_ = new QLabel(tr("(Custom)"), left);
    custom_->setObjectName(QStringLiteral("prefs.shortcuts.custom"));
    custom_->setToolTip(tr("Some keys differ from the preset."));
    custom_->setStyleSheet(QStringLiteral("color:%1;").arg(jefe::qt::kFXModifiedColor));
    {
        QSizePolicy sp = custom_->sizePolicy();
        sp.setRetainSizeWhenHidden(true);  // the buttons beside it do not jump when it appears
        custom_->setSizePolicy(sp);
    }
    leftRow->addWidget(presetLabel);
    leftRow->addWidget(preset_);
    leftRow->addWidget(custom_);
    auto* right = new QWidget(top);
    auto* rightRow = new QHBoxLayout(right);
    ui::applyRowMetrics(rightRow);
    import_ = new QPushButton(tr("Import…"), right);
    import_->setObjectName(QStringLiteral("prefs.shortcuts.import"));
    import_->setToolTip(tr("Load keys from a .jcshortcuts file: its preset is selected and its keys applied. "
                           "A file with no keys resets every key to that preset."));
    export_ = new QPushButton(tr("Export…"), right);
    export_->setObjectName(QStringLiteral("prefs.shortcuts.export"));
    export_->setToolTip(tr("Save the preset and every current key to a .jcshortcuts file."));
    reset_ = new QPushButton(tr("Reset to preset"), right);
    reset_->setObjectName(QStringLiteral("prefs.shortcuts.reset"));
    reset_->setToolTip(tr("Drop every custom key and use the preset's keys."));
    for (QPushButton* b : {import_, export_, reset_}) {
        ui::useWidgetRect(b);
        rightRow->addWidget(b);
    }
    flow->addWidget(left);
    flow->addWidget(right);
    ui::keepMinHeightForWidth(top);
    outer->addWidget(top);

    // "Switching discards N custom keys. [Switch] [Keep]"
    auto* sw = new QPushButton(tr("Switch"));
    sw->setObjectName(QStringLiteral("prefs.shortcuts.preset.switch"));
    auto* keep = new QPushButton(tr("Keep"));
    keep->setObjectName(QStringLiteral("prefs.shortcuts.preset.keep"));
    presetBar_ = makeBar(this, QStringLiteral("prefs.shortcuts.presetbar"), &presetBarText_, {sw, keep});
    outer->addWidget(presetBar_);

    // The keyboard map, above the list.
    keymapSlot_ = new QWidget(this);
    keymapSlot_->setObjectName(QStringLiteral("prefs.shortcuts.keymapslot"));
    auto* slotLayout = new QVBoxLayout(keymapSlot_);
    slotLayout->setContentsMargins(0, 0, 0, 0);
    slotLayout->setSpacing(ui::kGapTight);
    outer->addWidget(keymapSlot_);

    // Search and list on the left, the clicked key's layers on the right.
    auto* lower = new QHBoxLayout();
    ui::applyRowMetrics(lower);
    auto* listColumn = new QVBoxLayout();
    listColumn->setContentsMargins(0, 0, 0, 0);
    listColumn->setSpacing(ui::kGap);
    lower->addLayout(listColumn, 1);

    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("prefs.shortcuts.search"));
    search_->setPlaceholderText(tr("Search commands or keys"));
    search_->setClearButtonEnabled(true);
    search_->setToolTip(tr("Filter the list by command name or key."));
    search_->installEventFilter(this);  // Escape clears it, Return goes to the list (not Done / Cancel)
    listColumn->addWidget(search_);

    model_ = new CommandListModel(this);
    model_->setHorizontalHeaderLabels({tr("Command"), tr("Key"), tr("Second key"), QString()});
    list_ = new QTreeView(this);
    list_->setObjectName(QStringLiteral("prefs.shortcuts.list"));
    list_->setAccessibleName(tr("Shortcuts"));
    list_->setModel(model_);
    list_->setItemDelegate(new ShortcutCaptureDelegate_Qt(this));
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list_->setSelectionBehavior(QAbstractItemView::SelectRows);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setUniformRowHeights(true);
    list_->setDragEnabled(true);  // a command row drags onto a key of the map
    list_->setDragDropMode(QAbstractItemView::DragOnly);
    list_->setAllColumnsShowFocus(true);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->installEventFilter(this);
    auto* header = list_->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(kColName, QHeaderView::Stretch);
    header->setSectionResizeMode(kColKey0, QHeaderView::Interactive);
    header->setSectionResizeMode(kColKey1, QHeaderView::Interactive);
    header->setSectionResizeMode(kColReset, QHeaderView::Fixed);
    header->resizeSection(kColKey0, 120);
    header->resizeSection(kColKey1, 100);
    header->resizeSection(kColReset, ui::kControlH);
    listColumn->addWidget(list_, 1);

    auto* info = new QWidget(this);
    info->setObjectName(QStringLiteral("prefs.shortcuts.keyinfo"));
    info->setMinimumWidth(200);
    info->setMaximumWidth(260);
    auto* infoColumn = new QVBoxLayout(info);
    infoColumn->setContentsMargins(0, 0, 0, 0);
    infoColumn->setSpacing(ui::kGapTight);
    keyInfoTitle_ = new QLabel(info);
    keyInfoTitle_->setObjectName(QStringLiteral("prefs.shortcuts.keyinfo.title"));
    keyInfoTitle_->setWordWrap(true);
    infoColumn->addWidget(keyInfoTitle_);
    keyInfo_ = new QTreeWidget(info);
    keyInfo_->setObjectName(QStringLiteral("prefs.shortcuts.keyinfo.list"));
    keyInfo_->setAccessibleName(tr("Key on every modifier layer"));
    keyInfo_->setColumnCount(2);
    keyInfo_->setHeaderHidden(true);
    keyInfo_->setRootIsDecorated(false);
    keyInfo_->setUniformRowHeights(true);
    keyInfo_->setSelectionMode(QAbstractItemView::SingleSelection);
    keyInfo_->setToolTip(tr("Click a command to find it in the list."));
    keyInfo_->header()->setStretchLastSection(true);
    keyInfo_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    infoColumn->addWidget(keyInfo_, 1);
    lower->addWidget(info);
    outer->addLayout(lower, 1);

    auto* replace = new QPushButton(tr("Replace"));
    replace->setObjectName(QStringLiteral("prefs.shortcuts.conflict.replace"));
    replace->setToolTip(tr("Move the key here; the other command loses it."));
    auto* cancel = new QPushButton(tr("Cancel"));
    cancel->setObjectName(QStringLiteral("prefs.shortcuts.conflict.cancel"));
    conflictBar_ = makeBar(this, QStringLiteral("prefs.shortcuts.conflict"), &conflictText_, {replace, cancel});
    outer->addWidget(conflictBar_);

    notice_ = new QLabel(this);
    notice_->setObjectName(QStringLiteral("prefs.shortcuts.notice"));
    notice_->setWordWrap(true);
    notice_->hide();
    outer->addWidget(notice_);

    buildRows();
    refresh();
    buildKeymap();

    connect(&reg_, &sc::Registry::changed, this, [this] { refresh(); });
    connect(search_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(preset_, &QComboBox::activated, this, [this](int i) { onPresetActivated(i); });
    connect(sw, &QPushButton::clicked, this, [this] {
        presetBar_->hide();
        dismissConflict();  // the bar's key and "used by" were measured under the old preset
        reg_.setPreset(pendingPreset_);
        refresh();
    });
    connect(keep, &QPushButton::clicked, this, [this] {
        presetBar_->hide();
        refresh();  // the combo goes back to the current preset
    });
    connect(replace, &QPushButton::clicked, this, [this] {
        conflictBar_->hide();
        Pending p = pending_;
        pending_ = {};
        if (p.id.isEmpty()) return;
        // Measured again now: the bar's list may predate a preset switch or reset, and a stale
        // list would leave the key on two commands.
        p.others = reg_.conflicts(idBytes(p.id).constData(), p.key);
        for (const QString& other : p.others) unbindFrom(other, p.key);
        apply(p);
    });
    connect(cancel, &QPushButton::clicked, this, [this] { dismissConflict(); });
    connect(reset_, &QPushButton::clicked, this, [this] {
        presetBar_->hide();
        dismissConflict();
        reg_.resetToPreset();  // keeps a later version's preset name, unlike pinPreset
    });
    connect(import_, &QPushButton::clicked, this, [this] {
        QString path = testPath();
        if (path.isEmpty())
            path = QFileDialog::getOpenFileName(this, tr("Import shortcuts"),
                                                QString::fromStdString(jefe::qt::getDefaultBrowsePath()),
                                                tr("JefeCheck shortcuts (*.jcshortcuts)"));
        if (!path.isEmpty()) importFile(path);
    });
    connect(export_, &QPushButton::clicked, this, [this] {
        QString path = testPath();
        if (path.isEmpty()) {
            const QString dir = QString::fromStdString(jefe::qt::getDefaultBrowsePath());
            path = QFileDialog::getSaveFileName(this, tr("Export shortcuts"),
                                                dir + QStringLiteral("/shortcuts.jcshortcuts"),
                                                tr("JefeCheck shortcuts (*.jcshortcuts)"));
            if (!path.isEmpty() && !path.endsWith(QStringLiteral(".jcshortcuts"), Qt::CaseInsensitive))
                path += QStringLiteral(".jcshortcuts");
        }
        if (!path.isEmpty()) exportFile(path);
    });
    connect(list_, &QTreeView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!idx.isValid() || idx.column() == kColReset) return;
        const QModelIndex name = idx.siblingAtColumn(kColName);
        beginCapture(name.data(kIdRole).toString(), idx.column() == kColKey1 ? 1 : 0);
    });
    connect(list_, &QTreeView::clicked, this, [this](const QModelIndex& idx) {
        if (!idx.isValid() || idx.column() != kColReset) return;
        const QString id = idx.siblingAtColumn(kColName).data(kIdRole).toString();
        if (!id.isEmpty() && reg_.hasOverride(idBytes(id).constData())) resetRow(id);
    });
    connect(list_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& cur) {
                const QModelIndex name = cur.siblingAtColumn(kColName);
                const int kind = name.data(kKindRole).toInt();
                if (kind == CommandRow || kind == HoldRow) emit commandSelected(name.data(kIdRole).toString());
            });
    connect(list_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex idx = list_->indexAt(pos);
        const QString id = idx.siblingAtColumn(kColName).data(kIdRole).toString();
        if (id.isEmpty() || !reg_.hasOverride(idBytes(id).constData())) return;
        QMenu menu(this);
        QAction* a = menu.addAction(tr("Reset to preset key"));
        if (menu.exec(list_->viewport()->mapToGlobal(pos)) == a) resetRow(id);
    });
}

bool ShortcutsPage_Qt::eventFilter(QObject* o, QEvent* e)
{
    if ((o == list_ || o == search_) && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        const bool plain = (ke->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
        const bool isReturn = ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter;
        // Escape would reach the dialog, whose reject() reverts every page: cancel what is open first.
        if (ke->key() == Qt::Key_Escape && plain && handleEscape()) return true;
        if (o == search_ && isReturn && plain) {
            focusFirstMatch();  // never the dialog's default button (Done)
            return true;
        }
        if (o == list_ && isReturn && plain) {
            const QModelIndex cur = list_->currentIndex();
            const QString id = cur.siblingAtColumn(kColName).data(kIdRole).toString();
            if (!id.isEmpty()) beginCapture(id, cur.column() == kColKey1 ? 1 : 0);
            return true;  // a group or read-only row: nothing, rather than Done
        }
    }
    return QWidget::eventFilter(o, e);
}

void ShortcutsPage_Qt::keyPressEvent(QKeyEvent* e)
{
    // Escape from any other widget of the page (a bar's button, the preset menu).
    if (e->key() == Qt::Key_Escape && (e->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier && handleEscape()) {
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

bool ShortcutsPage_Qt::handleEscape()
{
    if (conflictBar_->isVisibleTo(this)) {
        dismissConflict();
        return true;
    }
    if (presetBar_->isVisibleTo(this)) {
        presetBar_->hide();
        refresh();  // the combo goes back to the current preset (Keep)
        return true;
    }
    if (!search_->text().isEmpty()) {
        search_->clear();
        return true;
    }
    return false;  // nothing to cancel: Escape closes Preferences (and reverts)
}

void ShortcutsPage_Qt::dismissConflict()
{
    conflictBar_->hide();
    pending_ = {};
}

void ShortcutsPage_Qt::focusFirstMatch()
{
    for (int r = 0; r < model_->rowCount(); ++r) {
        if (list_->isRowHidden(r, QModelIndex())) continue;
        QStandardItem* g = model_->item(r);
        for (int c = 0; c < g->rowCount(); ++c) {
            if (list_->isRowHidden(c, g->index())) continue;
            const QModelIndex idx = g->child(c)->index();
            list_->setCurrentIndex(idx);
            list_->scrollTo(idx);
            list_->setFocus(Qt::OtherFocusReason);
            return;
        }
    }
}

void ShortcutsPage_Qt::buildRows()
{
    auto addRow = [](QStandardItem* group, const QString& id, const QString& name, int kind) {
        QList<QStandardItem*> cells;
        for (int c = 0; c <= kColReset; ++c) {
            auto* it = new QStandardItem();
            it->setEditable(false);
            it->setData(id, kIdRole);
            it->setData(kind, kKindRole);
            cells << it;
        }
        cells[kColName]->setText(name);
        cells[kColReset]->setTextAlignment(Qt::AlignCenter);
        group->appendRow(cells);
    };
    auto addGroup = [this](const QString& title) {
        auto* g = new QStandardItem(title);
        g->setEditable(false);
        g->setSelectable(false);
        g->setData(int(Group), kKindRole);
        QFont f = g->font();
        f.setBold(true);
        g->setFont(f);
        model_->appendRow(g);
        return g;
    };

    for (sc::Category cat : kCategoryOrder) {
        QStandardItem* g = nullptr;
        for (const sc::Command& c : reg_.commands()) {
            if (c.category != cat) continue;
            if (!g) g = addGroup(categoryName(cat));
            addRow(g, QString::fromUtf8(c.id), c.name, CommandRow);
        }
    }
    if (!reg_.holdKeys().isEmpty()) {
        QStandardItem* g = addGroup(tr("Hold and drag"));
        for (const sc::HoldKey& h : reg_.holdKeys()) addRow(g, QString::fromUtf8(h.id), h.name, HoldRow);
    }
    QStandardItem* g = addGroup(tr("Inside panels"));
    g->setToolTip(tr("These keys work only inside their panel and cannot be changed."));
    for (const PanelKey& p : panelKeys()) {
        addRow(g, QString::fromUtf8(p.id), QString::fromUtf8(p.name), PanelRow);
        const int r = g->rowCount() - 1;
        for (int c = 0; c <= kColReset; ++c) {
            QStandardItem* it = g->child(r, c);
            it->setForeground(kReadOnly);
            it->setToolTip(tr("Works only inside its panel; cannot be changed."));
        }
        QStringList texts;
        for (const QKeySequence& k : p.keys) texts << native(k);
        g->child(r, kColKey0)->setText(texts.value(0));
        g->child(r, kColKey1)->setText(texts.mid(1).join(QStringLiteral(", ")));
    }

    for (int r = 0; r < model_->rowCount(); ++r) list_->setFirstColumnSpanned(r, QModelIndex(), true);
    list_->expandAll();
}

QStandardItem* ShortcutsPage_Qt::rowItem(const QString& id) const
{
    for (int r = 0; r < model_->rowCount(); ++r) {
        QStandardItem* g = model_->item(r);
        for (int c = 0; c < g->rowCount(); ++c)
            if (g->child(c)->data(kIdRole).toString() == id) return g->child(c);
    }
    return nullptr;
}

QString ShortcutsPage_Qt::nameOf(const QString& id) const
{
    for (const sc::Command& c : reg_.commands())
        if (QString::fromUtf8(c.id) == id) return c.name;
    for (const sc::HoldKey& h : reg_.holdKeys())
        if (QString::fromUtf8(h.id) == id) return h.name;
    return id;
}

void ShortcutsPage_Qt::refresh()
{
    // Keys changed: a conflict the bar shows was measured against the old keys.
    dismissConflict();
    for (int r = 0; r < model_->rowCount(); ++r) {
        QStandardItem* g = model_->item(r);
        for (int c = 0; c < g->rowCount(); ++c) {
            QStandardItem* name = g->child(c, kColName);
            const int kind = name->data(kKindRole).toInt();
            if (kind != CommandRow && kind != HoldRow) continue;
            const QByteArray id = idBytes(name->data(kIdRole).toString());
            QList<QKeySequence> keys;
            if (kind == HoldRow) {
                const Qt::Key hk = reg_.holdKey(id.constData());
                if (hk != Qt::Key_unknown) keys << QKeySequence(hk);
            } else {
                keys = reg_.keys(id.constData());
            }
            const bool changed = reg_.hasOverride(id.constData());
            // Unchanged rows clear the role (an empty QBrush would paint black).
            if (changed) name->setForeground(kChanged);
            else name->setData(QVariant(), Qt::ForegroundRole);
            for (int slot = 0; slot < 2; ++slot) {
                QStandardItem* cell = g->child(c, kColKey0 + slot);
                if (kind == HoldRow && slot == 1) {
                    cell->setText(QString());
                    cell->setToolTip(tr("A hold key has one key."));
                    continue;
                }
                const QKeySequence k = keys.value(slot);
                const QStringList clash = k.isEmpty() ? QStringList() : reg_.conflicts(id.constData(), k);
                QString text = native(k);
                QString tip = tr("Double-click or press Enter to change. Esc cancels; Backspace unbinds.");
                if (!clash.isEmpty()) {
                    QStringList names;
                    for (const QString& o : clash) names << QStringLiteral("“%1”").arg(nameOf(o));
                    text = QStringLiteral("⚠ ") + text;
                    tip = tr("Also used by %1.").arg(names.join(QStringLiteral(", ")));
                }
                cell->setText(text);
                if (clash.isEmpty()) cell->setData(QVariant(), Qt::ForegroundRole);
                else cell->setForeground(kConflict);
                cell->setToolTip(tip);
            }
            QStandardItem* dot = g->child(c, kColReset);
            dot->setText(changed ? kResetModified : QString());
            dot->setForeground(QBrush(kChanged));
            if (changed) {
                QStringList preset;
                if (kind == HoldRow) {
                    const Qt::Key pk = reg_.presetHoldKey(id.constData());
                    if (pk != Qt::Key_unknown) preset << native(QKeySequence(pk));
                } else {
                    for (const QKeySequence& k : reg_.presetKeys(id.constData())) preset << native(k);
                }
                dot->setToolTip(preset.isEmpty() ? tr("Reset to the preset (unbound)")
                                                 : tr("Reset to the preset key (%1)").arg(preset.join(QStringLiteral(", "))));
            } else {
                dot->setToolTip(QString());
            }
        }
    }
    custom_->setVisible(reg_.isCustom());
    reset_->setEnabled(reg_.isCustom());
    if (!presetBar_->isVisibleTo(this)) {
        const int i = preset_->findData(reg_.preset());
        if (i >= 0) preset_->setCurrentIndex(i);
    }
    applyFilter();
}

void ShortcutsPage_Qt::applyFilter()
{
    const QString t = search_->text().simplified();
    for (int r = 0; r < model_->rowCount(); ++r) {
        QStandardItem* g = model_->item(r);
        bool any = false;
        for (int c = 0; c < g->rowCount(); ++c) {
            bool match = t.isEmpty() || g->child(c, kColName)->text().contains(t, Qt::CaseInsensitive);
            if (!match) {
                // Key text as shown and as typed ("Ctrl+L" finds ⌘L too).
                for (int col : {kColKey0, kColKey1}) {
                    QString keyText = g->child(c, col)->text();
                    keyText.remove(QStringLiteral("⚠ "));
                    if (keyText.isEmpty()) continue;
                    const QString portable =
                        QKeySequence::fromString(keyText, QKeySequence::NativeText).toString(QKeySequence::PortableText);
                    if (keyText.contains(t, Qt::CaseInsensitive) || portable.contains(t, Qt::CaseInsensitive)) match = true;
                }
            }
            list_->setRowHidden(c, g->index(), !match);
            any = any || match;
        }
        list_->setRowHidden(r, QModelIndex(), !any);
    }
}

void ShortcutsPage_Qt::buildKeymap()
{
    auto* slotLayout = static_cast<QVBoxLayout*>(keymapSlot_->layout());

    // "Modifiers [None] [⇧] [⌃] [⌥] [⌘]" and the category legend; wraps when narrow.
    auto* bar = new QWidget(keymapSlot_);
    bar->setObjectName(QStringLiteral("prefs.shortcuts.keymap.bar"));
    auto* flow = new FlowLayout_Qt(bar, 0, ui::kGap, ui::kGapTight);
    auto* label = new QLabel(tr("Modifiers"), bar);
    flow->addWidget(label);
    auto* none = new QPushButton(tr("None"), bar);
    none->setObjectName(QStringLiteral("prefs.shortcuts.keymap.layer.none"));
    none->setCheckable(true);
    none->setChecked(true);
    none->setToolTip(tr("Show the keys pressed with no modifier."));
    ui::useWidgetRect(none);
    flow->addWidget(none);
    struct Toggle { Qt::KeyboardModifier mod; QPushButton* button; };
    auto toggles = std::make_shared<QList<Toggle>>();
    for (Qt::KeyboardModifier m : sc::layerModifiers()) {
        auto* b = new QPushButton(sc::modifierGlyph(m), bar);
        QString leaf;
        switch (m) {
        case Qt::ShiftModifier: leaf = QStringLiteral("shift"); break;
        case Qt::ControlModifier: leaf = QStringLiteral("ctrl"); break;
        case Qt::AltModifier: leaf = QStringLiteral("alt"); break;
        default: leaf = QStringLiteral("meta"); break;
        }
        b->setObjectName(QStringLiteral("prefs.shortcuts.keymap.layer.") + leaf);
        b->setCheckable(true);
        b->setToolTip(tr("Show the keys pressed with %1 (combine toggles for more than one). "
                         "Holding the key while this page has focus does the same.")
                          .arg(modifierName(m)));
        ui::useWidgetRect(b);
        flow->addWidget(b);
        toggles->push_back({m, b});
    }
    for (sc::Category c : sc::allCategories()) {
        auto* chip = new QLabel(bar);
        chip->setTextFormat(Qt::RichText);
        chip->setText(QStringLiteral("<span style=\"color:%1\">&#9632;</span> %2")
                          .arg(sc::categoryColor(c).name(), sc::categoryDisplayName(c).toHtmlEscaped()));
        flow->addWidget(chip);
    }
    ui::keepMinHeightForWidth(bar);
    slotLayout->addWidget(bar);

    keymap_ = new ShortcutKeyboard_Qt(keymapSlot_);
    keymap_->setModifierScope(this);  // held modifiers count while the list or the search box has focus
    slotLayout->addWidget(keymap_);

    auto sync = [none, toggles](Qt::KeyboardModifiers shown) {
        none->setChecked(shown == Qt::NoModifier);
        for (const Toggle& t : *toggles) t.button->setChecked(shown & t.mod);
    };
    connect(keymap_, &ShortcutKeyboard_Qt::layerChanged, this, sync);
    connect(none, &QPushButton::clicked, this, [this, sync] {
        keymap_->setLayer(Qt::NoModifier);
        sync(keymap_->layer());
    });
    for (const Toggle& t : *toggles)
        connect(t.button, &QPushButton::clicked, this, [this, sync, m = t.mod] {
            keymap_->setLayer(keymap_->toggledLayer() ^ m);
            sync(keymap_->layer());
        });
    connect(keymap_, &ShortcutKeyboard_Qt::keyClicked, this, [this](Qt::Key k) { showKeyInfo(k); });
    connect(keymap_, &ShortcutKeyboard_Qt::commandDropped, this,
            [this](const QString& id, Qt::Key k, Qt::KeyboardModifiers layer) { assignKey(id, k, layer); });
    connect(&reg_, &sc::Registry::changed, this, [this] {
        if (keymap_->selectedKey() != Qt::Key_unknown) showKeyInfo(keymap_->selectedKey());
    });
    connect(keyInfo_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
        const QString id = item ? item->data(0, Qt::UserRole).toString() : QString();
        if (!id.isEmpty()) selectCommand(id);
    });
    showKeyInfo(Qt::Key_unknown);
}

void ShortcutsPage_Qt::showKeyInfo(Qt::Key key)
{
    keyInfo_->clear();
    if (key == Qt::Key_unknown) {
        keyInfoTitle_->setText(tr("Click a key on the map to see what it does with each modifier."));
        return;
    }
    QString name;
    for (const sc::KeyCap& c : sc::ansiLayout())
        if (c.key == key) { name = c.label; break; }
    keyInfoTitle_->setText(tr("Key: %1").arg(name));
    const QList<ShortcutKeyboard_Qt::Binding> bindings = keymap_->bindingsFor(key);
    for (const ShortcutKeyboard_Qt::Binding& b : bindings) {
        auto* item = new QTreeWidgetItem(keyInfo_);
        item->setText(0, native(b.key) + (b.hold ? tr(" hold") : QString()));
        item->setText(1, b.name);
        item->setToolTip(1, b.name);
        item->setData(0, Qt::UserRole, b.id);
    }
    if (bindings.isEmpty()) {
        auto* item = new QTreeWidgetItem(keyInfo_);
        item->setText(1, tr("Nothing on this key"));
        item->setFlags(Qt::NoItemFlags);
    }
}

void ShortcutsPage_Qt::selectCommand(const QString& id)
{
    QStandardItem* it = rowItem(id);
    if (!it) return;
    if (list_->isRowHidden(it->row(), it->parent()->index())) search_->clear();
    const QModelIndex idx = it->index();
    list_->setCurrentIndex(idx);
    list_->scrollTo(idx);
}

bool ShortcutsPage_Qt::beginCapture(const QString& id, int slot)
{
    QStandardItem* it = rowItem(id);
    if (!it || slot < 0 || slot > 1) return false;
    const int kind = it->data(kKindRole).toInt();
    if (kind != CommandRow && kind != HoldRow) return false;
    if (kind == HoldRow) slot = 0;
    dismissConflict();
    finishCapture();
    selectCommand(id);
    const QModelIndex idx = it->index().siblingAtColumn(kColKey0 + slot);
    list_->setCurrentIndex(idx);
    captureIndex_ = idx;
    list_->openPersistentEditor(idx);
    if (captureEditor_) captureEditor_->setFocus(Qt::OtherFocusReason);
    return captureEditor_ != nullptr;
}

void ShortcutsPage_Qt::finishCapture()
{
    if (captureEditor_) QObject::disconnect(captureEditor_, nullptr, this, nullptr);
    const QPersistentModelIndex idx = captureIndex_;
    captureIndex_ = QPersistentModelIndex();
    const bool hadFocus = captureEditor_ && captureEditor_->hasFocus();
    captureEditor_ = nullptr;
    if (idx.isValid()) list_->closePersistentEditor(idx);
    if (hadFocus) list_->setFocus(Qt::OtherFocusReason);
}

void ShortcutsPage_Qt::onCaptured(const QKeySequence& key)
{
    const QModelIndex idx = captureIndex_;
    finishCapture();
    if (!idx.isValid()) return;
    Pending p;
    p.id = idx.siblingAtColumn(kColName).data(kIdRole).toString();
    p.hold = idx.siblingAtColumn(kColName).data(kKindRole).toInt() == HoldRow;
    p.slot = idx.column() - kColKey0;
    p.key = key;
    requestBind(p);
}

void ShortcutsPage_Qt::requestBind(const Pending& in)
{
    Pending p = in;
    const QKeySequence key = p.key;
    const QByteArray id = idBytes(p.id);
    if (sc::isSystemReserved(key)) {  // the system takes it first: the command would never run
        showNotice(tr("%1 is reserved by macOS.").arg(native(key)), true);
        return;
    }
    if (p.hold) {
        if (key[0].keyboardModifiers() != Qt::NoModifier) {
            showNotice(tr("A hold key is one key without modifiers; %1 was not used.").arg(native(key)), true);
            return;
        }
        if (reg_.holdKey(id.constData()) == key[0].key()) return;
    } else {
        const QList<QKeySequence> cur = reg_.keys(id.constData());
        if (cur.contains(key)) return;  // already one of this command's keys
    }
    notice_->hide();
    p.others = reg_.conflicts(id.constData(), key);
    if (p.others.isEmpty()) {
        apply(p);
        return;
    }
    QStringList names;
    for (const QString& o : p.others) names << QStringLiteral("“%1”").arg(nameOf(o));
    conflictText_->setText(tr("⚠ %1 is used by %2.").arg(native(key), names.join(QStringLiteral(", "))));
    pending_ = p;
    conflictBar_->show();
    emit conflictRaised(p.id, key);
}


bool ShortcutsPage_Qt::assignKey(const QString& id, Qt::Key key, Qt::KeyboardModifiers mods)
{
    QStandardItem* it = rowItem(id);
    if (!it) return false;
    const int kind = it->data(kKindRole).toInt();
    if (kind != CommandRow && kind != HoldRow) return false;
    dismissConflict();  // a new drop replaces a pending conflict
    finishCapture();
    if (kind == HoldRow && (mods & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        // A hold key is one bare key: the layer's modifiers cannot be part of it.
        showNotice(tr("A hold key is one key without modifiers; switch the map to no modifiers to assign it."), true);
        return true;
    }
    mods &= Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    const QKeySequence seq = ShortcutCaptureEdit_Qt::keyFor(key, mods);
    if (seq.isEmpty()) {
        showNotice(tr("That key cannot be used."), true);
        return true;
    }
    Pending p;
    p.id = id;
    p.hold = kind == HoldRow;
    p.slot = 0;  // the first key: replaced, or filled when the command has none
    p.key = seq;
    selectCommand(id);
    requestBind(p);
    return true;
}

void ShortcutsPage_Qt::onUnbound()
{
    const QModelIndex idx = captureIndex_;
    finishCapture();
    if (!idx.isValid()) return;
    const QString id = idx.siblingAtColumn(kColName).data(kIdRole).toString();
    const QByteArray b = idBytes(id);
    if (idx.siblingAtColumn(kColName).data(kKindRole).toInt() == HoldRow) {
        reg_.setHoldOverride(b.constData(), Qt::Key_unknown);
        return;
    }
    QList<QKeySequence> keys = reg_.keys(b.constData());
    const int slot = idx.column() - kColKey0;
    if (slot < keys.size()) keys.removeAt(slot);
    reg_.setOverrideKeys(b.constData(), keys);
}

void ShortcutsPage_Qt::apply(const Pending& p)
{
    if (p.id.isEmpty()) return;
    const QByteArray id = idBytes(p.id);
    if (p.hold) {
        reg_.setHoldOverride(id.constData(), p.key[0].key());
        return;
    }
    QList<QKeySequence> keys = reg_.keys(id.constData());
    if (p.slot < keys.size()) keys[p.slot] = p.key;
    else keys.append(p.key);
    reg_.setOverrideKeys(id.constData(), keys);
}

void ShortcutsPage_Qt::unbindFrom(const QString& id, const QKeySequence& key)
{
    const QByteArray b = idBytes(id);
    for (const sc::HoldKey& h : reg_.holdKeys())
        if (QString::fromUtf8(h.id) == id) {
            reg_.setHoldOverride(b.constData(), Qt::Key_unknown);
            return;
        }
    QList<QKeySequence> keys = reg_.keys(b.constData());
    keys.removeAll(key);
    reg_.setOverrideKeys(b.constData(), keys);
}

void ShortcutsPage_Qt::resetRow(const QString& id)
{
    dismissConflict();
    const QByteArray b = idBytes(id);
    QStandardItem* it = rowItem(id);
    if (it && it->data(kKindRole).toInt() == HoldRow) reg_.clearHoldOverride(b.constData());
    else reg_.clearOverride(b.constData());
}

void ShortcutsPage_Qt::onPresetActivated(int index)
{
    const QString p = preset_->itemData(index).toString();
    if (p == reg_.preset()) {
        presetBar_->hide();
        return;
    }
    if (!reg_.isCustom()) {
        presetBar_->hide();
        reg_.setPreset(p);
        return;
    }
    pendingPreset_ = p;
    const int n = reg_.overrideCount();
    presetBarText_->setText(n == 1 ? tr("Switching discards 1 custom key.")
                                   : tr("Switching discards %1 custom keys.").arg(n));
    presetBar_->show();
}

void ShortcutsPage_Qt::showNotice(const QString& text, bool error)
{
    notice_->setText(text);
    notice_->setStyleSheet(error ? QStringLiteral("color:%1;").arg(kConflict.name()) : QString());
    notice_->setVisible(!text.isEmpty());
}

bool ShortcutsPage_Qt::importFile(const QString& path)
{
    QString err;
    presetBar_->hide();
    dismissConflict();
    const bool ok = reg_.importFrom(path, &err);
    const QString file = QFileInfo(path).fileName();
    if (!ok) showNotice(tr("Could not import %1: %2").arg(file, err), true);
    else showNotice(err.isEmpty() ? tr("Imported %1.").arg(file) : tr("Imported %1. %2").arg(file, err), false);
    return ok;
}

bool ShortcutsPage_Qt::exportFile(const QString& path)
{
    const bool ok = reg_.exportTo(path);
    const QString file = QFileInfo(path).fileName();
    showNotice(ok ? tr("Exported %1.").arg(file) : tr("Could not write %1.").arg(file), !ok);
    return ok;
}

QString ShortcutsPage_Qt::testPath() const { return qEnvironmentVariable("JEFECHECK_SHORTCUTS_FILE"); }

QStringList ShortcutsPage_Qt::visibleIds() const
{
    QStringList out;
    for (int r = 0; r < model_->rowCount(); ++r) {
        if (list_->isRowHidden(r, QModelIndex())) continue;
        QStandardItem* g = model_->item(r);
        for (int c = 0; c < g->rowCount(); ++c)
            if (!list_->isRowHidden(c, g->index())) out << g->child(c)->data(kIdRole).toString();
    }
    return out;
}

QStringList ShortcutsPage_Qt::panelRowIds() const
{
    QStringList out;
    for (const PanelKey& p : panelKeys()) out << QString::fromUtf8(p.id);
    return out;
}

QString ShortcutsPage_Qt::keyText(const QString& id, int slot) const
{
    QStandardItem* it = rowItem(id);
    if (!it || slot < 0 || slot > 1) return {};
    return it->parent()->child(it->row(), kColKey0 + slot)->text();
}

bool ShortcutsPage_Qt::rowShowsChanged(const QString& id) const
{
    QStandardItem* it = rowItem(id);
    if (!it) return false;
    QStandardItem* dot = it->parent()->child(it->row(), kColReset);
    return it->foreground().color() == kChanged && dot->text() == kResetModified &&
           dot->foreground().color() == kChanged;
}

bool ShortcutsPage_Qt::resetRowByClick(const QString& id)
{
    QStandardItem* it = rowItem(id);
    if (!it) return false;
    selectCommand(id);
    const QModelIndex dot = it->index().siblingAtColumn(kColReset);
    list_->scrollTo(dot);
    const QRect r = list_->visualRect(dot);
    if (!r.isValid()) return false;
    const QPointF pos = r.center();
    QWidget* vp = list_->viewport();
    const QPointF global = vp->mapToGlobal(pos);
    QMouseEvent press(QEvent::MouseButtonPress, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(vp, &press);
    QApplication::sendEvent(vp, &release);
    return true;
}
