// SPDX-License-Identifier: GPL-2.0-or-later
// First-launch bar above the viewport: asks which keyboard preset the user wants. Any choice
// writes Shortcuts/preset at once, applies it to the registry and hides the bar.
#pragma once

#include <QFrame>

class QSettings;

class ShortcutsBanner_Qt : public QFrame {
    Q_OBJECT
public:
    explicit ShortcutsBanner_Qt(QWidget* parent = nullptr);

    // True when Shortcuts/preset is unset and this is not a headless / self-test run.
    static bool shouldShow(const QSettings& settings, bool isTestRun);

signals:
    // "Customize…": the preset is stored and the bar is hidden; the owner opens Preferences.
    void customizeRequested();

private:
    void choose(const QString& preset);
};
