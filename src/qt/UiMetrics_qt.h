// SPDX-License-Identifier: GPL-2.0-or-later
// Shared density metrics for the Qt panels: one set of numbers so every dock
// and dialog uses the same text size, control height and spacing.
#pragma once

#include <QAbstractButton>
#include <QBoxLayout>
#include <QLayout>
#include <QSize>

namespace jefe::qt::ui {

constexpr int kGapTight = 4;
constexpr int kGap = 8;
constexpr int kGapGroup = 12;
constexpr int kControlH = 24;
constexpr int kFontPx = 13;
constexpr int kIconPx = 16;

// A widget with this dynamic property set to true is skipped by the density
// checker, along with its subtree (painted or custom widgets, GL canvases).
inline constexpr const char* kDensityExemptProperty = "jefeDensityExempt";

// A label or combo with this property set to true elides its own text, so the
// checker does not report it as horizontally clipped.
inline constexpr const char* kDensityElidedProperty = "jefeElided";

inline void applyRowMetrics(QBoxLayout* row, int spacing = kGap) {
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(spacing);
}

inline void applyGroupMetrics(QLayout* l) {
    l->setContentsMargins(kGapGroup, kGapGroup, kGapGroup, kGapGroup);
    l->setSpacing(kGap);
}

inline void makeIconButton(QAbstractButton* b) {
    b->setFixedSize(kControlH, kControlH);
    b->setIconSize(QSize(kIconPx, kIconPx));
}

/** Buttons in a FlowLayout or beside fields: report the real widget rect (macOS
    otherwise gives layouts a 12 px rect and the button paints ~2 px low). */
inline void useWidgetRect(QAbstractButton* b) {
    b->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    b->setMinimumHeight(kControlH);
}

}  // namespace jefe::qt::ui
