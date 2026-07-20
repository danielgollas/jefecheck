// qticons.cpp — programmatic icon factory (JEF-19). See qticons.h.
#include "qticons.h"

#include <cmath>

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QRectF>

namespace jefe {
namespace qticons {

namespace {

constexpr qreal kDpr = 2.0;         // retina-crisp
constexpr int   kBtnSide = 24;      // matches the plate-card icon buttons
constexpr int   kIconPx  = 16;      // logical glyph size

// A round-capped/joined stroke pen in the given tint.
QPen stroke(const QColor& c, qreal w = 1.6) {
    QPen p(c, w);
    p.setCapStyle(Qt::RoundCap);
    p.setJoinStyle(Qt::RoundJoin);
    return p;
}

QPixmap render(const PaintFn& paint, int side, const QColor& color) {
    QPixmap pm(int(side * kDpr), int(side * kDpr));
    pm.setDevicePixelRatio(kDpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    paint(p, side, color);
    p.end();
    return pm;
}

// --- glyph painters (logical 16x16 box; usable area ~3..13) -----------------

void pAdd(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 1.9));
    p.drawLine(QPointF(s*0.16, s*0.5), QPointF(s*0.84, s*0.5));
    p.drawLine(QPointF(s*0.5, s*0.16), QPointF(s*0.5, s*0.84));
}

void pRemove(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 1.8));
    p.drawLine(QPointF(s*0.26, s*0.26), QPointF(s*0.74, s*0.74));
    p.drawLine(QPointF(s*0.74, s*0.26), QPointF(s*0.26, s*0.74));
}

void pUp(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 1.9));
    QPolygonF poly({QPointF(s*0.26, s*0.62), QPointF(s*0.5, s*0.34), QPointF(s*0.74, s*0.62)});
    p.drawPolyline(poly);
}

void pDown(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 1.9));
    QPolygonF poly({QPointF(s*0.26, s*0.4), QPointF(s*0.5, s*0.68), QPointF(s*0.74, s*0.4)});
    p.drawPolyline(poly);
}

void pChevron(QPainter& p, qreal s, const QColor& c, bool expanded) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    if (expanded) {  // ▾
        QPolygonF t({QPointF(s*0.32, s*0.42), QPointF(s*0.68, s*0.42), QPointF(s*0.5, s*0.66)});
        p.drawPolygon(t);
    } else {          // ▸
        QPolygonF t({QPointF(s*0.4, s*0.32), QPointF(s*0.64, s*0.5), QPointF(s*0.4, s*0.68)});
        p.drawPolygon(t);
    }
}

void pDragHandle(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 1.5));
    for (qreal y : {0.34, 0.5, 0.66})
        p.drawLine(QPointF(s*0.24, s*y), QPointF(s*0.76, s*y));
}

void pTrash(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 1.4));
    // lid + handle
    p.drawLine(QPointF(s*0.22, s*0.3), QPointF(s*0.78, s*0.3));
    p.drawLine(QPointF(s*0.4, s*0.3), QPointF(s*0.42, s*0.2));
    p.drawLine(QPointF(s*0.42, s*0.2), QPointF(s*0.58, s*0.2));
    p.drawLine(QPointF(s*0.58, s*0.2), QPointF(s*0.6, s*0.3));
    // can body
    QPainterPath body;
    body.moveTo(s*0.28, s*0.3);
    body.lineTo(s*0.33, s*0.8);
    body.lineTo(s*0.67, s*0.8);
    body.lineTo(s*0.72, s*0.3);
    p.drawPath(body);
    // vertical ribs
    for (qreal x : {0.42, 0.5, 0.58})
        p.drawLine(QPointF(s*x, s*0.4), QPointF(s*x, s*0.72));
}

void pPlay(QPainter& p, qreal s, const QColor& c) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    QPolygonF t({QPointF(s*0.32, s*0.24), QPointF(s*0.32, s*0.76), QPointF(s*0.78, s*0.5)});
    p.drawPolygon(t);
}

void pPause(QPainter& p, qreal s, const QColor& c) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(s*0.3, s*0.26, s*0.14, s*0.48), s*0.03, s*0.03);
    p.drawRoundedRect(QRectF(s*0.56, s*0.26, s*0.14, s*0.48), s*0.03, s*0.03);
}

void pStepBack(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 1.6));
    p.drawLine(QPointF(s*0.28, s*0.26), QPointF(s*0.28, s*0.74));
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    QPolygonF t({QPointF(s*0.74, s*0.26), QPointF(s*0.74, s*0.74), QPointF(s*0.36, s*0.5)});
    p.drawPolygon(t);
}

void pStepForward(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 1.6));
    p.drawLine(QPointF(s*0.72, s*0.26), QPointF(s*0.72, s*0.74));
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    QPolygonF t({QPointF(s*0.26, s*0.26), QPointF(s*0.26, s*0.74), QPointF(s*0.64, s*0.5)});
    p.drawPolygon(t);
}

void pRewind(QPainter& p, qreal s, const QColor& c) {  // ◀◀
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    QPolygonF a({QPointF(s*0.5, s*0.28), QPointF(s*0.5, s*0.72), QPointF(s*0.16, s*0.5)});
    QPolygonF b({QPointF(s*0.84, s*0.28), QPointF(s*0.84, s*0.72), QPointF(s*0.5, s*0.5)});
    p.drawPolygon(a);
    p.drawPolygon(b);
}

void pFastForward(QPainter& p, qreal s, const QColor& c) {  // ▶▶
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    QPolygonF a({QPointF(s*0.16, s*0.28), QPointF(s*0.16, s*0.72), QPointF(s*0.5, s*0.5)});
    QPolygonF b({QPointF(s*0.5, s*0.28), QPointF(s*0.5, s*0.72), QPointF(s*0.84, s*0.5)});
    p.drawPolygon(a);
    p.drawPolygon(b);
}

void pFilmstrip(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 1.3));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(s*0.16, s*0.26, s*0.68, s*0.48), s*0.05, s*0.05);
    // sprocket holes top & bottom
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    for (qreal x : {0.26, 0.42, 0.58, 0.74}) {
        p.drawRect(QRectF(s*(x-0.03), s*0.31, s*0.06, s*0.06));
        p.drawRect(QRectF(s*(x-0.03), s*0.63, s*0.06, s*0.06));
    }
}

void pRefresh(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 1.5));
    p.setBrush(Qt::NoBrush);
    QRectF r(s*0.24, s*0.24, s*0.52, s*0.52);
    // arc leaving a gap at the top-right for the arrowhead
    p.drawArc(r, 60 * 16, 280 * 16);
    // arrowhead at the arc's open end (top-right)
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    QPointF tip(s*0.76, s*0.34);
    QPolygonF head({tip, tip + QPointF(-s*0.14, -s*0.02), tip + QPointF(-s*0.02, s*0.14)});
    p.drawPolygon(head);
}

void pFolder(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 1.4));
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(s*0.18, s*0.72);
    path.lineTo(s*0.18, s*0.34);
    path.lineTo(s*0.42, s*0.34);
    path.lineTo(s*0.5, s*0.42);
    path.lineTo(s*0.82, s*0.42);
    path.lineTo(s*0.82, s*0.72);
    path.closeSubpath();
    p.drawPath(path);
}

void pAddFiles(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 1.4));
    p.setBrush(Qt::NoBrush);
    // document with a folded corner
    QPainterPath doc;
    doc.moveTo(s*0.24, s*0.16);
    doc.lineTo(s*0.56, s*0.16);
    doc.lineTo(s*0.7, s*0.3);
    doc.lineTo(s*0.7, s*0.84);
    doc.lineTo(s*0.24, s*0.84);
    doc.closeSubpath();
    p.drawPath(doc);
    p.drawLine(QPointF(s*0.56, s*0.16), QPointF(s*0.56, s*0.3));
    p.drawLine(QPointF(s*0.56, s*0.3), QPointF(s*0.7, s*0.3));
    // small plus
    p.setPen(stroke(c, 1.5));
    p.drawLine(QPointF(s*0.47, s*0.58), QPointF(s*0.47, s*0.74));
    p.drawLine(QPointF(s*0.39, s*0.66), QPointF(s*0.55, s*0.66));
}

void pSave(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 1.3));
    p.setBrush(Qt::NoBrush);
    // floppy outline with a clipped top-right corner
    QPainterPath body;
    body.moveTo(s*0.2, s*0.2);
    body.lineTo(s*0.66, s*0.2);
    body.lineTo(s*0.8, s*0.34);
    body.lineTo(s*0.8, s*0.8);
    body.lineTo(s*0.2, s*0.8);
    body.closeSubpath();
    p.drawPath(body);
    // label slot (bottom) and shutter (top)
    p.drawRect(QRectF(s*0.34, s*0.52, s*0.32, s*0.28));
    p.setBrush(c);
    p.setPen(Qt::NoPen);
    p.drawRect(QRectF(s*0.34, s*0.24, s*0.24, s*0.12));
}

void pCheck(QPainter& p, qreal s, const QColor& c) {
    p.setPen(stroke(c, 2.0));
    QPolygonF poly({QPointF(s*0.22, s*0.52), QPointF(s*0.42, s*0.72), QPointF(s*0.8, s*0.28)});
    p.drawPolyline(poly);
}

void pSend(QPainter& p, qreal s, const QColor& c) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    // simple paper plane
    QPolygonF plane({QPointF(s*0.16, s*0.5), QPointF(s*0.84, s*0.2),
                     QPointF(s*0.56, s*0.82), QPointF(s*0.46, s*0.56)});
    p.drawPolygon(plane);
}

void pRecent(QPainter& p, qreal s, const QColor& c) {
    // "history" clock: a full clock face with hands, plus a counter-clockwise
    // arrow arcing over the top-left (the standard "history" affordance). A
    // FULL circle (not one broken at the top) avoids reading as a power symbol.
    const QPointF ctr(s*0.5, s*0.56);
    const qreal R = s*0.24;
    auto onCircle = [&](qreal deg, qreal rad) {
        const qreal a = deg * M_PI / 180.0;
        return QPointF(ctr.x() + rad*std::cos(a), ctr.y() - rad*std::sin(a));
    };
    p.setPen(stroke(c, 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(ctr, R, R);                              // clock face
    // hands: to 1 o'clock (short) and ~4 o'clock — angled, never vertical.
    p.setPen(stroke(c, 1.3));
    p.drawLine(ctr, onCircle(70, R*0.62));
    p.drawLine(ctr, onCircle(-40, R*0.82));
    // counter-clockwise arrow arcing over the top, just outside the face.
    const qreal AR = R * 1.32;
    QRectF ar(ctr.x() - AR, ctr.y() - AR, 2*AR, 2*AR);
    p.setPen(stroke(c, 1.4));
    p.drawArc(ar, 55 * 16, 80 * 16);                      // 55°→135° over the top
    // arrowhead at the left end (~135°) pointing down (the CCW travel dir).
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    const QPointF tip = onCircle(135, AR);
    QPolygonF head({tip,
                    tip + QPointF(-s*0.02, s*0.13),
                    tip + QPointF(-s*0.12, s*0.0)});
    p.drawPolygon(head);
}

} // namespace

QIcon make(const PaintFn& paint, int side) {
    QIcon icon;
    icon.addPixmap(render(paint, side, QColor(0xE0, 0xE0, 0xE0)), QIcon::Normal, QIcon::Off);
    icon.addPixmap(render(paint, side, QColor(0x1A, 0x1A, 0x1A)), QIcon::Normal, QIcon::On);
    return icon;
}

QPushButton* makeIconButton(QWidget* parent, const QIcon& icon,
                            const QString& tooltip, const QString& accessibleName,
                            bool checkable, const QString& text) {
    auto* b = new QPushButton(parent);
    b->setIcon(icon);
    b->setIconSize(QSize(kIconPx, kIconPx));
    b->setCheckable(checkable);
    b->setToolTip(tooltip);
    b->setAccessibleName(accessibleName.isEmpty() ? tooltip : accessibleName);
    if (!text.isEmpty()) {
        b->setText(text);
        b->setStyleSheet("QPushButton { padding: 2px 8px; }");
    } else {
        b->setFixedSize(kBtnSide, kBtnSide);
        b->setStyleSheet("QPushButton { padding: 1px; }");
    }
    return b;
}

QIcon add()          { return make(pAdd); }
QIcon addFiles()     { return make(pAddFiles); }
QIcon remove()       { return make(pRemove); }
QIcon trash()        { return make(pTrash); }
QIcon up()           { return make(pUp); }
QIcon down()         { return make(pDown); }
QIcon chevron(bool e){ return make([e](QPainter& p, qreal s, const QColor& c){ pChevron(p, s, c, e); }); }
QIcon dragHandle()   { return make(pDragHandle); }
QIcon rewind()       { return make(pRewind); }
QIcon stepBack()     { return make(pStepBack); }
QIcon play()         { return make(pPlay); }
QIcon pause()        { return make(pPause); }
QIcon stepForward()  { return make(pStepForward); }
QIcon fastForward()  { return make(pFastForward); }
QIcon filmstrip()    { return make(pFilmstrip); }
QIcon refresh()      { return make(pRefresh); }
QIcon folder()       { return make(pFolder); }
QIcon save()         { return make(pSave); }
QIcon check()        { return make(pCheck); }
QIcon send()         { return make(pSend); }
QIcon recent()       { return make(pRecent); }

} // namespace qticons
} // namespace jefe
