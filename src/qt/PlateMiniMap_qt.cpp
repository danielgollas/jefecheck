// SPDX-License-Identifier: GPL-2.0-or-later
#include "PlateMiniMap_qt.h"

#include "../gfcPlateLayout.h"

#include <QColor>
#include <QMouseEvent>
#include <QPainter>

namespace {
// Existing theme colours: the border grey and the accent.
const QColor kInactive(0x3a, 0x3a, 0x3a);
const QColor kActive(0xd4, 0x77, 0x1e);
}

PlateMiniMap_Qt::PlateMiniMap_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("platemanager.minimap");
    setAccessibleName("Active plate map");
    setFixedSize(44, 24);
    setCursor(Qt::PointingHandCursor);
}

void PlateMiniMap_Qt::setState(int framingMode, int activePlate) {
    if (framingMode == framing_ && activePlate == active_) return;
    framing_ = framingMode;
    if (activePlate != active_) {
        active_ = activePlate;
        setToolTip(QString("Plate %1 is active").arg(active_ + 1));
    }
    update();
}

void PlateMiniMap_Qt::paintEvent(QPaintEvent*) {
    if (framing_ < 0) return;
    QPainter p(this);
    for (const auto& c : jefe::platelayout::cellsFor(framing_, width(), height()))
        p.fillRect(QRect(c.x + 1, c.y + 1, c.w - 2, c.h - 2), c.plate == active_ ? kActive : kInactive);
}

void PlateMiniMap_Qt::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || framing_ < 0) { QWidget::mousePressEvent(e); return; }
    const QPoint at = e->position().toPoint();
    const int plate = jefe::platelayout::plateAtPoint(framing_, at.x(), at.y(), width(), height());
    if (plate >= 0) emit plateClicked(plate);
    e->accept();
}
