// SPDX-License-Identifier: GPL-2.0-or-later
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
// How long after the popup dismisses a click counts as a "close": Qt::Popup
// eats the dismissing press, and the click reaching the button must not reopen it.
constexpr qint64 kReopenGuardMs = 150;
}  // namespace

AspectCropCombo_Qt::AspectCropCombo_Qt(QWidget* parent)
    : QToolButton(parent) {
    // The face is a plain button: current aspect text, optional crop icon
    // beside it. Clicking it opens the popup (see onClicked / openPopup).
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setMinimumWidth(60);
    setFixedHeight(20);  // match the compact button row in the card
    // The dark theme styles QComboBox/QPushButton but not QToolButton; mirror its
    // QComboBox body here (the selector keeps it off the popup's children).
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
    // The plate card's 10pt font stylesheet does not cover the popup's
    // children; a 10pt QFont keeps rows compact (default is ~13pt on macOS).
    {
        QFont compact = popupFrame_->font();
        compact.setPointSize(10);
        popupFrame_->setFont(compact);
    }
    // Watch Hide events so the reopen guard is stamped for every dismissal path,
    // including the Qt::Popup outside-click.
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

    // Custom-ratio entry under Crop; Enter commits the trimmed text as the aspect.
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
    // Never scroll, and don't let QListWidget's default 256x192 size hint
    // inflate the popup. Height is pinned in setPresets().
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
        // The canonical aspect value lives in UserRole; the display text can differ
        // (the "original" row shows the derived native ratio).
        auto* item = new QListWidgetItem(p, ratioList_);
        item->setData(Qt::UserRole, p);
    }
    refreshOriginalRow();
    // Pin the list to its rows (default size hint is 256x192); sizeHintForRow
    // needs at least one item.
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
    // Toggle via the reopen guard: with the popup open, the Qt::Popup grab delivers
    // the press as an outside-click (Hide stamps justClosed_) before clicked() fires.
    if (justClosed_.isValid() && !justClosed_.hasExpired(kReopenGuardMs)) {
        return;
    }
    openPopup();
}

bool AspectCropCombo_Qt::eventFilter(QObject* watched, QEvent* event) {
    if (watched == popupFrame_ && event->type() == QEvent::Hide) {
        // Start the guard window: a click within kReopenGuardMs is the tail of the
        // same physical click and must not reopen the popup.
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

    // Size the frame first so it can be positioned/flipped against the screen; width
    // is the wider of the combo face and a floor, not QListWidget's oversized hint.
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
    // Face shows the raw aspect string; "original" shows "source", or the
    // computed native ratio when known. The stored aspect_ stays "original".
    QString face = aspect_;
    if (aspect_ == QLatin1String("original")) {
        face = nativeAspect_.isEmpty() ? QStringLiteral("source") : nativeAspect_;
    }
    setText(face);
    // No crop indicator on the closed face (an icon widens the fixed-width
    // button); letterbox state is the checkbox in the drop-down.
}
