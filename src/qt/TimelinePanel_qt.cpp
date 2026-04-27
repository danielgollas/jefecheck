#include "TimelinePanel_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kScrubberPad = 4;  // horizontal padding inside the scrubber
}  // namespace

// ---- TimelineScrubber_Qt ----

TimelineScrubber_Qt::TimelineScrubber_Qt(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(22);
    setMaximumHeight(28);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(false);
    setCursor(Qt::PointingHandCursor);
}

void TimelineScrubber_Qt::setRange(int from, int to) {
    if (from == from_ && to == to_) return;
    from_ = from;
    to_ = std::max(to, from);
    update();
}

void TimelineScrubber_Qt::setInOut(int in, int out) {
    in_ = in;
    out_ = out;
    update();
}

void TimelineScrubber_Qt::setCurrentFrame(int frame) {
    if (frame == current_) return;
    current_ = frame;
    update();
}

int TimelineScrubber_Qt::frameFromX(int x) const {
    const int span = to_ - from_;
    if (span <= 0) return from_;
    const int usable = std::max(width() - 2 * kScrubberPad, 1);
    const int rel = std::clamp(x - kScrubberPad, 0, usable);
    return from_ + (rel * span + usable / 2) / usable;
}

int TimelineScrubber_Qt::xFromFrame(int frame) const {
    const int span = to_ - from_;
    if (span <= 0) return kScrubberPad;
    const int usable = std::max(width() - 2 * kScrubberPad, 1);
    const int rel = std::clamp(frame - from_, 0, span);
    return kScrubberPad + (rel * usable) / span;
}

void TimelineScrubber_Qt::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRect r = rect();
    p.fillRect(r, QColor(28, 28, 28));

    // Out-of-range area on the sides is just the dark fill. The
    // playable strip [from..to] sits inside kScrubberPad on each side.
    const int strip_y = r.height() / 2 - 4;
    const int strip_h = 8;
    const int x_from = xFromFrame(from_);
    const int x_to   = xFromFrame(to_);
    p.fillRect(QRect(x_from, strip_y, x_to - x_from, strip_h),
               QColor(48, 48, 48));

    // In/out highlight — slightly lighter so the user can see what
    // they'll actually play through. Skipped if in/out collapse to a
    // point or invert.
    if (out_ > in_) {
        const int x_in  = xFromFrame(in_);
        const int x_out = xFromFrame(out_);
        p.fillRect(QRect(x_in, strip_y, x_out - x_in, strip_h),
                   QColor(70, 70, 70));
    }

    // Playhead — vertical orange line. Matches the dark-VFX accent.
    const int x_cur = xFromFrame(current_);
    p.setPen(QPen(QColor(0xd4, 0x77, 0x1e), 2));
    p.drawLine(x_cur, 0, x_cur, r.height());
}

void TimelineScrubber_Qt::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        emit seek(frameFromX(e->position().x()));
    }
}

void TimelineScrubber_Qt::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) {
        emit seek(frameFromX(e->position().x()));
    }
}

// ---- TimelineTracks_Qt ----

TimelineTracks_Qt::TimelineTracks_Qt(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(96);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TimelineTracks_Qt::setTimelineRange(int from, int to) {
    from_ = from;
    to_ = std::max(to, from);
    update();
}

void TimelineTracks_Qt::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRect r = rect();
    p.fillRect(r, QColor(28, 28, 28));

    // Four track lanes. Per-track loaded ranges aren't plumbed yet —
    // future work bumps them out via a getTrackRange(i) bridge call.
    const int laneHeight = std::max(r.height() / 4, 16);
    for (int i = 0; i < 4; ++i) {
        const int y = i * laneHeight;
        const QRect lane(0, y, r.width(), laneHeight - 2);
        p.fillRect(lane, i % 2 ? QColor(36, 36, 36) : QColor(32, 32, 32));
        p.setPen(QColor(160, 160, 160));
        p.drawText(8, y + laneHeight / 2 + 4,
                   QString("Track %1").arg(QChar('A' + i)));
    }
    (void)from_;
    (void)to_;
}

// ---- TimelinePanel_Qt ----

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

    rewBtn_      = makeButton("⏮", "Rewind to start");
    stepBackBtn_ = makeButton("◀", "Step back one frame");
    playBtn_     = makeButton("▶", "Play / Pause");
    stepFwdBtn_  = makeButton("▶|", "Step forward one frame");
    ffwdBtn_     = makeButton("⏭", "Fast forward to end");

    transport->addWidget(rewBtn_);
    transport->addWidget(stepBackBtn_);
    transport->addWidget(playBtn_);
    transport->addWidget(stepFwdBtn_);
    transport->addWidget(ffwdBtn_);

    loopMode_ = new QComboBox(this);
    loopMode_->addItems({"Once", "Loop", "Bounce"});
    loopMode_->setFixedWidth(74);
    transport->addSpacing(4);
    transport->addWidget(loopMode_);

    transport->addSpacing(6);
    transport->addWidget(makeSmallLabel("F"));
    frameSpin_ = makeSpin(1, 99999, 56);
    transport->addWidget(frameSpin_);

    transport->addSpacing(4);
    transport->addWidget(makeSmallLabel("In"));
    inSpin_ = makeSpin(1, 99999, 50);
    transport->addWidget(inSpin_);

    transport->addWidget(makeSmallLabel("Out"));
    outSpin_ = makeSpin(1, 99999, 50);
    transport->addWidget(outSpin_);

    transport->addStretch(1);

    transport->addWidget(makeSmallLabel("FPS"));
    fpsSpin_ = new QDoubleSpinBox(this);
    fpsSpin_->setRange(1.0, 120.0);
    fpsSpin_->setDecimals(2);
    fpsSpin_->setValue(24.0);
    fpsSpin_->setFixedWidth(56);
    fpsSpin_->setAlignment(Qt::AlignRight);
    fpsSpin_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    transport->addWidget(fpsSpin_);

    scrubber_ = new TimelineScrubber_Qt(this);
    tracks_   = new TimelineTracks_Qt(this);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addLayout(transport);
    outer->addWidget(scrubber_);
    outer->addWidget(tracks_, /*stretch*/ 1);

    setStyleSheet(
        "QLabel, QPushButton, QSpinBox, QDoubleSpinBox, QComboBox, "
        "QComboBox QAbstractItemView, QAbstractSpinBox { font-size: 10pt; }"
    );

    setMinimumWidth(360);

    // ---- Wire widgets → playbackManager via the bridge ----
    connect(rewBtn_,      &QPushButton::clicked, this, []() { jefe::qt::rewindPlayback(); });
    connect(ffwdBtn_,     &QPushButton::clicked, this, []() { jefe::qt::fastFwdPlayback(); });
    connect(playBtn_,     &QPushButton::clicked, this, []() { jefe::qt::togglePlayFwd(); });
    connect(stepBackBtn_, &QPushButton::clicked, this, []() {
        // Stepping while playing pauses (per gfcPlaybackManager); the
        // refresh tick will pick up the new state on the next pulse.
        jefe::qt::stepFrame(-1);
    });
    connect(stepFwdBtn_,  &QPushButton::clicked, this, []() {
        jefe::qt::stepFrame(+1);
    });

    connect(loopMode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [](int idx) { jefe::qt::setLoopMode(idx); });

    connect(frameSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [](int v) { jefe::qt::seekToFrame(v); });
    connect(inSpin_,    QOverload<int>::of(&QSpinBox::valueChanged),
            this, [](int v) { jefe::qt::setInPoint(v); });
    connect(outSpin_,   QOverload<int>::of(&QSpinBox::valueChanged),
            this, [](int v) { jefe::qt::setOutPoint(v); });

    connect(fpsSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [](double v) { jefe::qt::setTargetFPS((float)v); });

    connect(scrubber_, &TimelineScrubber_Qt::seek,
            this, [](int v) { jefe::qt::seekToFrame(v); });

    refreshFromPlayback();
}

void TimelinePanel_Qt::refreshFromPlayback() {
    const int from = jefe::qt::getFromFrame();
    const int to   = jefe::qt::getToFrame();
    const int cur  = jefe::qt::getCurrentFrame();
    const int in   = jefe::qt::getInPoint();
    const int out  = jefe::qt::getOutPoint();
    const int loop = jefe::qt::getLoopMode();
    const float fps = jefe::qt::getTargetFPS();
    const bool playing = jefe::qt::isPlaying();

    {
        const QSignalBlocker bFrame(frameSpin_);
        const QSignalBlocker bIn(inSpin_);
        const QSignalBlocker bOut(outSpin_);
        const QSignalBlocker bFps(fpsSpin_);
        const QSignalBlocker bLoop(loopMode_);

        // Range-bound the spinboxes to the current playback range so
        // typing past the end clamps cleanly. Min always 1 to match
        // the FLTK defaults.
        const int hi = std::max(to, 1);
        frameSpin_->setRange(std::max(from, 1), hi);
        inSpin_->setRange(std::max(from, 1), hi);
        outSpin_->setRange(std::max(from, 1), hi);

        frameSpin_->setValue(cur);
        inSpin_->setValue(in);
        outSpin_->setValue(out);
        if (fps > 0.0f) fpsSpin_->setValue(fps);
        if (loop >= 0 && loop < loopMode_->count()) {
            loopMode_->setCurrentIndex(loop);
        }
    }

    scrubber_->setRange(from, to);
    scrubber_->setInOut(in, out);
    scrubber_->setCurrentFrame(cur);
    tracks_->setTimelineRange(from, to);

    playBtn_->setText(playing ? "⏸" : "▶");
}
