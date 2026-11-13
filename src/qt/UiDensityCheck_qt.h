// SPDX-License-Identifier: GPL-2.0-or-later
// Headless density checker: walks a visible widget tree and reports where it
// breaks the shared metrics (control height, icon squares, text size, overlap,
// clipping, line alignment). Used by --ui-density-test.
#pragma once

#include <QList>
#include <QString>

class QWidget;

namespace jefe::qt::ui {

struct DensityViolation {
    QString widgetPath;  // dotted objectName chain; class name when objectName is empty
    QString rule;        // height | square | font | overlap | clip | textclip | hclip | valign
    QString detail;
};

QList<DensityViolation> checkDensity(const QWidget* root);

// Builds synthetic widget trees, prints one DENSITY-SELFTEST line per case,
// and returns true when every case passes.
bool runDensityCheckerSelfTest();

}  // namespace jefe::qt::ui
