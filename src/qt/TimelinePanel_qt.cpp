#include "TimelinePanel_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QComboBox>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kScrubberPad = 4;  // horizontal padding inside the lane area

// Shared pixel<->frame mapping used by both the scrubber and the track
// rows so their frames line up vertically. `width` is the widget width;
// the playable strip [from, to] sits inside `kScrubberPad` on each side.
int frameFromXMapped(int x, int width, int from, int to) {
    const int span = to - from;
    if (span <= 0) return from;
    const int usable = std::max(width - 2 * kScrubberPad, 1);
    const int rel = std::clamp(x - kScrubberPad, 0, usable);
    return from + (rel * span + usable / 2) / usable;
}

int xFromFrameMapped(int frame, int width, int from, int to) {
    const int span = to - from;
    if (span <= 0) return kScrubberPad;
    const int usable = std::max(width - 2 * kScrubberPad, 1);
    const int rel = std::clamp(frame - from, 0, span);
    return kScrubberPad + (rel * usable) / span;
}
}  // namespace

// ---- TimelineScrubber_Qt ----

TimelineScrubber_Qt::TimelineScrubber_Qt(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(22);
    setMaximumHeight(28);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(false);
    setCursor(Qt::PointingHandCursor);
    setObjectName("timeline.scrubber");
    setAccessibleName("Timeline scrubber");
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
    return frameFromXMapped(x, width(), from_, to_);
}

int TimelineScrubber_Qt::xFromFrame(int frame) const {
    return xFromFrameMapped(frame, width(), from_, to_);
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
    setObjectName("timeline.tracks");
    setAccessibleName("Timeline tracks");
    setAcceptDrops(true);
    setCursor(Qt::SizeHorCursor);  // hint that horizontal drag = offset
}

int TimelineTracks_Qt::laneHeight() const {
    return std::max(height() / 4, 16);
}

int TimelineTracks_Qt::laneTopY(int track) const {
    return track * laneHeight();
}

void TimelineTracks_Qt::refresh() {
    const int from = jefe::qt::getFromFrame();
    const int to   = std::max(jefe::qt::getToFrame(), from);
    const int cur  = jefe::qt::getCurrentFrame();

    std::array<jefe::qt::TrackTimelineState, 4> next{};
    for (int i = 0; i < 4; ++i) next[i] = jefe::qt::getTrackTimelineState(i);

    // Cache-compare: skip the repaint when nothing a row draws has
    // changed (matches the AppKit-cascade-avoidance pattern elsewhere).
    bool changed = (from != from_ || to != to_ || cur != current_);
    for (int i = 0; i < 4 && !changed; ++i) {
        const auto& a = states_[i];
        const auto& b = next[i];
        changed = a.present != b.present || a.rangeStart != b.rangeStart
               || a.rangeEnd != b.rangeEnd || a.loadedCount != b.loadedCount
               || a.offset != b.offset || a.label != b.label;
    }
    if (!changed) return;

    from_ = from; to_ = to; current_ = cur; states_ = next;
    update();
}

void TimelineTracks_Qt::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRect r = rect();
    p.fillRect(r, QColor(28, 28, 28));

    const int lh = laneHeight();
    for (int i = 0; i < 4; ++i) {
        const int y = laneTopY(i);
        const QRect lane(0, y, r.width(), lh - 2);
        p.fillRect(lane, i % 2 ? QColor(36, 36, 36) : QColor(32, 32, 32));

        const auto& s = states_[i];
        const QChar letter('A' + i);

        if (!s.present) {
            // Empty lane: faint placeholder, doubles as drop / alt-click target.
            p.setPen(QColor(90, 90, 90));
            p.drawText(8, y + lh / 2 + 4,
                       QString("%1  (empty — drop or alt-click to load)")
                           .arg(letter));
            continue;
        }

        // Sequence bar across [rangeStart, rangeEnd] in dark gray.
        const int xStart = xFromFrameMapped(s.rangeStart, width(), from_, to_);
        const int xEnd   = xFromFrameMapped(s.rangeEnd,   width(), from_, to_);
        const int barTop = y + 3;
        const int barH   = lh - 8;
        const QRect bar(xStart, barTop, std::max(xEnd - xStart, 1), barH);
        p.fillRect(bar, QColor(85, 85, 85));

        // Loaded portion (lighter), starting at firstLoadedFrame.
        if (s.loadedCount > 0) {
            const int loadedEndFrame = s.firstLoadedFrame + s.loadedCount - 1;
            const int xL0 = xFromFrameMapped(s.firstLoadedFrame, width(), from_, to_);
            const int xL1 = xFromFrameMapped(loadedEndFrame,     width(), from_, to_);
            const QRect loaded(xL0, barTop, std::max(xL1 - xL0, 1), barH);
            p.fillRect(loaded, QColor(160, 160, 160));
        }

        // Label (track letter + filename) over the bar.
        p.setPen(QColor(230, 230, 230));
        p.drawText(xStart + 4, y + lh / 2 + 4,
                   QString("%1  %2").arg(letter).arg(QString::fromStdString(s.label)));
    }

    // Read-only playhead across all lanes (alignment with the scrubber;
    // not draggable — drag is reassigned to offset).
    const int xCur = xFromFrameMapped(current_, width(), from_, to_);
    p.setPen(QPen(QColor(0xd4, 0x77, 0x1e), 1));
    p.drawLine(xCur, 0, xCur, r.height());
}

int TimelineTracks_Qt::trackAtY(int y) const {
    const int t = y / std::max(laneHeight(), 1);
    return std::clamp(t, 0, 3);
}

double TimelineTracks_Qt::pxPerFrame() const {
    const int span = std::max(to_ - from_, 1);
    const int usable = std::max(width() - 2 * kScrubberPad, 1);
    return std::max((double)usable / span, 0.001);
}

void TimelineTracks_Qt::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const int track = trackAtY((int)e->position().y());

    if (e->modifiers() & Qt::AltModifier) {
        // Alt+click: load the track's sequence from the clicked frame on.
        const int frame = frameFromXMapped((int)e->position().x(),
                                           width(), from_, to_);
        jefe::qt::startLoadingTrackAt(track, frame);
        return;
    }

    // Begin a drag-to-offset gesture on this track.
    dragTrack_ = track;
    dragAccumPx_ = 0.0;
    dragPrevX_ = (int)e->position().x();
}

void TimelineTracks_Qt::mouseMoveEvent(QMouseEvent* e) {
    if (dragTrack_ < 0 || !(e->buttons() & Qt::LeftButton)) return;
    const int x = (int)e->position().x();
    dragAccumPx_ += (x - dragPrevX_);
    dragPrevX_ = x;

    const double frameW = pxPerFrame();
    while (dragAccumPx_ >= frameW || dragAccumPx_ <= -frameW) {
        const int step = dragAccumPx_ > 0 ? 1 : -1;
        jefe::qt::setTrackOffset(dragTrack_,
                                 jefe::qt::getTrackOffset(dragTrack_) + step);
        dragAccumPx_ -= step * frameW;
    }
    refresh();
}

void TimelineTracks_Qt::mouseReleaseEvent(QMouseEvent* e) {
    Q_UNUSED(e);
    dragTrack_ = -1;
    dragAccumPx_ = 0.0;
}

void TimelineTracks_Qt::contextMenuEvent(QContextMenuEvent* e) {
    const int track = trackAtY(e->y());
    QMenu menu(this);

    QAction* hold = menu.addAction("Hold last frame");
    hold->setCheckable(true);
    hold->setChecked(jefe::qt::getTrackHoldMode(track));

    QAction* setOff = menu.addAction("Set offset…");

    QAction* chosen = menu.exec(e->globalPos());
    if (chosen == hold) {
        jefe::qt::setTrackHoldMode(track, hold->isChecked());
        refresh();
    } else if (chosen == setOff) {
        bool ok = false;
        const int cur = jefe::qt::getTrackOffset(track);
        const int v = QInputDialog::getInt(this, "Track offset",
                          QString("Offset for track %1 (frames):")
                              .arg(QChar('A' + track)),
                          cur, -100000, 100000, 1, &ok);
        if (ok) {
            jefe::qt::setTrackOffset(track, v);
            refresh();
        }
    }
}

void TimelineTracks_Qt::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void TimelineTracks_Qt::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasUrls()) return;
    const int track = trackAtY((int)e->position().y());
    const QList<QUrl> urls = e->mimeData()->urls();
    if (urls.isEmpty()) return;
    const QString path = urls.first().toLocalFile();
    if (path.isEmpty()) return;
    // Reuse the standard load path, targeting this row's sequence.
    jefe::qt::loadFileIntoPlate(path.toStdString(), track, true, 1.0f);
    e->acceptProposedAction();
    refresh();
}

// ---- TimelinePanel_Qt ----

TimelinePanel_Qt::TimelinePanel_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("timeline.panel");
    setAccessibleName("Timeline and transport");

    auto* transport = new QHBoxLayout();
    transport->setContentsMargins(4, 2, 4, 2);
    transport->setSpacing(2);

    auto makeButton = [this](const QString& text, const QString& tip,
                              const QString& objectName,
                              const QString& accessibleName) {
        auto* b = new QPushButton(text, this);
        b->setToolTip(tip);
        b->setFixedSize(26, 22);
        b->setObjectName(objectName);
        b->setAccessibleName(accessibleName);
        return b;
    };

    auto makeSmallLabel = [this](const QString& text) {
        auto* l = new QLabel(text, this);
        l->setStyleSheet("color: #888;");
        return l;
    };

    auto makeSpin = [this](int minVal, int maxVal, int width,
                            const QString& objectName,
                            const QString& accessibleName) {
        auto* s = new QSpinBox(this);
        s->setRange(minVal, maxVal);
        s->setFixedWidth(width);
        s->setAlignment(Qt::AlignRight);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setObjectName(objectName);
        s->setAccessibleName(accessibleName);
        return s;
    };

    rewBtn_      = makeButton("⏮", "Rewind to start",
                              "transport.rewind.button", "Rewind to start");
    stepBackBtn_ = makeButton("◀", "Step back one frame",
                              "transport.stepback.button", "Step back");
    playBtn_     = makeButton("▶", "Play / Pause",
                              "transport.play.button", "Play / Pause");
    stepFwdBtn_  = makeButton("▶|", "Step forward one frame",
                              "transport.stepforward.button", "Step forward");
    ffwdBtn_     = makeButton("⏭", "Fast forward to end",
                              "transport.fastforward.button", "Fast forward");

    transport->addWidget(rewBtn_);
    transport->addWidget(stepBackBtn_);
    transport->addWidget(playBtn_);
    transport->addWidget(stepFwdBtn_);
    transport->addWidget(ffwdBtn_);

    loopMode_ = new QComboBox(this);
    loopMode_->addItems({"Once", "Loop", "Bounce"});
    loopMode_->setFixedWidth(74);
    loopMode_->setObjectName("transport.loop.combo");
    loopMode_->setAccessibleName("Loop mode");
    transport->addSpacing(4);
    transport->addWidget(loopMode_);

    transport->addSpacing(6);
    transport->addWidget(makeSmallLabel("F"));
    frameSpin_ = makeSpin(1, 99999, 56,
                          "transport.frame.spin", "Current frame");
    transport->addWidget(frameSpin_);

    transport->addSpacing(4);
    transport->addWidget(makeSmallLabel("In"));
    inSpin_ = makeSpin(1, 99999, 50,
                       "transport.in.spin", "In point");
    transport->addWidget(inSpin_);

    transport->addWidget(makeSmallLabel("Out"));
    outSpin_ = makeSpin(1, 99999, 50,
                        "transport.out.spin", "Out point");
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
    fpsSpin_->setObjectName("transport.fps.spin");
    fpsSpin_->setAccessibleName("Target FPS");
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
    // Track rows update independently of the transport values (their
    // loaded fill grows during decode even while frame/range hold), and
    // refresh() is itself cache-gated, so do it before the fast-path.
    tracks_->refresh();

    const int from = jefe::qt::getFromFrame();
    const int to   = jefe::qt::getToFrame();
    const int cur  = jefe::qt::getCurrentFrame();
    const int in   = jefe::qt::getInPoint();
    const int out  = jefe::qt::getOutPoint();
    const int loop = jefe::qt::getLoopMode();
    const float fps = jefe::qt::getTargetFPS();
    const bool playing = jefe::qt::isPlaying();

    // Fast-path: nothing changed since the last tick, so every widget
    // setter below would be a value-identical no-op that still walks
    // QAccessible / AppKit / AttributeGraph. Skip the whole pass.
    if (lastCacheValid_
        && from == lastFrom_ && to == lastTo_ && cur == lastCur_
        && in == lastIn_ && out == lastOut_ && loop == lastLoop_
        && fps == lastFps_ && playing == lastPlaying_) {
        return;
    }

    {
        const QSignalBlocker bFrame(frameSpin_);
        const QSignalBlocker bIn(inSpin_);
        const QSignalBlocker bOut(outSpin_);
        const QSignalBlocker bFps(fpsSpin_);
        const QSignalBlocker bLoop(loopMode_);

        // Range-bound the spinboxes to the current playback range so
        // typing past the end clamps cleanly. Min always 1 to match
        // the FLTK defaults. Only re-set range when from/to actually
        // changed — setRange triggers AX notifications even if the
        // bounds match.
        const int hi = std::max(to, 1);
        if (from != lastFrom_ || to != lastTo_) {
            frameSpin_->setRange(std::max(from, 1), hi);
            inSpin_->setRange(std::max(from, 1), hi);
            outSpin_->setRange(std::max(from, 1), hi);
        }

        if (cur != lastCur_) frameSpin_->setValue(cur);
        if (in != lastIn_)   inSpin_->setValue(in);
        if (out != lastOut_) outSpin_->setValue(out);
        if (fps > 0.0f && fps != lastFps_) fpsSpin_->setValue(fps);
        if (loop != lastLoop_ && loop >= 0 && loop < loopMode_->count()) {
            loopMode_->setCurrentIndex(loop);
        }
    }

    if (from != lastFrom_ || to != lastTo_) {
        scrubber_->setRange(from, to);
    }
    if (in != lastIn_ || out != lastOut_) {
        scrubber_->setInOut(in, out);
    }
    if (cur != lastCur_) scrubber_->setCurrentFrame(cur);
    if (playing != lastPlaying_) playBtn_->setText(playing ? "⏸" : "▶");

    lastFrom_ = from; lastTo_ = to; lastCur_ = cur;
    lastIn_ = in; lastOut_ = out; lastLoop_ = loop;
    lastFps_ = fps; lastPlaying_ = playing;
    lastCacheValid_ = true;
}
