// SPDX-License-Identifier: GPL-2.0-or-later
#include "ShortcutsBanner_qt.h"

#include "Shortcuts_qt.h"
#include "UiMetrics_qt.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>

bool ShortcutsBanner_Qt::shouldShow(const QSettings& settings, bool isTestRun) {
    return !isTestRun && !settings.contains(QStringLiteral("Shortcuts/preset"));
}

ShortcutsBanner_Qt::ShortcutsBanner_Qt(QWidget* parent) : QFrame(parent) {
    setObjectName("shortcuts.banner");
    setFrameShape(QFrame::NoFrame);
    // A top bar in the dark theme's accent: a thin orange rule under it.
    setStyleSheet(QStringLiteral(
        "ShortcutsBanner_Qt { background:#2a2a2e; "
        "border-bottom:1px solid #e8892b; }"));
    namespace ui = jefe::qt::ui;
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(ui::kGapGroup, ui::kGapTight, ui::kGapGroup, ui::kGapTight);
    row->setSpacing(ui::kGap);

    auto* text = new QLabel(
        tr("Keyboard shortcuts follow Premiere, Resolve and Final Cut. Used JefeCheck 1.7?"), this);
    text->setObjectName("shortcuts.banner.text");
    row->addWidget(text, 1);

    auto* classic = new QPushButton(tr("Use 1.7 keys"), this);
    classic->setObjectName("shortcuts.banner.classic");
    auto* keep = new QPushButton(tr("Keep"), this);
    keep->setObjectName("shortcuts.banner.keep");
    auto* customize = new QPushButton(tr("Customize…"), this);
    customize->setObjectName("shortcuts.banner.customize");
    row->addWidget(classic);
    row->addWidget(keep);
    row->addWidget(customize);

    connect(classic, &QPushButton::clicked, this, [this]() { choose(QStringLiteral("classic")); });
    connect(keep, &QPushButton::clicked, this, [this]() { choose(QStringLiteral("editing")); });
    connect(customize, &QPushButton::clicked, this, [this]() {
        choose(QStringLiteral("editing"));
        emit customizeRequested();
    });
}

void ShortcutsBanner_Qt::choose(const QString& preset) {
    jefe::shortcuts::Registry::instance().setPreset(preset);
    QSettings s;
    s.setValue(QStringLiteral("Shortcuts/preset"), preset);
    s.sync();
    hide();
}
