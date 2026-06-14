#include "AspectCropCombo_qt.h"

#include <QCheckBox>
#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace {
// The glyph prefixed to the button face while crop is active. Kept subtle
// (a small outlined-box glyph + space) so it reads as "letterbox on" without
// crowding the ratio text. It is presentation-only and is never written back
// into the stored aspect string.
const QString kCropPrefix = QStringLiteral("▢ ");  // ▢ + space
}  // namespace

AspectCropCombo_Qt::AspectCropCombo_Qt(QWidget* parent)
    : QComboBox(parent) {
    // Editable so users can still type a custom ratio string. We keep our
    // own model empty (no items) — the presets live in the popup's
    // QListWidget, and the line-edit only ever shows the decorated face.
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    setMinimumWidth(60);

    // Build the custom popup frame. Qt::Popup gives us click-outside
    // dismissal and grabs input while shown.
    popupFrame_ = new QFrame(this, Qt::Popup);
    popupFrame_->setFrameShape(QFrame::StyledPanel);

    auto* lay = new QVBoxLayout(popupFrame_);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    cropCheck_ = new QCheckBox(QStringLiteral("Crop (letterbox bars)"), popupFrame_);
    cropCheck_->setContentsMargins(6, 4, 6, 4);
    lay->addWidget(cropCheck_);

    auto* divider = new QFrame(popupFrame_);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Sunken);
    lay->addWidget(divider);

    ratioList_ = new QListWidget(popupFrame_);
    ratioList_->setFrameShape(QFrame::NoFrame);
    lay->addWidget(ratioList_);

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
        hidePopup();
    });

    // Typed custom ratio committed via Enter / focus-out on the line edit.
    if (lineEdit()) {
        connect(lineEdit(), &QLineEdit::editingFinished,
                this, &AspectCropCombo_Qt::onEditingFinished);
    }

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
    if (cropCheck_) {
        const QSignalBlocker block(cropCheck_);
        cropCheck_->setChecked(on);
    }
    updateFace();
}

void AspectCropCombo_Qt::setCropObjectName(const QString& name) {
    if (cropCheck_) cropCheck_->setObjectName(name);
}

void AspectCropCombo_Qt::setCropAccessibleName(const QString& name) {
    if (cropCheck_) cropCheck_->setAccessibleName(name);
}

void AspectCropCombo_Qt::setCropToolTip(const QString& tip) {
    if (cropCheck_) cropCheck_->setToolTip(tip);
}

void AspectCropCombo_Qt::showPopup() {
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

    // Position the frame directly under the combo, width-matched.
    const QPoint below = mapToGlobal(QPoint(0, height()));
    popupFrame_->setMinimumWidth(width());
    popupFrame_->adjustSize();
    popupFrame_->move(below);
    popupFrame_->show();
}

void AspectCropCombo_Qt::hidePopup() {
    if (popupFrame_) popupFrame_->hide();
    // Do NOT call QComboBox::hidePopup() — the base popup view is never
    // shown, so there is nothing to hide there.
}

void AspectCropCombo_Qt::onEditingFinished() {
    if (updatingFace_ || !lineEdit()) return;
    QString typed = lineEdit()->text();
    // Strip the presentation-only crop prefix if the user committed the
    // decorated face verbatim (e.g. focus-out without editing).
    if (typed.startsWith(kCropPrefix)) {
        typed = typed.mid(kCropPrefix.size());
    }
    typed = typed.trimmed();
    if (typed.isEmpty() || typed == aspect_) {
        // Nothing meaningful changed — re-render to discard any stray
        // edits and restore the canonical decorated face.
        updateFace();
        return;
    }
    aspect_ = typed;
    updateFace();
    emit aspectChanged(typed);
}

void AspectCropCombo_Qt::updateFace() {
    if (!lineEdit()) return;
    // Guard so the programmatic setEditText below doesn't re-trigger
    // editingFinished and recurse.
    updatingFace_ = true;
    const QString face = crop_ ? (kCropPrefix + aspect_) : aspect_;
    setEditText(face);
    updatingFace_ = false;
}
