#include "AspectCropCombo_qt.h"

#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QScreen>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
// How long after the popup dismisses we treat a button click as a "close",
// not a fresh "open". Qt::Popup eats the press that dismisses it as an
// outside-click; the subsequent click that reaches the button must NOT
// reopen the popup, or it flickers shut-then-open. 150ms comfortably covers
// the same physical click without swallowing a deliberate second click.
constexpr qint64 kReopenGuardMs = 150;

// Build the small crop-marks icon shown on the face while crop is active:
// two opposing right-angle corner brackets (the universal crop glyph),
// drawn into a 2x-dpr pixmap so it stays crisp on Retina. Light color so it
// reads on the dark VFX theme.
QIcon makeCropIcon() {
    QPixmap pm(28, 28);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(0xe0, 0xe0, 0xe0), 1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    const QPointF tl[3] = {{10, 3}, {3, 3}, {3, 10}};    // top-left bracket
    const QPointF br[3] = {{4, 11}, {11, 11}, {11, 4}};  // bottom-right bracket
    p.drawPolyline(tl, 3);
    p.drawPolyline(br, 3);
    p.end();
    return QIcon(pm);
}
}  // namespace

AspectCropCombo_Qt::AspectCropCombo_Qt(QWidget* parent)
    : QToolButton(parent) {
    // The face is a plain button: current aspect text, optional crop icon
    // beside it. Clicking it opens the popup (see onClicked / openPopup).
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setMinimumWidth(60);
    setFixedHeight(20);  // match the compact button row in the card
    // Read like a combo: left-aligned text, no auto-raise flicker.
    setStyleSheet("QToolButton { text-align: left; padding-left: 4px; }");

    // Build the custom popup frame. Qt::Popup gives us click-outside
    // dismissal and grabs input while shown.
    popupFrame_ = new QFrame(this, Qt::Popup);
    popupFrame_->setFrameShape(QFrame::StyledPanel);
    // Watch the frame's Hide events so the reopen-flicker guard is stamped
    // for EVERY dismissal path — our own hide() calls AND the Qt::Popup
    // outside-click that closes it without going through our code.
    popupFrame_->installEventFilter(this);

    auto* lay = new QVBoxLayout(popupFrame_);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    cropCheck_ = new QCheckBox(QStringLiteral("Crop (letterbox bars)"), popupFrame_);
    cropCheck_->setContentsMargins(6, 4, 6, 4);
    // crop_ is the source of truth; sync the freshly-created checkbox to it
    // (false) so they start unambiguously consistent.
    {
        const QSignalBlocker block(cropCheck_);
        cropCheck_->setChecked(crop_);
    }
    lay->addWidget(cropCheck_);

    auto* divider = new QFrame(popupFrame_);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Sunken);
    lay->addWidget(divider);

    ratioList_ = new QListWidget(popupFrame_);
    ratioList_->setFrameShape(QFrame::NoFrame);
    lay->addWidget(ratioList_);

    auto* divider2 = new QFrame(popupFrame_);
    divider2->setFrameShape(QFrame::HLine);
    divider2->setFrameShadow(QFrame::Sunken);
    lay->addWidget(divider2);

    // Custom-ratio entry now lives in the popup (it used to be the editable
    // combo face). Enter commits the trimmed text as the new aspect.
    customEdit_ = new QLineEdit(popupFrame_);
    customEdit_->setPlaceholderText(QStringLiteral("Custom ratio…"));
    customEdit_->setContentsMargins(6, 4, 6, 4);
    lay->addWidget(customEdit_);

    // Checkbox: flip crop, keep popup OPEN, leave ratio untouched.
    connect(cropCheck_, &QCheckBox::toggled, this, [this](bool on) {
        crop_ = on;
        updateFace();
        emit cropToggled(on);
        // Intentionally do NOT hide the popup — the user may want to also
        // pick a ratio in the same interaction.
    });

    // Ratio click: set aspect, CLOSE popup, leave crop unchanged.
    connect(ratioList_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) {
        if (!item) return;
        const QString s = item->text();
        if (s != aspect_) {
            aspect_ = s;
            updateFace();
            emit aspectChanged(s);
        }
        popupFrame_->hide();
    });

    // Custom ratio committed via Enter: trim, ignore empty, commit + close.
    connect(customEdit_, &QLineEdit::returnPressed, this, [this]() {
        const QString typed = customEdit_->text().trimmed();
        if (typed.isEmpty()) return;
        customEdit_->clear();
        if (typed != aspect_) {
            aspect_ = typed;
            updateFace();
            emit aspectChanged(typed);
        }
        popupFrame_->hide();
    });

    // Clicking the button toggles the popup.
    connect(this, &QToolButton::clicked, this, &AspectCropCombo_Qt::onClicked);

    justClosed_.invalidate();
    updateFace();
}

AspectCropCombo_Qt::~AspectCropCombo_Qt() = default;

void AspectCropCombo_Qt::setPresets(const QStringList& presets) {
    ratioList_->clear();
    for (const QString& p : presets) {
        ratioList_->addItem(p);
    }
}

QString AspectCropCombo_Qt::currentAspect() const {
    return aspect_;
}

bool AspectCropCombo_Qt::cropChecked() const {
    return crop_;
}

void AspectCropCombo_Qt::setCurrentAspect(const QString& aspect) {
    // Signal-free: only updates internal state + face. No aspectChanged.
    if (aspect == aspect_) return;
    aspect_ = aspect;
    updateFace();
}

void AspectCropCombo_Qt::setCropChecked(bool on) {
    // Signal-free: block the checkbox's toggled signal so neither
    // cropToggled nor the lambda fires while we mirror external state.
    if (on == crop_) return;
    crop_ = on;
    {
        const QSignalBlocker block(cropCheck_);
        cropCheck_->setChecked(on);
    }
    updateFace();
}

void AspectCropCombo_Qt::setCropObjectName(const QString& name) {
    cropCheck_->setObjectName(name);
}

void AspectCropCombo_Qt::setCropAccessibleName(const QString& name) {
    cropCheck_->setAccessibleName(name);
}

void AspectCropCombo_Qt::setCropToolTip(const QString& tip) {
    cropCheck_->setToolTip(tip);
}

void AspectCropCombo_Qt::onClicked() {
    // Toggle semantics, implemented purely via the reopen guard:
    //
    // - Popup CLOSED, button clicked → no recent dismissal, guard inactive
    //   → openPopup().
    // - Popup OPEN, button clicked → the Qt::Popup mouse grab delivers the
    //   press FIRST as an outside-click that hides the frame; the frame's
    //   Hide event (eventFilter) stamps justClosed_. Only THEN does this
    //   clicked() signal fire. justClosed_ is still well within the guard
    //   window, so we ignore the open and the popup stays shut — no flicker.
    if (justClosed_.isValid() && !justClosed_.hasExpired(kReopenGuardMs)) {
        return;
    }
    openPopup();
}

bool AspectCropCombo_Qt::eventFilter(QObject* watched, QEvent* event) {
    if (watched == popupFrame_ && event->type() == QEvent::Hide) {
        // Start the guard window now: any button click arriving within
        // kReopenGuardMs is the tail of the same physical click and must
        // not reopen the popup.
        justClosed_.start();
    }
    return QToolButton::eventFilter(watched, event);
}

void AspectCropCombo_Qt::openPopup() {
    // Keep the checkbox in sync (it could have been mutated signal-free).
    {
        const QSignalBlocker block(cropCheck_);
        cropCheck_->setChecked(crop_);
    }

    // Highlight the current ratio row (if it's a known preset). Custom
    // typed ratios simply leave nothing selected.
    ratioList_->clearSelection();
    ratioList_->setCurrentItem(nullptr);
    for (int i = 0; i < ratioList_->count(); ++i) {
        QListWidgetItem* it = ratioList_->item(i);
        if (it->text() == aspect_) {
            ratioList_->setCurrentItem(it);
            break;
        }
    }

    customEdit_->clear();

    // Size the frame first so we can position/flip it against the screen.
    popupFrame_->setMinimumWidth(width());
    popupFrame_->adjustSize();
    const QSize pop = popupFrame_->sizeHint().expandedTo(popupFrame_->size());

    // Available geometry of the screen this widget is on (fall back to the
    // screen at the global cursor anchor if screen() is null).
    const QPoint anchor = mapToGlobal(QPoint(0, height()));
    QScreen* scr = screen();
    if (!scr) scr = QGuiApplication::screenAt(anchor);
    QRect avail = scr ? scr->availableGeometry()
                      : QRect(anchor, pop);  // degenerate fallback

    // Default: directly under the button. Flip above if it would spill off
    // the bottom and there's more room above.
    int x = anchor.x();
    int y = anchor.y();
    if (y + pop.height() > avail.bottom()) {
        const int above = mapToGlobal(QPoint(0, 0)).y() - pop.height();
        if (above >= avail.top()) {
            y = above;  // flip above the button
        } else {
            y = avail.bottom() - pop.height();  // clamp to bottom edge
        }
    }
    // Clamp x so the right edge stays on-screen (and not past the left).
    if (x + pop.width() > avail.right()) x = avail.right() - pop.width();
    if (x < avail.left()) x = avail.left();

    popupFrame_->move(QPoint(x, y));
    popupFrame_->show();
}

void AspectCropCombo_Qt::updateFace() {
    // Face text is ALWAYS the raw aspect string — never decorated, never
    // round-tripped into stored state. The crop indicator is a painted icon.
    setText(aspect_);
    if (crop_) {
        static const QIcon cropIcon = makeCropIcon();
        setIcon(cropIcon);
    } else {
        setIcon(QIcon());
    }
}
