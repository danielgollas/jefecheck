#include "FlowLayout_qt.h"

#include <QWidget>

FlowLayout_Qt::FlowLayout_Qt(QWidget* parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), hSpace_(hSpacing), vSpace_(vSpacing) {
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout_Qt::FlowLayout_Qt(int margin, int hSpacing, int vSpacing)
    : hSpace_(hSpacing), vSpace_(vSpacing) {
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout_Qt::~FlowLayout_Qt() {
    QLayoutItem* item;
    while ((item = takeAt(0))) delete item;
}

void FlowLayout_Qt::addItem(QLayoutItem* item) { itemList_.append(item); }
int  FlowLayout_Qt::count() const               { return itemList_.size(); }
QLayoutItem* FlowLayout_Qt::itemAt(int i) const { return itemList_.value(i); }
QLayoutItem* FlowLayout_Qt::takeAt(int i) {
    return (i >= 0 && i < itemList_.size()) ? itemList_.takeAt(i) : nullptr;
}

int FlowLayout_Qt::horizontalSpacing() const {
    return hSpace_ >= 0 ? hSpace_ : smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}
int FlowLayout_Qt::verticalSpacing() const {
    return vSpace_ >= 0 ? vSpace_ : smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

Qt::Orientations FlowLayout_Qt::expandingDirections() const { return {}; }
bool FlowLayout_Qt::hasHeightForWidth() const               { return true; }
int  FlowLayout_Qt::heightForWidth(int width) const {
    return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout_Qt::setGeometry(const QRect& rect) {
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout_Qt::sizeHint() const   { return minimumSize(); }
QSize FlowLayout_Qt::minimumSize() const {
    QSize size;
    for (auto* item : itemList_) size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    size += QSize(m.left() + m.right(), m.top() + m.bottom());
    return size;
}

int FlowLayout_Qt::doLayout(const QRect& rect, bool testOnly) const {
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    const QRect effective = rect.adjusted(+left, +top, -right, -bottom);
    int x = effective.x();
    int y = effective.y();
    int lineHeight = 0;
    for (auto* item : itemList_) {
        QWidget* w = item->widget();
        int hSp = horizontalSpacing();
        if (hSp == -1 && w)
            hSp = w->style()->layoutSpacing(QSizePolicy::PushButton,
                                            QSizePolicy::PushButton,
                                            Qt::Horizontal);
        int vSp = verticalSpacing();
        if (vSp == -1 && w)
            vSp = w->style()->layoutSpacing(QSizePolicy::PushButton,
                                            QSizePolicy::PushButton,
                                            Qt::Vertical);
        int next = x + item->sizeHint().width() + hSp;
        if (next - hSp > effective.right() && lineHeight > 0) {
            x = effective.x();
            y = y + lineHeight + vSp;
            next = x + item->sizeHint().width() + hSp;
            lineHeight = 0;
        }
        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
        x = next;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
    }
    return y + lineHeight - rect.y() + bottom;
}

int FlowLayout_Qt::smartSpacing(QStyle::PixelMetric pm) const {
    QObject* p = parent();
    if (!p) return -1;
    if (p->isWidgetType()) return static_cast<QWidget*>(p)->style()->pixelMetric(pm, nullptr, static_cast<QWidget*>(p));
    return static_cast<QLayout*>(p)->spacing();
}
