// Adapted from Qt's flowlayout example (BSD 3-Clause, Qt Company).
// Reflows children left-to-right, wrapping to the next row when the
// available width is exceeded. Used by LoadWindowDialog_Qt so the four
// track strips lay out 2×2 at default width and 4×1 when narrow.
#pragma once

#include <QLayout>
#include <QList>
#include <QRect>
#include <QStyle>

class QLayoutItem;

class FlowLayout_Qt : public QLayout {
public:
    explicit FlowLayout_Qt(QWidget* parent, int margin = -1,
                           int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout_Qt(int margin = -1,
                           int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout_Qt() override;

    void addItem(QLayoutItem* item) override;
    int  horizontalSpacing() const;
    int  verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int  heightForWidth(int width) const override;
    int  count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;

private:
    int  doLayout(const QRect& rect, bool testOnly) const;
    int  smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem*> itemList_;
    int hSpace_;
    int vSpace_;
};
