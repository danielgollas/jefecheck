#include "AspectCropCombo_qt.h"

#include <QCheckBox>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QGuiApplication>
#include <QIcon>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
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
}  // namespace

AspectCropCombo_Qt::AspectCropCombo_Qt(QWidget* parent)
    : QToolButton(parent) {
    // The face is a plain button: current aspect text, optional crop icon
    // beside it. Clicking it opens the popup (see onClicked / openPopup).
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setMinimumWidth(60);
    setFixedHeight(20);  // match the compact button row in the card
    // The dark VFX theme (jefecheck_dark.qss) styles QComboBox/QPushButton
    // but NOT QToolButton, so an unstyled QToolButton would render with the
    // native look and clash with the sibling combos/buttons in this row.
    // Mirror the theme's QComboBox body (bg/border/radius/color + orange
    // hover) so the control reads as a dropdown consistent with its
    // neighbors. The `QToolButton`-typed selector keeps this off the popup's
    // checkbox/list/line-edit children (none of which are QToolButtons).
    setStyleSheet(
        "QToolButton {"
        " background-color: #2a2a2a;"
        " border: 1px solid #3a3a3a;"
        " border-radius: 3px;"
        " color: #e0e0e0;"
        " text-align: left;"
        " padding: 2px 4px;"
        " }"
        "QToolButton:hover { border-color: #d4771e; }");

    // Build the custom popup frame. Qt::Popup gives us click-outside
    // dismissal and grabs input while shown.
    popupFrame_ = new QFrame(this, Qt::Popup);
    popupFrame_->setFrameShape(QFrame::StyledPanel);
    // The popup's children (checkbox / list / line-edit) are NOT covered by
    // the plate card's 10pt font stylesheet, so without this they render at
    // the platform default (~13pt on macOS) and the popup balloons. A 10pt
    // QFont propagates to all children and keeps rows compact.
    {
        QFont compact = popupFrame_->font();
        compact.setPointSize(10);
        popupFrame_->setFont(compact);
    }
    // Watch the frame's Hide events so the reopen-flicker guard is stamped
    // for EVERY dismissal path — our own hide() calls AND the Qt::Popup
    // outside-click that closes it without going through our code.
    popupFrame_->installEventFilter(this);

    auto* lay = new QVBoxLayout(popupFrame_);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // Section header so the popup reads as the Aspect control.
    auto* header = new QLabel(QStringLiteral("Aspect"), popupFrame_);
    header->setContentsMargins(6, 3, 6, 1);
    header->setStyleSheet("color: #888; font-weight: bold;");
    lay->addWidget(header);

    cropCheck_ = new QCheckBox(QStringLiteral("Letterbox"), popupFrame_);
    cropCheck_->setContentsMargins(12, 3, 6, 3);  // breathing room left of the box
    // crop_ is the source of truth; sync the freshly-created checkbox to it
    // (false) so they start unambiguously consistent.
    {
        const QSignalBlocker block(cropCheck_);
        cropCheck_->setChecked(crop_);
    }
    lay->addWidget(cropCheck_);

    // Custom-ratio entry lives near the top under Crop (it used to be the
    // editable combo face). Enter commits the trimmed text as the new aspect.
    customEdit_ = new QLineEdit(popupFrame_);
    customEdit_->setPlaceholderText(QStringLiteral("Custom ratio…"));
    customEdit_->setContentsMargins(0, 2, 0, 2);  // full width, like the list rows
    lay->addWidget(customEdit_);

    auto* divider = new QFrame(popupFrame_);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Sunken);
    lay->addWidget(divider);

    ratioList_ = new QListWidget(popupFrame_);
    ratioList_->setFrameShape(QFrame::NoFrame);
    // The list shows a fixed handful of short rows — never scroll, and don't
    // let QListWidget's default 256x192 size hint inflate the popup. Height
    // is pinned to the row count in setPresets(); width comes from the frame.
    ratioList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ratioList_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ratioList_->setUniformItemSizes(true);
    lay->addWidget(ratioList_);

    // Checkbox: flip crop, keep popup OPEN, leave ratio untouched.
    connect(cropCheck_, &QCheckBox::toggled, this, [this](bool on) {
        crop_ = on;
        updateFace();
        emit cropToggled(on);
        // Intentionally do NOT hide the popup — the user may want to also
        // pick a ratio in the same interaction.
    });

    // Ratio click: set aspect, CLOSE popup, leave crop unchanged. Use the
    // canonical UserRole value, not the (possibly decorated) display text.
    connect(ratioList_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) {
        if (!item) return;
        const QString s = item->data(Qt::UserRole).toString();
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
        // The canonical aspect value lives in UserRole; the display text can
        // diverge from it (the "original" row shows the derived native ratio
        // while still meaning "original"). Default the two to be equal.
        auto* item = new QListWidgetItem(p, ratioList_);
        item->setData(Qt::UserRole, p);
    }
    refreshOriginalRow();
    // Pin the list to exactly its rows so the popup is no taller than its
    // contents (QListWidget's default size hint is 256x192). sizeHintForRow
    // needs at least one item, which we now have.
    if (ratioList_->count() > 0) {
        const int rh = ratioList_->sizeHintForRow(0);
        const int fw = ratioList_->frameWidth();
        ratioList_->setFixedHeight(rh * ratioList_->count() + 2 * fw);
    }
}

void AspectCropCombo_Qt::setNativeAspectLabel(const QString& derived) {
    if (derived == nativeAspect_) return;
    nativeAspect_ = derived;
    refreshOriginalRow();
    // If "original" is the current selection, the face shows the computed
    // ratio — refresh it now that the native value changed.
    if (aspect_ == QLatin1String("original")) updateFace();
}

void AspectCropCombo_Qt::refreshOriginalRow() {
    if (!ratioList_) return;
    for (int i = 0; i < ratioList_->count(); ++i) {
        QListWidgetItem* it = ratioList_->item(i);
        if (it->data(Qt::UserRole).toString() == QLatin1String("original")) {
            it->setText(nativeAspect_.isEmpty()
                            ? QStringLiteral("source")
                            : nativeAspect_ + QStringLiteral(" (native)"));
            return;
        }
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
        if (it->data(Qt::UserRole).toString() == aspect_) {
            ratioList_->setCurrentItem(it);
            break;
        }
    }

    customEdit_->clear();

    // Size the frame first so we can position/flip it against the screen.
    // Width is the wider of the combo face and a compact floor — NOT driven
    // by QListWidget's oversized default hint. setFixedWidth keeps adjustSize
    // from ballooning horizontally; height still follows the (now pinned)
    // content.
    popupFrame_->setFixedWidth(qMax(width(), 150));
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
    // Face shows the raw aspect string, EXCEPT when "original" is selected and
    // we know the frame's native ratio — then show the computed ratio (without
    // the drop-down-only "(native)" suffix). The stored aspect_ stays
    // "original" so selection semantics are unchanged; this is display-only.
    // The default ("original") canonical value is shown as "source" on the
    // face; if the frame's native ratio is known we show that number
    // instead. Non-default ratios show their own string.
    QString face = aspect_;
    if (aspect_ == QLatin1String("original")) {
        face = nativeAspect_.isEmpty() ? QStringLiteral("source") : nativeAspect_;
    }
    setText(face);
    // No crop indicator on the closed face — an icon widens the fixed-width
    // button and crowds the text. Letterbox state is shown by the checkbox
    // in the drop-down.
}
