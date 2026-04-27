#include "TimelinePanel_qt.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

TimelineTracks_Qt::TimelineTracks_Qt(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TimelineTracks_Qt::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRect r = rect();
    p.fillRect(r, QColor(28, 28, 28));

    // Scrubber strip placeholder.
    p.fillRect(QRect(0, 0, r.width(), 22), QColor(40, 40, 40));
    p.setPen(QColor(180, 180, 180));
    p.drawText(8, 16, "Scrubber (placeholder)");

    // Four track lanes.
    const int laneTop = 28;
    const int laneHeight = (r.height() - laneTop) / 4;
    for (int i = 0; i < 4; ++i) {
        const int y = laneTop + i * laneHeight;
        const QRect lane(0, y, r.width(), laneHeight - 2);
        p.fillRect(lane, i % 2 ? QColor(36, 36, 36) : QColor(32, 32, 32));
        p.setPen(QColor(160, 160, 160));
        p.drawText(8, y + laneHeight / 2 + 4, QString("Track %1").arg(QChar('A' + i)));
    }
}

TimelinePanel_Qt::TimelinePanel_Qt(QWidget* parent) : QWidget(parent) {
    auto* transport = new QHBoxLayout();
    transport->setContentsMargins(4, 2, 4, 2);
    transport->setSpacing(2);

    auto makeButton = [this](const QString& text, const QString& tip) {
        auto* b = new QPushButton(text, this);
        b->setToolTip(tip);
        b->setFixedSize(26, 22);
        return b;
    };

    auto makeSmallLabel = [this](const QString& text) {
        auto* l = new QLabel(text, this);
        l->setStyleSheet("color: #888;");
        return l;
    };

    auto makeSpin = [this](int minVal, int maxVal, int width) {
        auto* s = new QSpinBox(this);
        s->setRange(minVal, maxVal);
        s->setFixedWidth(width);
        s->setAlignment(Qt::AlignRight);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        return s;
    };

    transport->addWidget(makeButton("⏮", "Rewind to start"));
    transport->addWidget(makeButton("◀", "Step back one frame"));
    transport->addWidget(makeButton("⏸", "Play / Pause"));
    transport->addWidget(makeButton("▶", "Step forward one frame"));
    transport->addWidget(makeButton("⏭", "Fast forward to end"));

    auto* loopMode = new QComboBox(this);
    loopMode->addItems({"Once", "Loop", "Bounce"});
    loopMode->setFixedWidth(74);
    transport->addSpacing(4);
    transport->addWidget(loopMode);

    transport->addSpacing(6);
    transport->addWidget(makeSmallLabel("F"));
    transport->addWidget(makeSpin(1, 99999, 56));

    transport->addSpacing(4);
    transport->addWidget(makeSmallLabel("In"));
    transport->addWidget(makeSpin(1, 99999, 50));

    transport->addWidget(makeSmallLabel("Out"));
    transport->addWidget(makeSpin(1, 99999, 50));

    transport->addStretch(1);

    transport->addWidget(makeSmallLabel("FPS"));
    auto* fps = new QDoubleSpinBox(this);
    fps->setRange(1.0, 120.0);
    fps->setDecimals(2);
    fps->setValue(24.0);
    fps->setFixedWidth(56);
    fps->setAlignment(Qt::AlignRight);
    fps->setButtonSymbols(QAbstractSpinBox::NoButtons);
    transport->addWidget(fps);

    auto* tracks = new TimelineTracks_Qt(this);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addLayout(transport);
    outer->addWidget(tracks, /*stretch*/ 1);

    setStyleSheet(
        "QLabel, QPushButton, QSpinBox, QDoubleSpinBox, QComboBox, "
        "QComboBox QAbstractItemView, QAbstractSpinBox { font-size: 10pt; }"
    );

    // Allow the timeline to shrink horizontally so it stops fighting the
    // plate dock's minimum width when the bottom strip is split.
    setMinimumWidth(360);
}
