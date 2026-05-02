#include "FXParamPanel_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

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

QString formatValue(const jefe::qt::FXParamMeta& p) {
    using T = jefe::qt::FXParamType;
    switch (p.type) {
        case T::Float:
            return QString::number(p.value, 'g', 6);
        case T::Bool:
            return p.value != 0.0f ? "true" : "false";
        case T::Choice: {
            const int idx = static_cast<int>(p.value);
            if (idx >= 0 && idx < static_cast<int>(p.options.size())) {
                return QString::fromStdString(p.options[idx]);
            }
            return QString("[%1]").arg(idx);
        }
        case T::Spacer:
        case T::Newline:
            return QString();
        default:
            return QString::number(p.value, 'g', 6);
    }
}

QFrame* makeSeparator(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setStyleSheet("color: #444;");
    return line;
}

}  // namespace

FXParamPanel_Qt::FXParamPanel_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("fxparams.panel");
    setAccessibleName("FX Parameters");

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
    // Tear down existing rows. addStretch is the last item — drop it
    // along with everything else, then re-add at the end.
    while (auto* item = contentLayout_->takeAt(0)) {
        if (auto* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }

    const int active = jefe::qt::getActivePlate();
    if (active < 0) {
        status_->setText("No active plate.");
        contentLayout_->addStretch(1);
        return;
    }

    const auto stack = jefe::qt::getFXStackMetaOnPlate(active);
    if (stack.empty()) {
        status_->setText(QString("Plate %1: no FX on stack.").arg(active));
        contentLayout_->addStretch(1);
        return;
    }

    int paramCount = 0;
    for (size_t i = 0; i < stack.size(); ++i) {
        const auto& fx = stack[i];

        // FX header — name + active flag.
        const QString fxDisplay = QString::fromStdString(
            !fx.menuName.empty() ? fx.menuName : fx.name);
        auto* header = new QLabel(
            QString("<b>%1.</b> %2  <span style='color:#666'>(%3)</span>")
                .arg(static_cast<int>(i))
                .arg(fxDisplay.toHtmlEscaped())
                .arg(fx.active ? "active" : "inactive"),
            contentWidget_);
        header->setTextFormat(Qt::RichText);
        header->setObjectName(
            QString("fxparams.fx%1.header").arg(static_cast<int>(i)));
        contentLayout_->addWidget(header);

        QString lastGroup;
        for (const auto& p : fx.params) {
            // Skip cosmetic widget types — they have no value to show.
            if (p.type == jefe::qt::FXParamType::Spacer ||
                p.type == jefe::qt::FXParamType::Newline) {
                continue;
            }

            // Group separator line when group changes.
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

            // Param row: label : value (type)
            const QString labelText = !p.label.empty()
                ? QString::fromStdString(p.label)
                : QString::fromStdString(p.name);

            auto* row = new QWidget(contentWidget_);
            row->setObjectName(
                QString("fxparams.fx%1.param.%2.row")
                    .arg(static_cast<int>(i))
                    .arg(QString::fromStdString(p.name)));
            auto* rowLay = new QHBoxLayout(row);
            rowLay->setContentsMargins(12, 0, 0, 0);
            rowLay->setSpacing(6);

            auto* nameLab = new QLabel(labelText + ":", row);
            nameLab->setMinimumWidth(120);
            nameLab->setObjectName(
                QString("fxparams.fx%1.param.%2.label")
                    .arg(static_cast<int>(i))
                    .arg(QString::fromStdString(p.name)));

            auto* valLab = new QLabel(formatValue(p), row);
            valLab->setStyleSheet("color: #ddd; font-family: monospace;");
            valLab->setObjectName(
                QString("fxparams.fx%1.param.%2.value")
                    .arg(static_cast<int>(i))
                    .arg(QString::fromStdString(p.name)));

            auto* typeLab = new QLabel(
                QString("<span style='color:#666'>(%1)</span>")
                    .arg(typeLabel(p.type)),
                row);
            typeLab->setTextFormat(Qt::RichText);

            rowLay->addWidget(nameLab);
            rowLay->addWidget(valLab, /*stretch*/ 1);
            rowLay->addWidget(typeLab);
            contentLayout_->addWidget(row);
            ++paramCount;
        }

        if (i + 1 < stack.size()) {
            contentLayout_->addWidget(makeSeparator(contentWidget_));
        }
    }

    contentLayout_->addStretch(1);
    status_->setText(QString("Plate %1 — %2 FX, %3 params (read-only)")
                         .arg(active)
                         .arg(stack.size())
                         .arg(paramCount));
}
