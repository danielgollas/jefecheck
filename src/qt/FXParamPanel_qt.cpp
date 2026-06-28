#include "FXParamPanel_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QMenu>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace {

// Stack index of an FX, stashed on its list item so the reorder math can
// reconstruct the pre-drop order after QListWidget shuffles items.
constexpr int kFxIndexRole = Qt::UserRole;

QString typeLabel(jefe::qt::FXParamType t) {
    using T = jefe::qt::FXParamType;
    switch (t) {
        case T::Float:   return "float";
        case T::Bool:    return "bool";
        case T::Choice:  return "choice";
        case T::Texture: return "texture";
        case T::Cube:    return "cube";
        case T::LUT:     return "lut";
        case T::Spacer:  return "spacer";
        case T::Newline: return "newline";
        case T::Other:   return "other";
        case T::Unknown: return "unknown";
    }
    return "unknown";
}

// Number of decimals for a float spinbox: derive from `step` so values
// like step=0.001 get 3 decimals and step=1.0 gets 0. Clamp to a sane
// range so a pathologically tiny step doesn't blow the field width up.
int decimalsForStep(float step) {
    if (step <= 0.0f) return 3;
    const float abs_step = std::fabs(step);
    if (abs_step >= 1.0f) return 0;
    int d = 1;
    float s = abs_step;
    while (s < 0.1f && d < 6) { s *= 10.0f; ++d; }
    return d;
}

}  // namespace

// ---------------------------------------------------------------------------
// FXReorderList_Qt
// ---------------------------------------------------------------------------

void FXReorderList_Qt::dropEvent(QDropEvent* e) {
    // Capture the stack indices in their pre-drop order. Each item carries
    // its build-time stack position in kFxIndexRole; a fresh rebuild stores
    // these as the identity sequence [0, 1, … n-1], so `before` is identity
    // and the moved element's old position equals its stored value.
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

// ---------------------------------------------------------------------------
// FXParamPanel_Qt
// ---------------------------------------------------------------------------

FXParamPanel_Qt::FXParamPanel_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("fxparams.panel");

    // "+ Add FX" — InstantPopup tool button driving a hierarchical menu
    // built from each FX's menuName. Rebuilt on aboutToShow so a late
    // autoload is reflected.
    addButton_ = new QToolButton(this);
    addButton_->setObjectName("fxparams.addfx.button");
    addButton_->setAccessibleName("Add FX");
    addButton_->setText("+ Add FX");
    addButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    addButton_->setPopupMode(QToolButton::InstantPopup);

    addMenu_ = new QMenu(this);
    addMenu_->setObjectName("fxparams.addfx.menu");
    addButton_->setMenu(addMenu_);
    connect(addMenu_, &QMenu::aboutToShow, this, [this]() { rebuildAddMenu(); });

    // No accessibleName on the status label — Mac AX otherwise reports the
    // QLabel under its accessibleName (AXTitle) instead of letting setText
    // drive AXValue.
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
    // carry the active/remove buttons off-view when the dock is narrow).
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(list_, &FXReorderList_Qt::itemsReordered, this,
            [this](int from, int to) {
                if (refreshing_) return;
                const int active = jefe::qt::getActivePlate();
                if (active < 0) return;
                jefe::qt::moveFXOnPlate(active, from, to);
                emit viewportRepaintRequested();
                // Defer the rebuild: we're inside the drop's call stack and
                // tearing down the list items now is unsafe.
                QTimer::singleShot(0, this, [this]() { refresh(); });
            });

    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->addWidget(addButton_);
    topRow->addStretch(1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);
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

void FXParamPanel_Qt::refresh() {
    const int active = jefe::qt::getActivePlate();

    // Fast path: when the active plate and the FX stack's name list
    // haven't changed since the last rebuild, the editor widgets are
    // still valid — just refresh their values in place. plateStateChanged
    // fires per viewport mouse-move, so without this every drag pixel tore
    // down + rebuilt the whole card hierarchy.
    if (active >= 0 && active == lastActivePlate_) {
        const auto stackNames = jefe::qt::getFXStackOnPlate(active);
        if (stackNames == lastStack_ && !stackNames.empty()) {
            refreshing_ = true;
            const bool ok = refreshValuesOnly();
            refreshing_ = false;
            if (ok) return;
            // Fall through to full rebuild if the cached row list is stale.
        }
    }

    refreshing_ = true;

    // Tear down existing cards synchronously. Immediate clear (vs
    // deleteLater) keeps the AX tree coherent.
    list_->clear();
    rowCache_.clear();

    if (active < 0) {
        status_->setText("No active plate.");
        refreshing_ = false;
        lastActivePlate_ = active;
        lastStack_.clear();
        return;
    }

    const auto stack = jefe::qt::getFXStackMetaOnPlate(active);
    if (stack.empty()) {
        status_->setText(QString("Plate %1: no FX. Use \"+ Add FX\".").arg(active));
        refreshing_ = false;
        lastActivePlate_ = active;
        lastStack_.clear();
        return;
    }

    int paramCount = 0;
    int editableCount = 0;
    for (size_t i = 0; i < stack.size(); ++i) {
        const auto& fx = stack[i];
        const int fxIdx = static_cast<int>(i);

        auto* card = new QFrame();
        card->setObjectName(QString("fxparams.fx%1.card").arg(fxIdx));
        card->setFrameShape(QFrame::StyledPanel);
        card->setStyleSheet(
            "QFrame { background: #2b2b2b; border: 1px solid #3a3a3a;"
            " border-radius: 4px; }");
        auto* cardLay = new QVBoxLayout(card);
        cardLay->setContentsMargins(8, 6, 8, 6);
        cardLay->setSpacing(4);

        // --- Header row: drag handle + index/name + active + remove -----
        auto* headerRow = new QWidget(card);
        headerRow->setStyleSheet("background: transparent; border: none;");
        auto* headerLay = new QHBoxLayout(headerRow);
        headerLay->setContentsMargins(0, 0, 0, 0);
        headerLay->setSpacing(6);

        auto* handle = new QLabel("☰", headerRow);  // ☰ drag glyph
        handle->setStyleSheet("color: #777; border: none;");
        handle->setToolTip("Drag to reorder");
        headerLay->addWidget(handle);

        const QString fxDisplay = QString::fromStdString(
            !fx.menuName.empty() ? fx.menuName : fx.name);
        auto* nameLab = new QLabel(
            QString("<b>%1.</b> %2")
                .arg(fxIdx + 1)
                .arg(fxDisplay.toHtmlEscaped()),
            headerRow);
        nameLab->setTextFormat(Qt::RichText);
        nameLab->setStyleSheet("border: none;");
        nameLab->setObjectName(QString("fxparams.fx%1.header").arg(fxIdx));
        nameLab->setToolTip(fxDisplay);
        // Let the name absorb slack AND shrink/clip below its text width so a
        // long menuName never pushes the active/remove buttons out of view or
        // forces the whole dock wider. Ignored = ignore size + minimum hints.
        nameLab->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        headerLay->addWidget(nameLab, /*stretch*/ 1);

        auto* activeChk = new QCheckBox(headerRow);
        activeChk->setObjectName(QString("fxparams.fx%1.active.check").arg(fxIdx));
        activeChk->setAccessibleName(QString("FX %1 active").arg(fxIdx));
        activeChk->setToolTip("Enable / disable this effect");
        activeChk->setStyleSheet("border: none;");
        activeChk->setChecked(fx.active);
        connect(activeChk, &QCheckBox::toggled, this,
                [this, active, fxIdx](bool on) {
                    if (refreshing_) return;
                    jefe::qt::setFXActiveOnPlate(active, fxIdx, on);
                    emit viewportRepaintRequested();
                });
        headerLay->addWidget(activeChk);

        auto* removeBtn = new QToolButton(headerRow);
        removeBtn->setObjectName(QString("fxparams.fx%1.remove.button").arg(fxIdx));
        removeBtn->setAccessibleName(QString("Remove FX %1").arg(fxIdx));
        removeBtn->setText("⌫");  // ⌫
        removeBtn->setToolTip("Remove this effect");
        removeBtn->setStyleSheet("border: none;");
        connect(removeBtn, &QToolButton::clicked, this,
                [this, active, fxIdx]() {
                    if (refreshing_) return;
                    jefe::qt::removeFXFromPlate(active, fxIdx);
                    emit viewportRepaintRequested();
                    // Defer: the button lives inside the card we're about
                    // to delete, so don't rebuild inside its own click.
                    QTimer::singleShot(0, this, [this]() { refresh(); });
                });
        headerLay->addWidget(removeBtn);

        cardLay->addWidget(headerRow);

        // --- Params ------------------------------------------------------
        QString lastGroup;
        for (const auto& p : fx.params) {
            if (p.type == jefe::qt::FXParamType::Spacer ||
                p.type == jefe::qt::FXParamType::Newline) {
                continue;
            }

            const QString groupName = QString::fromStdString(p.group);
            if (groupName != lastGroup && !groupName.isEmpty()) {
                auto* gLabel = new QLabel(
                    QString("<i style='color:#777'>%1</i>")
                        .arg(groupName.toHtmlEscaped()),
                    card);
                gLabel->setTextFormat(Qt::RichText);
                gLabel->setStyleSheet("border: none;");
                cardLay->addWidget(gLabel);
                lastGroup = groupName;
            }

            const QString labelText = !p.label.empty()
                ? QString::fromStdString(p.label)
                : QString::fromStdString(p.name);
            const std::string groupStd = p.group;
            const std::string nameStd  = p.name;

            auto* row = new QWidget(card);
            row->setStyleSheet("border: none;");
            row->setObjectName(
                QString("fxparams.fx%1.param.%2.row")
                    .arg(fxIdx)
                    .arg(QString::fromStdString(p.name)));
            auto* rowLay = new QHBoxLayout(row);
            rowLay->setContentsMargins(12, 0, 0, 0);
            rowLay->setSpacing(6);

            auto* paramLab = new QLabel(labelText + ":", row);
            paramLab->setMinimumWidth(50);
            paramLab->setStyleSheet("border: none;");
            paramLab->setObjectName(
                QString("fxparams.fx%1.param.%2.label")
                    .arg(fxIdx)
                    .arg(QString::fromStdString(p.name)));
            rowLay->addWidget(paramLab);

            QWidget* editor = nullptr;
            switch (p.type) {
                case jefe::qt::FXParamType::Float: {
                    auto* spin = new QDoubleSpinBox(row);
                    spin->setObjectName(
                        QString("fxparams.fx%1.param.%2.spin")
                            .arg(fxIdx)
                            .arg(QString::fromStdString(p.name)));
                    spin->setRange(static_cast<double>(p.minimum),
                                   static_cast<double>(p.maximum));
                    spin->setDecimals(decimalsForStep(p.step));
                    spin->setSingleStep(p.step > 0 ? p.step : 0.001);
                    spin->setValue(static_cast<double>(p.value));
                    spin->setKeyboardTracking(false);
                    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                            this, [this, active, fxIdx, groupStd, nameStd](double v) {
                                if (refreshing_) return;
                                jefe::qt::setFXParamValueOnPlate(
                                    active, fxIdx, groupStd, nameStd,
                                    static_cast<float>(v));
                                emit viewportRepaintRequested();
                            });
                    editor = spin;
                    ++editableCount;
                    break;
                }
                case jefe::qt::FXParamType::Bool: {
                    auto* check = new QCheckBox(row);
                    check->setStyleSheet("border: none;");
                    check->setObjectName(
                        QString("fxparams.fx%1.param.%2.check")
                            .arg(fxIdx)
                            .arg(QString::fromStdString(p.name)));
                    check->setChecked(p.value != 0.0f);
                    connect(check, &QCheckBox::toggled,
                            this, [this, active, fxIdx, groupStd, nameStd](bool on) {
                                if (refreshing_) return;
                                jefe::qt::setFXParamValueOnPlate(
                                    active, fxIdx, groupStd, nameStd,
                                    on ? 1.0f : 0.0f);
                                emit viewportRepaintRequested();
                            });
                    editor = check;
                    ++editableCount;
                    break;
                }
                case jefe::qt::FXParamType::Choice: {
                    auto* combo = new QComboBox(row);
                    combo->setObjectName(
                        QString("fxparams.fx%1.param.%2.combo")
                            .arg(fxIdx)
                            .arg(QString::fromStdString(p.name)));
                    for (const auto& opt : p.options) {
                        combo->addItem(QString::fromStdString(opt));
                    }
                    const int idx = static_cast<int>(p.value);
                    if (idx >= 0 && idx < combo->count()) {
                        combo->setCurrentIndex(idx);
                    }
                    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                            this, [this, active, fxIdx, groupStd, nameStd](int v) {
                                if (refreshing_) return;
                                jefe::qt::setFXParamValueOnPlate(
                                    active, fxIdx, groupStd, nameStd,
                                    static_cast<float>(v));
                                emit viewportRepaintRequested();
                            });
                    editor = combo;
                    ++editableCount;
                    break;
                }
                case jefe::qt::FXParamType::Texture: {
                    // Texture input picker. Matches the FLTK FX-control
                    // Fl_Choice: the source feeding this texture uniform is
                    // "Previous" (the FBO result of the prior pass) or one of
                    // the four tracks. The stored value is the option index,
                    // which gfcFX::bind() maps: 0 = previousTexID, 1..4 =
                    // trackManager.getSequence(value-1) (Track A..D).
                    auto* combo = new QComboBox(row);
                    combo->setObjectName(
                        QString("fxparams.fx%1.param.%2.texture")
                            .arg(fxIdx)
                            .arg(QString::fromStdString(p.name)));
                    combo->addItem("Previous");
                    combo->addItem("Track A");
                    combo->addItem("Track B");
                    combo->addItem("Track C");
                    combo->addItem("Track D");
                    const int idx = static_cast<int>(p.value);
                    if (idx >= 0 && idx < combo->count()) {
                        combo->setCurrentIndex(idx);
                    }
                    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                            this, [this, active, fxIdx, groupStd, nameStd](int v) {
                                if (refreshing_) return;
                                jefe::qt::setFXParamValueOnPlate(
                                    active, fxIdx, groupStd, nameStd,
                                    static_cast<float>(v));
                                emit viewportRepaintRequested();
                            });
                    editor = combo;
                    ++editableCount;
                    break;
                }
                case jefe::qt::FXParamType::Cube:
                case jefe::qt::FXParamType::LUT: {
                    // LUT picker. Cube = 3D LUTs, LUT = 1D LUTs. Matches the
                    // FLTK Fl_Choice fed by lutManager.get3DLutNames() /
                    // get1DLutNames(). The combo DISPLAYS names but STORES the
                    // global lutManager index (display order != stored value),
                    // carried per-item in Qt::UserRole; gfcFX::bind() reads
                    // getLUT(value).texture3D/.texture1D.
                    const bool is3D = (p.type == jefe::qt::FXParamType::Cube);
                    const auto choices = is3D ? jefe::qt::getCubeLutChoices()
                                              : jefe::qt::getLut1DChoices();
                    if (choices.empty()) {
                        // No LUTs of this kind loaded — show a hint instead of
                        // an empty combo (a full rebuild picks them up later).
                        auto* valLab = new QLabel(
                            QString("(no %1 LUTs loaded)").arg(is3D ? "3D" : "1D"),
                            row);
                        valLab->setStyleSheet(
                            "color:#888; font-style:italic; border:none;");
                        valLab->setObjectName(
                            QString("fxparams.fx%1.param.%2.value")
                                .arg(fxIdx)
                                .arg(QString::fromStdString(p.name)));
                        editor = valLab;
                        break;
                    }
                    auto* combo = new QComboBox(row);
                    combo->setObjectName(
                        QString("fxparams.fx%1.param.%2.%3")
                            .arg(fxIdx)
                            .arg(QString::fromStdString(p.name))
                            .arg(is3D ? "cube" : "lut"));
                    const int cur = static_cast<int>(p.value);   // global lut index
                    int curPos = -1;
                    for (int ci = 0; ci < (int)choices.size(); ++ci) {
                        combo->addItem(QString::fromStdString(choices[ci].second),
                                       choices[ci].first);  // global index in UserRole
                        if (choices[ci].first == cur) curPos = ci;
                    }
                    if (curPos >= 0) combo->setCurrentIndex(curPos);
                    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                            this, [this, active, fxIdx, groupStd, nameStd, combo](int v) {
                                if (refreshing_) return;
                                const int globalIdx = combo->itemData(v).toInt();
                                jefe::qt::setFXParamValueOnPlate(
                                    active, fxIdx, groupStd, nameStd,
                                    static_cast<float>(globalIdx));
                                emit viewportRepaintRequested();
                            });
                    editor = combo;
                    ++editableCount;
                    break;
                }
                default: {
                    // Other — read-only display.
                    auto* valLab = new QLabel(
                        QString::number(static_cast<double>(p.value), 'g', 6),
                        row);
                    valLab->setStyleSheet(
                        "color: #ddd; font-family: monospace; border: none;");
                    valLab->setObjectName(
                        QString("fxparams.fx%1.param.%2.value")
                            .arg(fxIdx)
                            .arg(QString::fromStdString(p.name)));
                    editor = valLab;
                    break;
                }
            }
            // Let the editor compress further than its content-based minimum
            // when the dock is narrowed. Spinboxes/combos otherwise impose a
            // wide floor (longest item / digit count) that keeps the whole
            // panel from shrinking. Ignored horizontal policy drops that floor;
            // a small explicit minimum keeps the control usable, and the row's
            // stretch still lets it grow when there's room.
            if (editor) {
                editor->setMinimumWidth(40);
                editor->setSizePolicy(QSizePolicy::Ignored,
                                      editor->sizePolicy().verticalPolicy());
            }
            rowLay->addWidget(editor, /*stretch*/ 1);

            CachedRow cached;
            cached.fxIdx = fxIdx;
            cached.group = p.group;
            cached.name = p.name;
            cached.paramType = static_cast<int>(p.type);
            cached.value = p.value;
            cached.editor = editor;
            rowCache_.push_back(std::move(cached));

            auto* typeLab = new QLabel(
                QString("<span style='color:#666'>(%1)</span>")
                    .arg(typeLabel(p.type)),
                row);
            typeLab->setTextFormat(Qt::RichText);
            typeLab->setStyleSheet("border: none;");
            // Don't let the type hint impose a width floor — it's the first
            // thing that can give when the dock is narrowed.
            typeLab->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            rowLay->addWidget(typeLab);

            cardLay->addWidget(row);
            ++paramCount;
        }

        // Host the card as a list item so InternalMove can drag-reorder.
        auto* item = new QListWidgetItem(list_);
        item->setData(kFxIndexRole, fxIdx);
        // Keep the default item flags (drag+drop enabled) so InternalMove
        // reordering works; just disable inline rename.
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        // Width 0 → the item spans the list viewport width (never wider), so
        // the card is sized to the dock and the list never scrolls
        // horizontally (which would slide the active/remove buttons off-view).
        // Only the height comes from the card's content.
        item->setSizeHint(QSize(0, card->sizeHint().height()));
        list_->setItemWidget(item, card);
    }

    status_->setText(
        QString("Plate %1 — %2 FX, %3 params (%4 editable)")
            .arg(active)
            .arg(stack.size())
            .arg(paramCount)
            .arg(editableCount));

    // Update the stack fingerprint for the next refresh() call. Names only
    // — mirror getFXStackOnPlate's name selection (menuName preferred) so
    // the fast-path equality check works.
    lastActivePlate_ = active;
    lastStack_.clear();
    lastStack_.reserve(stack.size());
    for (const auto& fx : stack) {
        lastStack_.push_back(fx.menuName.empty() ? fx.name : fx.menuName);
    }

    refreshing_ = false;
}

bool FXParamPanel_Qt::refreshValuesOnly() {
    // Fast-path partner to refresh(). Walks the cached editor list and
    // re-reads the bridge's current param values; only writes a widget
    // when the value actually changed. Returns false to demand a full
    // rebuild if the bridge's row layout no longer matches our cache.
    const int active = lastActivePlate_;
    if (active < 0) return false;

    const auto stack = jefe::qt::getFXStackMetaOnPlate(active);
    if (stack.empty()) return false;

    std::vector<const jefe::qt::FXParamMeta*> flat;
    flat.reserve(rowCache_.size());
    for (const auto& fx : stack) {
        for (const auto& p : fx.params) {
            if (p.type == jefe::qt::FXParamType::Spacer ||
                p.type == jefe::qt::FXParamType::Newline) {
                continue;
            }
            flat.push_back(&p);
        }
    }
    if (flat.size() != rowCache_.size()) return false;

    for (size_t i = 0; i < rowCache_.size(); ++i) {
        auto& cached = rowCache_[i];
        const auto* p = flat[i];

        if (cached.group != p->group ||
            cached.name != p->name ||
            cached.paramType != static_cast<int>(p->type)) {
            return false;
        }

        if (cached.value == p->value) continue;  // unchanged
        cached.value = p->value;

        QWidget* w = cached.editor.data();
        if (!w) continue;  // editor torn down behind our back; skip

        switch (static_cast<jefe::qt::FXParamType>(cached.paramType)) {
            case jefe::qt::FXParamType::Float: {
                if (auto* spin = qobject_cast<QDoubleSpinBox*>(w)) {
                    const QSignalBlocker b(spin);
                    spin->setValue(static_cast<double>(p->value));
                }
                break;
            }
            case jefe::qt::FXParamType::Bool: {
                if (auto* check = qobject_cast<QCheckBox*>(w)) {
                    const QSignalBlocker b(check);
                    check->setChecked(p->value != 0.0f);
                }
                break;
            }
            case jefe::qt::FXParamType::Choice:
            case jefe::qt::FXParamType::Texture: {
                if (auto* combo = qobject_cast<QComboBox*>(w)) {
                    const QSignalBlocker b(combo);
                    const int idx = static_cast<int>(p->value);
                    if (idx >= 0 && idx < combo->count()) {
                        combo->setCurrentIndex(idx);
                    }
                }
                break;
            }
            case jefe::qt::FXParamType::Cube:
            case jefe::qt::FXParamType::LUT: {
                // value is a global lut index; find the item carrying it.
                if (auto* combo = qobject_cast<QComboBox*>(w)) {
                    const QSignalBlocker b(combo);
                    const int want = static_cast<int>(p->value);
                    for (int i = 0; i < combo->count(); ++i) {
                        if (combo->itemData(i).toInt() == want) {
                            combo->setCurrentIndex(i);
                            break;
                        }
                    }
                }
                break;
            }
            default: {
                if (auto* lab = qobject_cast<QLabel*>(w)) {
                    lab->setText(QString::number(
                        static_cast<double>(p->value), 'g', 6));
                }
                break;
            }
        }
    }
    return true;
}
