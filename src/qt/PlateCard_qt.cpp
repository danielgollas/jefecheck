#include "PlateCard_qt.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QDoubleSpinBox* makeSpin(QWidget* parent, double min, double max, double step,
                         double initial, int width) {
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setValue(initial);
    s->setDecimals(step < 1.0 ? 2 : 0);
    s->setFixedWidth(width);
    s->setAlignment(Qt::AlignRight);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    return s;
}

QLabel* makeInlineLabel(QWidget* parent, const QString& text) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet("color: #888;");
    return l;
}

QPushButton* makeToggle(QWidget* parent, const QString& text,
                        const QString& tip, int width = 32) {
    auto* b = new QPushButton(text, parent);
    b->setCheckable(true);
    b->setToolTip(tip);
    b->setFixedHeight(20);
    b->setMinimumWidth(width);
    return b;
}
}  // namespace

PlateCard_Qt::PlateCard_Qt(int id, QWidget* parent) : QFrame(parent) {
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);
    // 3 rows now (title gone, row 4 merged into row 1). PlateManager_Qt
    // uses a QGridLayout to lay cards out in 1-or-2 columns based on dock
    // width, so cards are free to stretch horizontally to fill their cell.
    setMinimumSize(280, 84);

    setStyleSheet(
        "QLabel, QPushButton, QSpinBox, QDoubleSpinBox, QComboBox, "
        "QComboBox QAbstractItemView, QAbstractSpinBox { font-size: 10pt; }"
    );

    // Plate ID (1-4) replaces the previous "Trk" label, doubling as the card
    // identifier now that the "Plate A/B/C/D" title is gone. The combo to its
    // right still chooses the source track (A/B/C/D).
    auto* plateId = new QLabel(QString::number(id + 1), this);
    plateId->setStyleSheet("font-weight: bold; font-size: 12pt; color: #ccc;");
    plateId->setFixedWidth(14);
    plateId->setAlignment(Qt::AlignCenter);

    auto* track = new QComboBox(this);
    track->addItems({"A", "B", "C", "D"});
    track->setCurrentIndex(id);
    track->setFixedWidth(40);

    auto* aspect = new QComboBox(this);
    aspect->setEditable(true);
    aspect->addItems({"original", "16:9", "4:3", "2.39:1", "2.35:1", "1.85:1", "1.37:1"});
    aspect->setMinimumWidth(60);

    auto* crop = makeToggle(this, "Crop", "Toggle crop bars (aspect-ratio letterbox)", 36);
    auto* flip = makeToggle(this, "Flip", "Flip vertically", 32);
    auto* flop = makeToggle(this, "Flop", "Flop horizontally", 32);

    auto* rgba = new QPushButton("RGB", this);
    rgba->setToolTip("Cycle RGBA channel display (shortcuts r/g/b/a)");
    rgba->setFixedHeight(20);
    rgba->setMinimumWidth(36);

    // Row 1 — identity + format toggles + channel: # / Track / Aspect / Crop / Flip / Flop / RGB
    auto* row1 = new QHBoxLayout();
    row1->setSpacing(4);
    row1->addWidget(plateId);
    row1->addWidget(track);
    row1->addWidget(aspect, 1);
    row1->addWidget(crop);
    row1->addWidget(flip);
    row1->addWidget(flop);
    row1->addWidget(rgba);

    // Row 2 — transform + LUT: Zoom / X / Y / R / LUT
    auto* zoom = makeSpin(this, 0.01,    99.99, 0.01, 1.0, 46);
    auto* panX = makeSpin(this, -9999.0, 9999.0, 1.0,  0.0, 50);
    auto* panY = makeSpin(this, -9999.0, 9999.0, 1.0,  0.0, 50);
    auto* rot  = makeSpin(this, -360.0,  360.0,  0.01, 0.0, 56);

    auto* lut = new QComboBox(this);
    lut->addItem("No LUT");
    lut->setMinimumWidth(70);

    auto* row2 = new QHBoxLayout();
    row2->setSpacing(2);
    row2->addWidget(makeInlineLabel(this, "Zoom"));
    row2->addWidget(zoom);
    row2->addSpacing(4);
    row2->addWidget(makeInlineLabel(this, "X"));
    row2->addWidget(panX);
    row2->addWidget(makeInlineLabel(this, "Y"));
    row2->addWidget(panY);
    row2->addSpacing(4);
    row2->addWidget(makeInlineLabel(this, "R"));
    row2->addWidget(rot);
    row2->addSpacing(4);
    row2->addWidget(makeInlineLabel(this, "LUT"));
    row2->addWidget(lut, 1);

    // Row 3 — color: γ / Ex / Cn / Br / St
    auto* gamma      = makeSpin(this, 0.01,   99.99, 0.01, 1.0, 44);
    auto* exposure   = makeSpin(this, -99.99, 99.99, 0.01, 0.0, 48);
    auto* contrast   = makeSpin(this, 0.01,   99.99, 0.01, 1.0, 44);
    auto* brightness = makeSpin(this, -99.99, 99.99, 0.01, 0.0, 48);
    auto* saturation = makeSpin(this, 0.0,    99.99, 0.01, 1.0, 44);

    auto* row3 = new QHBoxLayout();
    row3->setSpacing(2);
    row3->addWidget(makeInlineLabel(this, "γ"));
    row3->addWidget(gamma);
    row3->addWidget(makeInlineLabel(this, "Ex"));
    row3->addWidget(exposure);
    row3->addWidget(makeInlineLabel(this, "Cn"));
    row3->addWidget(contrast);
    row3->addWidget(makeInlineLabel(this, "Br"));
    row3->addWidget(brightness);
    row3->addWidget(makeInlineLabel(this, "St"));
    row3->addWidget(saturation);
    row3->addStretch(1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 2, 4, 2);
    outer->setSpacing(2);
    outer->addLayout(row1);
    outer->addLayout(row2);
    outer->addLayout(row3);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

