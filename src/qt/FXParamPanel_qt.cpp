#include "FXParamPanel_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include <cmath>

namespace {

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

QFrame* makeSeparator(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setStyleSheet("color: #444;");
    return line;
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

FXParamPanel_Qt::FXParamPanel_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("fxparams.panel");

    // No accessibleName — Mac AX otherwise reports the QLabel under
    // its accessibleName (AXTitle) instead of letting setText drive
    // AXValue. Without one, AX picks up the live text via .text and
    // get_attribute("value"), matching how the status-bar startup
    // label works.
    status_ = new QLabel(this);
    status_->setStyleSheet("color: #888; font-style: italic;");
    status_->setObjectName("fxparams.status.label");
    status_->setText("FX Params: initializing");

    scroll_ = new QScrollArea(this);
    scroll_->setObjectName("fxparams.scroll");
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);

    contentWidget_ = new QWidget(scroll_);
    contentWidget_->setObjectName("fxparams.content");
    contentLayout_ = new QVBoxLayout(contentWidget_);
    contentLayout_->setContentsMargins(8, 8, 8, 8);
    contentLayout_->setSpacing(4);
    contentLayout_->addStretch(1);
    scroll_->setWidget(contentWidget_);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);
    outer->addWidget(status_);
    outer->addWidget(scroll_, /*stretch*/ 1);

    refresh();
}

void FXParamPanel_Qt::refresh() {
    refreshing_ = true;

    // Tear down existing rows synchronously. addStretch is the last
    // item — drop it along with everything else, then re-add at the
    // end. Immediate `delete` (vs deleteLater) keeps the AX tree
    // coherent: deferred deletes leave detached old widgets alive
    // until the next event-loop spin, and a Mac2 predicate query
    // landing in that window can return a stale objectName match
    // for an editor widget that's been replaced but not yet freed.
    while (auto* item = contentLayout_->takeAt(0)) {
        if (auto* w = item->widget()) {
            delete w;
        }
        delete item;
    }

    const int active = jefe::qt::getActivePlate();
    if (active < 0) {
        status_->setText("No active plate.");
        contentLayout_->addStretch(1);
        refreshing_ = false;
        return;
    }

    const auto stack = jefe::qt::getFXStackMetaOnPlate(active);
    if (stack.empty()) {
        status_->setText(QString("Plate %1: no FX on stack.").arg(active));
        contentLayout_->addStretch(1);
        refreshing_ = false;
        return;
    }

    int paramCount = 0;
    int editableCount = 0;
    for (size_t i = 0; i < stack.size(); ++i) {
        const auto& fx = stack[i];
        const int fxIdx = static_cast<int>(i);

        // FX header — name + active flag.
        const QString fxDisplay = QString::fromStdString(
            !fx.menuName.empty() ? fx.menuName : fx.name);
        auto* header = new QLabel(
            QString("<b>%1.</b> %2  <span style='color:#666'>(%3)</span>")
                .arg(fxIdx)
                .arg(fxDisplay.toHtmlEscaped())
                .arg(fx.active ? "active" : "inactive"),
            contentWidget_);
        header->setTextFormat(Qt::RichText);
        header->setObjectName(QString("fxparams.fx%1.header").arg(fxIdx));
        contentLayout_->addWidget(header);

        QString lastGroup;
        for (const auto& p : fx.params) {
            // Skip cosmetic widget types — they have no value to edit.
            if (p.type == jefe::qt::FXParamType::Spacer ||
                p.type == jefe::qt::FXParamType::Newline) {
                continue;
            }

            const QString groupName = QString::fromStdString(p.group);
            if (groupName != lastGroup) {
                auto* gLabel = new QLabel(
                    QString("<i style='color:#777'>%1</i>")
                        .arg(groupName.toHtmlEscaped()),
                    contentWidget_);
                gLabel->setTextFormat(Qt::RichText);
                contentLayout_->addWidget(gLabel);
                lastGroup = groupName;
            }

            const QString labelText = !p.label.empty()
                ? QString::fromStdString(p.label)
                : QString::fromStdString(p.name);
            const std::string groupStd = p.group;
            const std::string nameStd  = p.name;

            auto* row = new QWidget(contentWidget_);
            row->setObjectName(
                QString("fxparams.fx%1.param.%2.row")
                    .arg(fxIdx)
                    .arg(QString::fromStdString(p.name)));
            auto* rowLay = new QHBoxLayout(row);
            rowLay->setContentsMargins(12, 0, 0, 0);
            rowLay->setSpacing(6);

            auto* nameLab = new QLabel(labelText + ":", row);
            nameLab->setMinimumWidth(120);
            nameLab->setObjectName(
                QString("fxparams.fx%1.param.%2.label")
                    .arg(fxIdx)
                    .arg(QString::fromStdString(p.name)));
            rowLay->addWidget(nameLab);

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
                            });
                    editor = spin;
                    ++editableCount;
                    break;
                }
                case jefe::qt::FXParamType::Bool: {
                    auto* check = new QCheckBox(row);
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
                            });
                    editor = combo;
                    ++editableCount;
                    break;
                }
                default: {
                    // Texture / cube / LUT / Other — display read-only.
                    auto* valLab = new QLabel(
                        QString::number(static_cast<double>(p.value), 'g', 6),
                        row);
                    valLab->setStyleSheet("color: #ddd; font-family: monospace;");
                    valLab->setObjectName(
                        QString("fxparams.fx%1.param.%2.value")
                            .arg(fxIdx)
                            .arg(QString::fromStdString(p.name)));
                    editor = valLab;
                    break;
                }
            }
            rowLay->addWidget(editor, /*stretch*/ 1);

            auto* typeLab = new QLabel(
                QString("<span style='color:#666'>(%1)</span>")
                    .arg(typeLabel(p.type)),
                row);
            typeLab->setTextFormat(Qt::RichText);
            rowLay->addWidget(typeLab);

            contentLayout_->addWidget(row);
            ++paramCount;
        }

        if (i + 1 < stack.size()) {
            contentLayout_->addWidget(makeSeparator(contentWidget_));
        }
    }

    contentLayout_->addStretch(1);
    status_->setText(
        QString("Plate %1 — %2 FX, %3 params (%4 editable)")
            .arg(active)
            .arg(stack.size())
            .arg(paramCount)
            .arg(editableCount));

    refreshing_ = false;
}
