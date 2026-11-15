// SPDX-License-Identifier: GPL-2.0-or-later
#include "FXParamPanel_qt.h"
#include "FXParamEditors_qt.h"
#include "FlowLayout_qt.h"
#include "UiMetrics_qt.h"
#include "qticons.h"
#include "SequenceLoadBridge_qt.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDropEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QMenu>
#include <QMetaObject>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace ui = jefe::qt::ui;

namespace {

// Stack index of an FX, stashed on its list item so the reorder math can
// reconstruct the pre-drop order after QListWidget shuffles items.
constexpr int kFxIndexRole = Qt::UserRole;

}  // namespace

// FXReorderList_Qt

void FXReorderList_Qt::dropEvent(QDropEvent* e) {
    // Capture the stack indices in their pre-drop order: each item's build-time position
    // is in kFxIndexRole, so the moved element's old position equals its stored value.
    std::vector<int> before;
    before.reserve(count());
    for (int i = 0; i < count(); ++i) {
        before.push_back(item(i)->data(kFxIndexRole).toInt());
    }

    QListWidget::dropEvent(e);

    std::vector<int> after;
    after.reserve(count());
    for (int i = 0; i < count(); ++i) {
        after.push_back(item(i)->data(kFxIndexRole).toInt());
    }

    if (before.size() != after.size() || before == after) return;

    const int n = static_cast<int>(after.size());
    int lo = 0;
    while (lo < n && before[lo] == after[lo]) ++lo;
    int hi = n - 1;
    while (hi >= 0 && before[hi] == after[hi]) --hi;
    if (lo > hi) return;

    int from, to;
    if (after[lo] == before[hi]) {
        // Up-move: the element that lived at `hi` is now at `lo`.
        from = hi;
        to = lo;
    } else {
        // Down-move: the element that lived at `lo` is now at `hi`.
        from = lo;
        to = hi;
    }
    if (from != to) emit itemsReordered(from, to);
}

// FXParamPanel_Qt

FXParamPanel_Qt::FXParamPanel_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("fxparams.panel");

    // "+ Add FX": InstantPopup button with a hierarchical menu built from each FX's
    // menuName, rebuilt on aboutToShow so a late autoload is reflected.
    addButton_ = new QToolButton(this);
    addButton_->setObjectName("fxparams.addfx.button");
    addButton_->setAccessibleName("Add FX");
    addButton_->setIcon(jefe::qticons::add());
    addButton_->setIconSize(QSize(14, 14));
    addButton_->setText("Add FX");
    addButton_->setToolTip("Add an effect to the active plate");
    addButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addButton_->setPopupMode(QToolButton::InstantPopup);
    // Hide the built-in menu-indicator caret — the + icon is the affordance.
    addButton_->setStyleSheet("QToolButton::menu-indicator { image: none; }");

    addMenu_ = new QMenu(this);
    addMenu_->setObjectName("fxparams.addfx.menu");
    addButton_->setMenu(addMenu_);
    connect(addMenu_, &QMenu::aboutToShow, this, [this]() { rebuildAddMenu(); });

    // No accessibleName on the status label: Mac AX would report it as AXTitle instead
    // of letting setText drive AXValue.
    status_ = new QLabel(this);
    status_->setStyleSheet("color: #888; font-style: italic;");
    status_->setObjectName("fxparams.status.label");
    status_->setText("FX: initializing");

    list_ = new FXReorderList_Qt(this);
    list_->setObjectName("fxparams.list");
    list_->setAccessibleName("FX stack");
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    list_->setDefaultDropAction(Qt::MoveAction);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setUniformItemSizes(false);
    list_->setSpacing(2);
    // Cards always fit the dock width; never scroll horizontally (that would
    // carry the power/remove buttons off-view when the dock is narrow).
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->viewport()->installEventFilter(this);
    connect(list_, &FXReorderList_Qt::itemsReordered, this,
            [this](int from, int to) {
                if (refreshing_) return;
                const int active = jefe::qt::getActivePlate();
                if (active < 0) return;
                jefe::qt::moveFXOnPlate(active, from, to);
                emit viewportRepaintRequested();
                // We're inside the drop's call stack: rebuild on the next turn.
                scheduleRefresh();
            });

    // A rebuild that arrived mid-drag waits for the drag to end.
    retryTimer_ = new QTimer(this);
    retryTimer_->setSingleShot(true);
    retryTimer_->setInterval(50);
    connect(retryTimer_, &QTimer::timeout, this, &FXParamPanel_Qt::refresh);

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addWidget(addButton_);
    topRow->addStretch(1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(ui::kGapTight, ui::kGapTight, ui::kGapTight, ui::kGapTight);
    outer->setSpacing(ui::kGapTight);
    outer->addLayout(topRow);
    outer->addWidget(status_);
    outer->addWidget(list_, /*stretch*/ 1);

    refresh();
}

void FXParamPanel_Qt::rebuildAddMenu() {
    addMenu_->clear();
    const auto entries = jefe::qt::getAvailableFXMenu();
    if (entries.empty()) {
        QAction* a = addMenu_->addAction(tr("(no FX loaded)"));
        a->setEnabled(false);
        return;
    }

    for (const auto& [fxIndex, menuName] : entries) {
        // menuName is "Category/Subcategory/Name"; walk/create submenus
        // and hang the leaf action (carrying fxIndex) under the last one.
        const QString full = QString::fromStdString(menuName);
        const QStringList parts = full.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;

        QMenu* parentMenu = addMenu_;
        for (int i = 0; i < parts.size() - 1; ++i) {
            const QString seg = parts[i].trimmed();
            QMenu* sub = nullptr;
            for (QAction* a : parentMenu->actions()) {
                if (a->menu() && a->menu()->title() == seg) {
                    sub = a->menu();
                    break;
                }
            }
            if (!sub) sub = parentMenu->addMenu(seg);
            parentMenu = sub;
        }

        const QString leaf = parts.last().trimmed();
        QAction* act = parentMenu->addAction(leaf);
        act->setData(fxIndex);
        connect(act, &QAction::triggered, this, [this, fxIndex]() {
            // Defensive: ensure a plate is active before the bridge add
            // (addFXToActivePlate targets getActivePlate()).
            jefe::qt::setActivePlate(jefe::qt::getActivePlate());
            jefe::qt::addFXToActivePlate(fxIndex);
            emit viewportRepaintRequested();
            refresh();
        });
    }
}

void FXParamPanel_Qt::scheduleRefresh() {
    if (refreshQueued_) return;
    refreshQueued_ = true;
    QMetaObject::invokeMethod(this, [this]() {
        refreshQueued_ = false;
        refresh();
    }, Qt::QueuedConnection);
}

bool FXParamPanel_Qt::anyEditorInteracting() const {
    for (const CachedRow& r : rowCache_)
        if (r.floatEditor && r.floatEditor->isInteracting()) return true;
    return false;
}

void FXParamPanel_Qt::refresh() {
    const int active = jefe::qt::getActivePlate();
    const std::vector<jefe::qt::FXMeta> stack =
        active >= 0 ? jefe::qt::getFXStackMetaOnPlate(active) : std::vector<jefe::qt::FXMeta>();

    std::vector<std::pair<std::uint32_t, std::string>> fingerprint;
    fingerprint.reserve(stack.size());
    for (const auto& fx : stack) fingerprint.emplace_back(fx.instanceId, fx.hash);

    // Fast path: same plate, same effects. plateStateChanged fires per viewport
    // mouse-move and during playback, so this must not rebuild anything.
    if (active >= 0 && active == lastActivePlate_ && fingerprint == lastFingerprint_ && !stack.empty()) {
        refreshing_ = true;
        const bool ok = refreshValuesOnly(stack);
        refreshing_ = false;
        if (ok) return;
    }

    // Never tear the cards down under a drag or an open edit: try again once it ends.
    if (anyEditorInteracting()) {
        retryTimer_->start();
        return;
    }
    rebuild(active, stack);
    lastActivePlate_ = active;
    lastFingerprint_ = std::move(fingerprint);
}

namespace {

bool isLayoutOnly(jefe::qt::FXParamType t) {
    return t == jefe::qt::FXParamType::Spacer || t == jefe::qt::FXParamType::Newline;
}

// The file's label colour, or empty for white (white means "theme colour").
QString fileColourOf(const jefe::qt::FXParamMeta& p) {
    if (fxui::isWhite(p.labelR, p.labelG, p.labelB)) return QString();
    return QColor(p.labelR, p.labelG, p.labelB).name();
}

// Menu name's last segment ("Color/Brightness, …" -> "Brightness, …").
QString displayName(const jefe::qt::FXMeta& fx) {
    const QString full = QString::fromStdString(!fx.menuName.empty() ? fx.menuName : fx.name);
    const QStringList parts = full.split('/', Qt::SkipEmptyParts);
    return parts.isEmpty() ? full : parts.last().trimmed();
}

// Header name: elided to whatever width the header leaves it.
class ElidedLabel : public QLabel {
public:
    ElidedLabel(const QString& text, QWidget* parent) : QLabel(text, parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setProperty(ui::kDensityElidedProperty, true);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const QRect r = contentsRect();
        style()->drawItemText(&p, r, Qt::AlignLeft | Qt::AlignVCenter, palette(), isEnabled(),
                              fontMetrics().elidedText(text(), Qt::ElideRight, r.width()), foregroundRole());
    }
};

// The widest a combo grows on a column row: the slider track's cap, so a bottom dock
// does not stretch a LUT picker across the screen.
constexpr int kColumnComboMaxW = 320;

// One non-float parameter: label, then the control, 24 px tall. In a flow row (several
// controls sharing a line) the row is packed to its content. A column row (the line's
// only control) spans the card: the label sits in the card's right-aligned label column,
// sized in relayoutCards(), so the control starts where the slider rows' value fields
// start; a combo stretches up to the slider cap.
QWidget* makeLabelledRow(const QString& objectBase, const QString& text, const QString& tooltip,
                         QWidget* control, QLabel** labelOut, bool column = false) {
    auto* row = new QWidget();
    row->setObjectName(objectBase + ".row");
    row->setToolTip(tooltip);
    row->setFixedHeight(ui::kControlH);
    row->setSizePolicy(column ? QSizePolicy::Expanding : QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* lay = new QHBoxLayout(row);
    ui::applyRowMetrics(lay, ui::kGapTight);
    auto* label = new QLabel(text, row);
    label->setObjectName(objectBase + ".label");
    lay->addWidget(label);
    control->setParent(row);
    control->setToolTip(tooltip);
    control->setAccessibleName(text);
    if (column) {
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        const bool combo = qobject_cast<QComboBox*>(control) != nullptr;
        if (combo) control->setMaximumWidth(kColumnComboMaxW);
        lay->addWidget(control, combo ? 1 : 0);
        lay->addStretch(0);  // takes the width past the combo's cap, or right of a checkbox
    } else {
        lay->addWidget(control);
    }
    *labelOut = label;
    return row;
}

QComboBox* makeCombo() {
    auto* combo = new QComboBox();
    // Size to a short minimum, not the longest option, so a picker fits a narrow dock.
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(8);
    combo->setProperty(ui::kDensityElidedProperty, true);
    return combo;
}

// Picks the combo item whose data equals `want` (LUT pickers store the global LUT index).
void selectByData(QComboBox* combo, int want) {
    for (int i = 0; i < combo->count(); ++i)
        if (combo->itemData(i).toInt() == want) { combo->setCurrentIndex(i); return; }
}

}  // namespace

void FXParamPanel_Qt::rebuild(int active, const std::vector<jefe::qt::FXMeta>& stack) {
    refreshing_ = true;

    // The list releases item widgets with deleteLater, so this is safe even when
    // the request came from inside a card.
    list_->clear();
    rowCache_.clear();
    sliderLabels_.clear();
    columnLabels_.clear();
    powerButtons_.clear();
    lastCardWidth_ = -1;

    if (active < 0) {
        status_->setText("No active plate.");
        refreshing_ = false;
        return;
    }
    if (stack.empty()) {
        status_->setText(QString("Plate %1: no FX. Use \"+ Add FX\".").arg(active));
        refreshing_ = false;
        return;
    }

    using jefe::qt::FXParamType;
    int paramCount = 0, editableCount = 0;
    for (size_t i = 0; i < stack.size(); ++i) {
        const auto& fx = stack[i];
        const int fxIdx = static_cast<int>(i);
        const QString fxBase = QString("fxparams.fx%1").arg(fxIdx);
        // Writes find this FX by instance id when they happen: the stack can change under
        // a drag (a peer or the viewport inserts an FX before it) while the rebuild waits.
        const auto fxNow = [active, fxIdx, fxId = fx.instanceId]() {
            return fxId != 0 ? jefe::qt::fxIndexOfInstanceOnPlate(active, fxId) : fxIdx;
        };

        auto* card = new QFrame();
        card->setObjectName(fxBase + ".card");
        card->setProperty("fxCard", true);
        card->setStyleSheet(
            "QFrame[fxCard=\"true\"] { background: #2b2b2b; border: 1px solid #3a3a3a;"
            " border-radius: 4px; }"
            " *[fxClear=\"true\"] { background: transparent; }");
        auto* cardLay = new QVBoxLayout(card);
        cardLay->setContentsMargins(ui::kGapTight, ui::kGapTight, ui::kGapTight, ui::kGapTight);
        cardLay->setSpacing(ui::kGapTight);

        // --- Header: handle, power, name, remove ------------------------------------
        auto* header = new QWidget(card);
        header->setObjectName(fxBase + ".header");
        header->setFixedHeight(ui::kControlH);
        auto* headerLay = new QHBoxLayout(header);
        ui::applyRowMetrics(headerLay, ui::kGapTight);

        // The handle and the empty card area ignore presses, so they reach the list
        // and start a card drag; the editors accept theirs.
        auto* handle = new QLabel(header);
        handle->setObjectName(fxBase + ".handle");
        handle->setPixmap(jefe::qticons::dragHandle().pixmap(QSize(ui::kIconPx, ui::kIconPx)));
        handle->setFixedSize(ui::kIconPx, ui::kControlH);
        handle->setToolTip("Drag to reorder");
        handle->setCursor(Qt::OpenHandCursor);
        headerLay->addWidget(handle);

        QToolButton* power = makePowerToggle(header);
        power->setObjectName(fxBase + ".active.check");
        power->setAccessibleName(QString("FX %1 active").arg(fxIdx));
        power->setToolTip("Enable / disable this effect");
        power->setChecked(fx.active);
        powerButtons_.push_back(power);
        connect(power, &QToolButton::toggled, this, [this, active, fxNow](bool on) {
            const int at = fxNow();
            if (refreshing_ || at < 0) return;
            jefe::qt::setFXActiveOnPlate(active, at, on);
            emit viewportRepaintRequested();
        });
        headerLay->addWidget(power);

        const QString fullName = QString::fromStdString(!fx.menuName.empty() ? fx.menuName : fx.name);
        auto* nameLab = new ElidedLabel(displayName(fx), header);
        nameLab->setObjectName(fxBase + ".name");
        nameLab->setToolTip(fullName);
        QFont bold = nameLab->font();
        bold.setBold(true);
        nameLab->setFont(bold);
        headerLay->addWidget(nameLab, /*stretch*/ 1);

        auto* removeBtn = new QToolButton(header);
        removeBtn->setObjectName(fxBase + ".remove.button");
        removeBtn->setAccessibleName(QString("Remove FX %1").arg(fxIdx));
        removeBtn->setIcon(jefe::qticons::remove());
        removeBtn->setToolTip("Remove this effect");
        ui::makeIconButton(removeBtn);
        removeBtn->setStyleSheet("QToolButton { padding: 0; }");
        connect(removeBtn, &QToolButton::clicked, this, [this, active, fxNow]() {
            const int at = fxNow();
            if (refreshing_ || at < 0) return;
            jefe::qt::removeFXFromPlate(active, at);
            emit viewportRepaintRequested();
            // The button lives inside the card the rebuild deletes.
            scheduleRefresh();
        });
        headerLay->addWidget(removeBtn);
        cardLay->addWidget(header);

        // --- Body: rows from the .jfx layout -----------------------------------------
        std::vector<fxui::Item> items;
        items.reserve(fx.params.size());
        std::vector<std::string> groups;
        for (const auto& p : fx.params) {
            items.push_back({jefe::qt::fxKindOf(p.type), p.style, p.group});
            if (std::find(groups.begin(), groups.end(), p.group) == groups.end()) groups.push_back(p.group);
        }
        const std::vector<fxui::Row> rows = fxui::layoutRows(items, /*showGroupHeadings=*/groups.size() > 1);

        // Cache entries by param index, flattened in declaration order afterwards.
        std::vector<CachedRow> cacheByParam(fx.params.size());
        std::vector<bool> cached(fx.params.size(), false);
        std::vector<SliderLabel> sliders;
        std::vector<ColumnLabel> columnLabels;
        // Set while building a line's only labelled control: its label joins the column.
        bool columnRow = false;

        // Builds the editor for param pi; returns the widget for the row.
        auto buildParam = [&](int pi) -> QWidget* {
            const auto& p = fx.params[pi];
            const QString base = QString("%1.param.%2").arg(fxBase, QString::fromStdString(p.name));
            const QString text = QString::fromStdString(!p.label.empty() ? p.label : p.name);
            const QString tooltip = QString::fromStdString(p.tooltip);
            const std::string group = p.group, name = p.name;
            CachedRow c;
            c.group = p.group;
            c.name = p.name;
            c.paramType = static_cast<int>(p.type);
            c.value = p.value;
            c.defaultValue = p.defaultValue;
            c.fileColour = fileColourOf(p);

            QWidget* rowWidget = nullptr;
            switch (p.type) {
                case FXParamType::Spacer: {
                    auto* gap = new QWidget();
                    gap->setFixedSize(ui::kControlH, ui::kControlH);
                    return gap;
                }
                case FXParamType::Float: {
                    FXEditorSpec spec;
                    spec.label = text;
                    spec.tooltip = tooltip;
                    spec.hardMin = p.minimum;
                    spec.hardMax = p.maximum;
                    spec.softMin = p.softMinimum;
                    spec.softMax = p.softMaximum;
                    spec.step = p.step > 0.f ? p.step : 0.001f;
                    spec.defaultValue = p.defaultValue;
                    if (!c.fileColour.isEmpty()) spec.labelColor = QColor(c.fileColour);
                    spec.objectBase = base;
                    FXParamEditor_Qt* editor = makeFloatEditor(p.style, spec, nullptr);
                    editor->setObjectName(base + ".row");
                    editor->setValue(p.value);
                    connect(editor, &FXParamEditor_Qt::valueEdited, this,
                            [this, active, fxNow, group, name](float v) {
                                const int at = fxNow();
                                if (refreshing_ || at < 0) return;
                                jefe::qt::setFXParamValueOnPlate(active, at, group, name, v);
                                emit viewportRepaintRequested();
                            });
                    if (auto* slider = qobject_cast<FXSliderRow_Qt*>(editor)) sliders.push_back({slider, text});
                    c.floatEditor = editor;
                    rowWidget = editor;
                    break;
                }
                case FXParamType::Bool: {
                    auto* check = new QCheckBox();
                    check->setObjectName(base + ".check");
                    check->setChecked(p.value != 0.0f);
                    connect(check, &QCheckBox::toggled, this, [this, active, fxNow, group, name](bool on) {
                        const int at = fxNow();
                        if (refreshing_ || at < 0) return;
                        jefe::qt::setFXParamValueOnPlate(active, at, group, name, on ? 1.0f : 0.0f);
                        emit viewportRepaintRequested();
                        scheduleRefresh();  // label accent follows on the next turn
                    });
                    QLabel* label = nullptr;
                    rowWidget = makeLabelledRow(base, text, tooltip, check, &label, columnRow);
                    c.control = check;
                    c.label = label;
                    break;
                }
                case FXParamType::Choice:
                case FXParamType::Texture: {
                    const bool texture = p.type == FXParamType::Texture;
                    QComboBox* combo = makeCombo();
                    combo->setObjectName(base + (texture ? ".texture" : ".combo"));
                    if (texture) {
                        // 0 = "Previous" (prior pass FBO), 1..4 = Track A..D, as gfcFX::bind() maps it.
                        combo->addItems({"Previous", "Track A", "Track B", "Track C", "Track D"});
                    } else {
                        for (const auto& opt : p.options) combo->addItem(QString::fromStdString(opt));
                    }
                    const int idx = static_cast<int>(p.value);
                    if (idx >= 0 && idx < combo->count()) combo->setCurrentIndex(idx);
                    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                            [this, active, fxNow, group, name](int v) {
                                const int at = fxNow();
                                if (refreshing_ || at < 0) return;
                                jefe::qt::setFXParamValueOnPlate(active, at, group, name, static_cast<float>(v));
                                emit viewportRepaintRequested();
                                scheduleRefresh();
                            });
                    QLabel* label = nullptr;
                    rowWidget = makeLabelledRow(base, text, tooltip, combo, &label, columnRow);
                    c.control = combo;
                    c.label = label;
                    break;
                }
                case FXParamType::Cube:
                case FXParamType::LUT: {
                    // The combo shows LUT names but stores the global lutManager index
                    // (gfcFX::bind() reads getLUT(value)), carried in the item data.
                    const bool is3D = p.type == FXParamType::Cube;
                    const auto choices = is3D ? jefe::qt::getCubeLutChoices() : jefe::qt::getLut1DChoices();
                    QWidget* control = nullptr;
                    if (choices.empty()) {
                        auto* none = new QLabel(QString("(no %1 LUTs loaded)").arg(is3D ? "3D" : "1D"));
                        none->setStyleSheet("color:#888; font-style:italic;");
                        none->setObjectName(base + ".value");
                        control = none;
                    } else {
                        QComboBox* combo = makeCombo();
                        combo->setObjectName(base + (is3D ? ".cube" : ".lut"));
                        for (const auto& [globalIdx, lutName] : choices)
                            combo->addItem(QString::fromStdString(lutName), globalIdx);
                        selectByData(combo, static_cast<int>(p.value));
                        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                                [this, active, fxNow, group, name, combo](int v) {
                                    const int at = fxNow();
                                    if (refreshing_ || at < 0) return;
                                    jefe::qt::setFXParamValueOnPlate(active, at, group, name,
                                                                    static_cast<float>(combo->itemData(v).toInt()));
                                    emit viewportRepaintRequested();
                                    scheduleRefresh();  // label accent follows on the next turn
                                });
                        control = combo;
                    }
                    QLabel* label = nullptr;
                    rowWidget = makeLabelledRow(base, text, tooltip, control, &label, columnRow);
                    c.control = control;
                    c.label = label;
                    break;
                }
                default: {
                    auto* value = new QLabel(QString::number(static_cast<double>(p.value), 'g', 6));
                    value->setObjectName(base + ".value");
                    QLabel* label = nullptr;
                    rowWidget = makeLabelledRow(base, text, tooltip, value, &label, columnRow);
                    c.control = value;
                    c.label = label;
                    break;
                }
            }

            // Every non-float row: reset menu, accent label when modified, else the file's colour.
            if (c.label) {
                attachResetMenu(rowWidget, [this, active, fxNow, group, name, def = p.defaultValue]() {
                    const int at = fxNow();
                    if (at < 0) return;
                    jefe::qt::setFXParamValueOnPlate(active, at, group, name, def);
                    emit viewportRepaintRequested();
                    scheduleRefresh();
                });
            }
            if (columnRow && c.label) columnLabels.push_back({c.label, c.control, text});
            if (c.floatEditor || qobject_cast<QCheckBox*>(c.control) || qobject_cast<QComboBox*>(c.control))
                ++editableCount;
            cacheByParam[pi] = std::move(c);
            cached[pi] = true;
            ++paramCount;
            return rowWidget;
        };

        for (const fxui::Row& row : rows) {
            switch (row.kind) {
                case fxui::RowKind::Heading: {
                    auto* heading = new QLabel(QString::fromStdString(row.group), card);
                    heading->setObjectName(QString("%1.group.%2").arg(fxBase, QString::fromStdString(row.group)));
                    heading->setStyleSheet("color: #777; font-style: italic;");
                    heading->setFixedHeight(ui::kControlH);
                    cardLay->addWidget(heading);
                    break;
                }
                case fxui::RowKind::Slider:
                    cardLay->addWidget(buildParam(row.items.front()));
                    break;
                case fxui::RowKind::Flow: {
                    // A line holding one labelled control lines up with the slider rows;
                    // several controls on a line stay packed so they can wrap.
                    const fxui::Kind k = items[row.items.front()].kind;
                    if (row.items.size() == 1 && k != fxui::Kind::Float && k != fxui::Kind::Spacer &&
                        k != fxui::Kind::NewLine) {
                        columnRow = true;
                        cardLay->addWidget(buildParam(row.items.front()));
                        columnRow = false;
                        break;
                    }
                    auto* host = new QWidget(card);
                    auto* flow = new FlowLayout_Qt(host, 0, ui::kGap, ui::kGapTight);
                    for (int pi : row.items) flow->addWidget(buildParam(pi));
                    cardLay->addWidget(host);
                    break;
                }
            }
        }
        // The theme gives every plain QWidget the window background; the card's own
        // containers (header, rows, flow hosts, spacers, drawn sliders and knobs) must
        // show the card colour instead. A property rule only touches their background.
        for (QWidget* w : card->findChildren<QWidget*>())
            if (w->metaObject() == &QWidget::staticMetaObject || qobject_cast<FXParamEditor_Qt*>(w))
                w->setProperty("fxClear", true);

        // Cache in declaration order, spacers and newLines skipped: refreshValuesOnly walks
        // the stack the same way.
        for (size_t pi = 0; pi < cacheByParam.size(); ++pi)
            if (cached[pi] && !isLayoutOnly(fx.params[pi].type)) rowCache_.push_back(std::move(cacheByParam[pi]));
        sliderLabels_.push_back(std::move(sliders));
        columnLabels_.push_back(std::move(columnLabels));

        // Host the card as a list item so InternalMove can drag-reorder.
        auto* item = new QListWidgetItem(list_);
        item->setData(kFxIndexRole, fxIdx);
        // Keep the default item flags (drag+drop enabled) so InternalMove
        // reordering works; just disable inline rename.
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        // Width 0: the item spans the list viewport width, so the list never scrolls
        // horizontally. relayoutCards() sets the height for that width.
        item->setSizeHint(QSize(0, card->sizeHint().height()));
        list_->setItemWidget(item, card);
    }

    for (CachedRow& c : rowCache_) {
        if (!c.label) continue;
        const bool modified = fxui::isModified(c.value, c.defaultValue, 1.f);
        c.labelState = modified ? 1 : 0;
        if (modified) c.label->setStyleSheet(QString("color: %1;").arg(jefe::qt::kFXModifiedColor));
        else if (!c.fileColour.isEmpty()) c.label->setStyleSheet(QString("color: %1;").arg(c.fileColour));
    }

    status_->setText(QString("Plate %1 — %2 FX, %3 params (%4 editable)")
                         .arg(active).arg(stack.size()).arg(paramCount).arg(editableCount));
    relayoutCards();
    refreshing_ = false;
}

bool FXParamPanel_Qt::eventFilter(QObject* watched, QEvent* event) {
    if (watched == list_->viewport() && event->type() == QEvent::Resize) relayoutCards();
    return QWidget::eventFilter(watched, event);
}

void FXParamPanel_Qt::relayoutCards() {
    const int cardWidth = list_->viewport()->width() - 2 * list_->spacing();
    if (cardWidth <= 0) return;

    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        QWidget* card = list_->itemWidget(item);
        if (!card) continue;

        // Label column: as wide as the card's longest slider or column-row label when
        // every control keeps its minimum width, else narrower with the labels elided
        // (full text in the tooltip). Never more than 45 % of the card.
        const bool haveRows = i < int(sliderLabels_.size()) && i < int(columnLabels_.size()) &&
                              (!sliderLabels_[i].empty() || !columnLabels_[i].empty());
        if (cardWidth != lastCardWidth_ && haveRows) {
            const auto& sliders = sliderLabels_[i];
            const auto& columns = columnLabels_[i];
            QLabel* probe = nullptr;  // any live label: the theme's 13 px font, not the default
            for (const SliderLabel& s : sliders)
                if (s.row) { probe = s.row->label(); break; }
            if (!probe)
                for (const ColumnLabel& c : columns)
                    if (c.label) { probe = c.label; break; }
            if (probe) {
                probe->ensurePolished();
                const QFontMetrics fm(probe->font());
                const QMargins m = card->layout()->contentsMargins();
                const int inner = cardWidth - m.left() - m.right() - 2;  // 1 px card border each side
                int widest = 0;
                int room = inner;
                for (const SliderLabel& s : sliders) {
                    if (!s.row) continue;
                    widest = std::max(widest, fm.horizontalAdvance(s.text) + 1);
                    const int spacing = s.row->layout() ? s.row->layout()->spacing() : ui::kGapTight;
                    room = std::min(room, inner - s.row->field()->sizeHint().width() -
                                              s.row->slider()->minimumWidth() - ui::kControlH - 3 * spacing);
                }
                for (const ColumnLabel& c : columns) {
                    if (!c.label) continue;
                    widest = std::max(widest, fm.horizontalAdvance(c.text) + 1);
                    if (c.control) room = std::min(room, inner - c.control->minimumSizeHint().width() - ui::kGapTight);
                }
                const int column = std::max(ui::kControlH, std::min({widest, room, inner * 45 / 100}));
                const auto fit = [&](QLabel* label, const QString& text) {
                    const QString shown = fm.elidedText(text, Qt::ElideRight, column);
                    if (shown != label->text()) label->setText(shown);
                    label->setProperty(ui::kDensityElidedProperty, true);
                    return shown == text;
                };
                for (const SliderLabel& s : sliders) {
                    if (!s.row) continue;
                    s.row->setLabelColumnWidth(column);
                    QLabel* label = s.row->label();
                    label->setToolTip(fit(label, s.text) ? QString() : s.text);
                }
                for (const ColumnLabel& c : columns) {
                    if (!c.label) continue;
                    c.label->setFixedWidth(column);
                    c.label->setToolTip(fit(c.label, c.text) ? QString() : c.text);
                }
            }
        }

        const int h = card->hasHeightForWidth() ? card->heightForWidth(cardWidth) : card->sizeHint().height();
        if (h > 0 && item->sizeHint().height() != h) item->setSizeHint(QSize(0, h));
    }
    lastCardWidth_ = cardWidth;
}

bool FXParamPanel_Qt::refreshValuesOnly(const std::vector<jefe::qt::FXMeta>& stack) {
    // Walk the params in the order the rebuild cached them.
    std::vector<const jefe::qt::FXParamMeta*> flat;
    flat.reserve(rowCache_.size());
    for (const auto& fx : stack)
        for (const auto& p : fx.params)
            if (!isLayoutOnly(p.type)) flat.push_back(&p);
    if (flat.size() != rowCache_.size() || stack.size() != powerButtons_.size()) return false;

    // Active state can change outside the panel (a peer, a favourite, the bridge).
    for (size_t i = 0; i < stack.size(); ++i) {
        QToolButton* power = powerButtons_[i].data();
        if (!power || power->isChecked() == stack[i].active) continue;  // cache-gated (§5c)
        const QSignalBlocker b(power);
        power->setChecked(stack[i].active);
    }

    using jefe::qt::FXParamType;
    for (size_t i = 0; i < rowCache_.size(); ++i) {
        CachedRow& c = rowCache_[i];
        const auto* p = flat[i];
        if (c.group != p->group || c.name != p->name || c.paramType != static_cast<int>(p->type)) return false;
        if (c.value == p->value) continue;  // unchanged: no widget write (developer_notes.md §5c)

        if (c.floatEditor) {
            // The editor ignores setValue while dragged or typed in; leave the cache
            // stale then, so the next refresh after the interaction syncs it.
            if (c.floatEditor->isInteracting()) continue;
            c.floatEditor->setValue(p->value);
            c.value = p->value;
            continue;
        }
        c.value = p->value;
        QWidget* w = c.control.data();
        if (!w) continue;
        switch (static_cast<FXParamType>(c.paramType)) {
            case FXParamType::Bool:
                if (auto* check = qobject_cast<QCheckBox*>(w)) {
                    const QSignalBlocker b(check);
                    check->setChecked(p->value != 0.0f);
                }
                break;
            case FXParamType::Choice:
            case FXParamType::Texture:
                if (auto* combo = qobject_cast<QComboBox*>(w)) {
                    const QSignalBlocker b(combo);
                    const int idx = static_cast<int>(p->value);
                    if (idx >= 0 && idx < combo->count()) combo->setCurrentIndex(idx);
                }
                break;
            case FXParamType::Cube:
            case FXParamType::LUT:
                if (auto* combo = qobject_cast<QComboBox*>(w)) {
                    const QSignalBlocker b(combo);
                    selectByData(combo, static_cast<int>(p->value));
                }
                break;
            default:
                if (auto* lab = qobject_cast<QLabel*>(w)) lab->setText(QString::number(static_cast<double>(p->value), 'g', 6));
                break;
        }
        if (c.label) {
            const int state = fxui::isModified(c.value, c.defaultValue, 1.f) ? 1 : 0;
            if (state != c.labelState) {
                c.labelState = state;
                c.label->setStyleSheet(state ? QString("color: %1;").arg(jefe::qt::kFXModifiedColor)
                                             : (c.fileColour.isEmpty() ? QString()
                                                                       : QString("color: %1;").arg(c.fileColour)));
            }
        }
    }
    return true;
}
