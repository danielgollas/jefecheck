#include "PathHighlightLineEdit_qt.h"

#include <QFontMetrics>
#include <QPainter>
#include <QStyleOptionFrame>

void PathHighlightLineEdit_Qt::setHighlightRange(int start, int length) {
    highlightStart_ = start;
    highlightLength_ = length;
    update();
}

void PathHighlightLineEdit_Qt::clearHighlight() {
    highlightStart_ = -1;
    highlightLength_ = 0;
    update();
}

void PathHighlightLineEdit_Qt::paintEvent(QPaintEvent* e) {
    QLineEdit::paintEvent(e);

    if (highlightLength_ <= 0 || highlightStart_ < 0) return;
    // We only draw the highlight when the full literal text is showing —
    // the elide path swaps to a middle-elided string whose character
    // indices don't map back to our cached digit-range. The bounds
    // check below catches the elided case (elided text is shorter than
    // the literal, so the cached digit range may exceed it). Focus
    // state is irrelevant: the user sees the highlight precisely when
    // they're looking at the literal filename.
    const QString full = text();
    if (highlightStart_ + highlightLength_ > full.length()) return;

    // Get the text content rect from the style — accounts for frame and
    // margins exactly like QLineEdit's own paint does.
    QStyleOptionFrame opt;
    initStyleOption(&opt);
    QRect contentRect = style()->subElementRect(QStyle::SE_LineEditContents, &opt, this);
    contentRect.adjust(textMargins().left(), textMargins().top(),
                       -textMargins().right(), -textMargins().bottom());

    const QFontMetrics fm(font());
    const QString prefix = full.left(highlightStart_);
    const QString digits = full.mid(highlightStart_, highlightLength_);

    const int prefixW = fm.horizontalAdvance(prefix);
    const int digitsW = fm.horizontalAdvance(digits);
    const int textH   = fm.height();

    // QLineEdit adds a small inner left padding (typically ~2px) before
    // the first glyph. This matches QLineEditPrivate::horizontalMargin.
    const int innerPad = 2;
    const int x = contentRect.x() + innerPad + prefixW;
    const int y = contentRect.y() + (contentRect.height() - textH) / 2;
    const QRect highlightRect(x, y, digitsW, textH);

    QPainter p(this);
    // Soft yellow underlay. Alpha keeps the rendered glyph readable on
    // top of the fill, and the warm hue distinguishes the seq digits
    // from selection highlight (which uses the palette accent).
    p.fillRect(highlightRect, QColor(255, 200, 0, 90));
}
