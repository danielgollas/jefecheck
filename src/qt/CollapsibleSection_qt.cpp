#include "CollapsibleSection_qt.h"

#include <QToolButton>
#include <QVBoxLayout>

namespace {

// Self-contained dark styling so the section looks intentional in any context:
// a subtle header bar with a hover state and a thin divider under it, and a
// slightly inset content area. The triangle is Qt's native arrow (crisp at any
// DPI); the header spans the full width and left-aligns like Nuke/Maya groups.
const char* kSectionStyle = R"(
QToolButton#collapsible_header {
    background: #26262b;
    border: none;
    border-top: 1px solid #34343a;
    border-bottom: 1px solid #17171a;
    color: #b8b8be;
    font-size: 11px;
    font-weight: 600;
    padding: 5px 8px;
    text-align: left;
}
QToolButton#collapsible_header:hover  { background: #2e2e34; color: #d6d6db; }
QToolButton#collapsible_header:checked { color: #e4e4e8; }
QWidget#collapsible_content { background: transparent; }
)";

}  // namespace

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent)
    : QWidget(parent) {
    setStyleSheet(kSectionStyle);

    header_ = new QToolButton(this);
    header_->setObjectName("collapsible_header");
    header_->setText(title);
    header_->setCheckable(true);
    header_->setChecked(false);
    header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header_->setArrowType(Qt::RightArrow);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header_->setCursor(Qt::PointingHandCursor);
    header_->setFocusPolicy(Qt::NoFocus);

    content_ = new QWidget(this);
    content_->setObjectName("collapsible_content");
    contentLayout_ = new QVBoxLayout(content_);
    contentLayout_->setContentsMargins(4, 6, 4, 6);
    contentLayout_->setSpacing(0);
    content_->setVisible(false);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(header_);
    outer->addWidget(content_);

    connect(header_, &QToolButton::toggled, this, [this](bool on) {
        content_->setVisible(on);
        applyArrow(on);
        emit toggled(on);
    });
}

void CollapsibleSection::setContentWidget(QWidget* w) {
    if (contentWidget_) {
        contentLayout_->removeWidget(contentWidget_);
        contentWidget_->deleteLater();
    }
    contentWidget_ = w;
    if (w) {
        w->setParent(content_);
        contentLayout_->addWidget(w);
    }
}

void CollapsibleSection::setExpanded(bool expanded) {
    header_->setChecked(expanded);   // toggled() slot handles visibility + arrow
}

bool CollapsibleSection::isExpanded() const {
    return header_->isChecked();
}

void CollapsibleSection::setTitle(const QString& title) {
    header_->setText(title);
}

void CollapsibleSection::applyArrow(bool expanded) {
    header_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
}
