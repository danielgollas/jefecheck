// SPDX-License-Identifier: GPL-2.0-or-later
// --shortcuts-page-test [outDir]: the Preferences -> Shortcuts page, driven in process. Builds
// the Preferences dialog, selects Shortcuts and works the page the way a user would (search,
// key capture through the in-place editor, conflict bar, reset dot, preset switch, import /
// export), then checks that the dialog's reject() puts every key back. The registry is pinned
// to the Editing preset first and the user's keys are restored at the end (memory only;
// nothing is written to QSettings).
#include "MainWindow_qt.h"

#include "PreferencesWindow_qt.h"
#include "Shortcuts_qt.h"
#include "ShortcutsPage_qt.h"
#include "UiDensityCheck_qt.h"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTreeView>

#include <cstdio>

namespace sc = jefe::shortcuts;

namespace {

int g_pageFails = 0;

void pageCheck(const char* name, bool ok, const QString& detail = QString())
{
    std::printf("SHORTCUTS-PAGE-TEST %s %s %s\n", name, ok ? "PASS" : "FAIL", detail.toUtf8().constData());
    if (!ok) ++g_pageFails;
}

void settle(int ms = 30)
{
    QElapsedTimer t;
    t.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (t.elapsed() < ms);
}

QString keysOf(const char* id)
{
    QStringList out;
    for (const QKeySequence& k : sc::Registry::instance().keys(id)) out << k.toString(QKeySequence::PortableText);
    return out.join(QStringLiteral(" | "));
}

// Every command's and hold key's keys plus the preset: equal strings = same registry state.
QString allKeys()
{
    auto& reg = sc::Registry::instance();
    QStringList out;
    for (const sc::Command& c : reg.commands()) out << QString::fromUtf8(c.id) + "=" + keysOf(c.id);
    for (const sc::HoldKey& h : reg.holdKeys()) out << QString::fromUtf8(h.id) + "=" + QString::number(int(reg.holdKey(h.id)));
    return out.join(';') + "|" + reg.preset();
}

// A key press (and release) the way the capture editor receives one.
void press(QWidget* w, int key, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString& text = QString())
{
    if (!w) return;
    QKeyEvent down(QEvent::KeyPress, key, mods, text);
    QApplication::sendEvent(w, &down);
    QKeyEvent up(QEvent::KeyRelease, key, mods, text);
    QApplication::sendEvent(w, &up);
    settle(10);
}

// Opens the capture on `id` and presses one key into it. False when no editor opened.
bool capture(ShortcutsPage_Qt* page, const char* id, int slot, int key, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    if (!page->beginCapture(QString::fromUtf8(id), slot)) return false;
    settle(10);
    ShortcutCaptureEdit_Qt* ed = page->captureEditor();
    if (!ed) return false;
    press(ed, key, mods);
    return true;
}

// A key no command, hold key or preset uses, in any scope.
QKeySequence freeKey(const char* id)
{
    auto& reg = sc::Registry::instance();
    for (int k = Qt::Key_A; k <= Qt::Key_Z; ++k) {
        const QKeySequence s(QKeyCombination(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier, Qt::Key(k)));
        if (reg.conflicts(id, s).isEmpty()) return s;
    }
    return {};
}

// The density rules with an inline bar showing (the --ui-density-test pass sees the page idle).
QString densityProblems(const QWidget* root)
{
    QStringList out;
    for (const auto& v : jefe::qt::ui::checkDensity(root)) out << v.rule + " " + v.widgetPath + ": " + v.detail;
    return out.join(QStringLiteral("; "));
}

}  // namespace

int MainWindow_Qt::runHeadlessShortcutsPageTest(const QString& outDir)
{
    auto& reg = sc::Registry::instance();
    struct RestoreKeys {
        sc::Registry::Snapshot before = sc::Registry::instance().snapshot();
        ~RestoreKeys() { sc::Registry::instance().restore(before); }
    } restoreKeys;
    reg.pinPreset(QStringLiteral("editing"));
    const QString pinned = allKeys();
    g_pageFails = 0;

    const QString shots = outDir.isEmpty() ? QStringLiteral("/Volumes/Archive/jefecheck-builds/shortcuts-shots") : outDir;
    QTemporaryDir tmp;

    {
        PreferencesWindow_Qt dlg(this);
        dlg.resize(760, 560);
        dlg.show();
        settle(100);
        auto* sidebar = dlg.findChild<QListWidget*>(QStringLiteral("preferences.sidebar"));
        int row = -1;
        if (sidebar)
            for (int i = 0; i < sidebar->count(); ++i)
                if (sidebar->item(i)->text() == QLatin1String("Shortcuts")) row = i;
        pageCheck("page-sixth-section", sidebar && row == 5 && sidebar->count() == 6,
                  QStringLiteral("row=%1").arg(row));
        if (sidebar && row >= 0) sidebar->setCurrentRow(row);
        settle(50);
        auto* page = dlg.findChild<ShortcutsPage_Qt*>();
        pageCheck("page-shown", page && page->isVisible());
        if (!page) {
            std::printf("SHORTCUTS-PAGE-TEST FAIL\n");
            std::fflush(stdout);
            return 2;
        }
        const char* fwd = "playback.jkl-forward";

        // Search.
        page->search()->setText(QStringLiteral("play forward"));
        settle();
        const QStringList shown = page->visibleIds();
        pageCheck("search-filters", shown.contains(QString::fromUtf8(fwd)) && !shown.contains("playback.play-pause") &&
                                        !shown.isEmpty() && shown.size() <= 2,
                  shown.join(','));
        page->search()->setText(QStringLiteral("Space"));
        settle();
        pageCheck("search-by-key", page->visibleIds().contains("playback.play-pause"), page->visibleIds().join(','));
        page->search()->clear();
        settle();
        const int allRows = int(page->visibleIds().size());
        pageCheck("search-cleared-shows-all", allRows == int(reg.commands().size() + reg.holdKeys().size() +
                                                             page->panelRowIds().size()),
                  QString::number(allRows));

        // Capture a free key: set, changed label + reset dot.
        const QKeySequence free = freeKey(fwd);
        const bool opened = capture(page, fwd, 0, free[0].key(), free[0].keyboardModifiers());
        pageCheck("capture-free-key",
                  opened && !free.isEmpty() && reg.key(fwd) == free && page->rowShowsChanged(fwd) &&
                      page->keyText(fwd, 0) == free.toString(QKeySequence::NativeText) && !page->captureEditor(),
                  keysOf(fwd) + " shown=" + page->keyText(fwd, 0));

        // Modifier alone and an unknown key: the capture keeps waiting, nothing changes.
        {
            const QString before = allKeys();
            page->beginCapture(QString::fromUtf8(fwd), 0);
            settle(10);
            ShortcutCaptureEdit_Qt* ed = page->captureEditor();
            press(ed, Qt::Key_Shift, Qt::ShiftModifier);
            press(ed, Qt::Key_unknown);
            const bool waiting = page->captureEditor() == ed && ed;
            press(ed, Qt::Key_Escape);
            pageCheck("capture-ignores-modifier-only", waiting && allKeys() == before && !page->captureEditor());
        }

        // Escape cancels without changing anything.
        {
            const QString before = allKeys();
            const bool ok = capture(page, "playback.step-back", 0, Qt::Key_Escape);
            pageCheck("capture-escape-cancels", ok && allKeys() == before && !page->captureEditor());
        }

        // A taken key: conflict bar; Cancel leaves both.
        auto* bar = dlg.findChild<QWidget*>(QStringLiteral("prefs.shortcuts.conflict"));
        auto* replace = dlg.findChild<QPushButton*>(QStringLiteral("prefs.shortcuts.conflict.replace"));
        auto* cancel = dlg.findChild<QPushButton*>(QStringLiteral("prefs.shortcuts.conflict.cancel"));
        auto* barText = dlg.findChild<QLabel*>(QStringLiteral("prefs.shortcuts.conflict.text"));
        const QKeySequence space(Qt::Key_Space);
        {
            capture(page, fwd, 0, Qt::Key_Space);
            settle();
            const bool shownBar = bar && bar->isVisible() && barText && barText->text().contains("Play / Pause");
            const bool unchanged = reg.key(fwd) == free && reg.key("playback.play-pause") == space;
            const QString dens = densityProblems(&dlg);
            pageCheck("density-conflict-bar", dens.isEmpty(), dens);
            if (bar && bar->isVisible() && !shots.isEmpty()) {
                QDir().mkpath(shots);
                dlg.grab().save(shots + "/shortcuts-page-conflict.png");
            }
            if (cancel) cancel->click();
            settle();
            pageCheck("conflict-bar-shown", shownBar && unchanged, barText ? barText->text() : QString());
            pageCheck("conflict-cancel-leaves-both",
                      bar && !bar->isVisible() && reg.key(fwd) == free && reg.key("playback.play-pause") == space);
        }
        // Replace moves the key and unbinds the other command.
        {
            capture(page, fwd, 0, Qt::Key_Space);
            settle();
            const bool shownBar = bar && bar->isVisible();
            if (replace) replace->click();
            settle();
            pageCheck("conflict-replace-moves-key",
                      shownBar && reg.key(fwd) == space && reg.keys("playback.play-pause").isEmpty() &&
                          page->rowShowsChanged("playback.play-pause") && bar && !bar->isVisible(),
                      keysOf(fwd) + " / " + keysOf("playback.play-pause"));
        }
        // Shift + a digit symbol records Shift+digit (CC favourite 1's key in Editing): a conflict.
        {
            const char* fav = "color.cc-fav-apply-1";
            const QKeySequence favKey = reg.key(fav);
            capture(page, "playback.step-back", 0, Qt::Key_Exclam, Qt::ShiftModifier);
            settle();
            const bool shownBar = bar && bar->isVisible() && favKey == QKeySequence(Qt::SHIFT | Qt::Key_1);
            if (cancel) cancel->click();
            settle();
            pageCheck("capture-shift-digit", shownBar && reg.key(fav) == favKey, favKey.toString());
        }

        // Backspace on an empty capture unbinds.
        {
            const bool ok = capture(page, fwd, 0, Qt::Key_Backspace);
            pageCheck("capture-backspace-unbinds", ok && reg.keys(fwd).isEmpty() && page->keyText(fwd, 0).isEmpty(),
                      keysOf(fwd));
        }

        // The second key is captured on its own; the first stays.
        {
            const char* step = "playback.step-forward";
            const QKeySequence first = reg.key(step);
            const QKeySequence second = freeKey(step);
            const bool ok = capture(page, step, 1, second[0].key(), second[0].keyboardModifiers());
            const QList<QKeySequence> ks = reg.keys(step);
            pageCheck("capture-second-key", ok && ks.size() == 2 && ks[0] == first && ks[1] == second &&
                                                page->keyText(step, 1) == second.toString(QKeySequence::NativeText),
                      keysOf(step));
        }

        // A hold key takes one bare key: modifiers are refused with a notice.
        {
            const char* hold = "color.drag-gamma";
            const Qt::Key before = reg.holdKey(hold);
            capture(page, hold, 0, Qt::Key_Y, Qt::ControlModifier);
            auto* notice = dlg.findChild<QLabel*>(QStringLiteral("prefs.shortcuts.notice"));
            pageCheck("hold-rejects-modifiers", reg.holdKey(hold) == before && notice && notice->isVisible());
        }

        // The reset dot puts the preset key back.
        {
            const bool clicked = page->resetRowByClick(QStringLiteral("playback.play-pause"));
            settle();
            pageCheck("reset-dot-restores", clicked && reg.keys("playback.play-pause") == reg.presetKeys("playback.play-pause") &&
                                                !page->rowShowsChanged("playback.play-pause"),
                      keysOf("playback.play-pause"));
        }

        // Read-only "Inside panels" rows: no capture.
        {
            const QString before = allKeys();
            bool none = true;
            for (const QString& id : page->panelRowIds()) none = none && !page->beginCapture(id, 0) && !page->captureEditor();
            const QString lutKey = page->keyText(QStringLiteral("panel.lut-frame"), 0);
            pageCheck("inside-panels-read-only", none && !page->panelRowIds().isEmpty() && allKeys() == before &&
                                                     lutKey == QKeySequence(Qt::Key_F).toString(QKeySequence::NativeText),
                      lutKey);
        }

        // Preset switch with custom keys: inline bar; Keep leaves everything, Switch applies.
        {
            auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("prefs.shortcuts.preset"));
            auto* pbar = dlg.findChild<QWidget*>(QStringLiteral("prefs.shortcuts.presetbar"));
            auto* ptext = dlg.findChild<QLabel*>(QStringLiteral("prefs.shortcuts.presetbar.text"));
            auto* keep = dlg.findChild<QPushButton*>(QStringLiteral("prefs.shortcuts.preset.keep"));
            auto* sw = dlg.findChild<QPushButton*>(QStringLiteral("prefs.shortcuts.preset.switch"));
            const int classic = combo ? combo->findData(QStringLiteral("classic")) : -1;
            const int n = reg.overrideCount();
            const QString before = allKeys();
            if (combo && classic >= 0) {
                combo->setCurrentIndex(classic);
                emit combo->activated(classic);
            }
            settle();
            const QString expect = QStringLiteral("Switching discards %1 custom key").arg(n);
            const bool asked = pbar && pbar->isVisible() && ptext && ptext->text().startsWith(expect) &&
                               reg.preset() == "editing";
            const QString dens = densityProblems(&dlg);
            pageCheck("density-preset-bar", dens.isEmpty(), dens);
            if (keep) keep->click();
            settle();
            pageCheck("preset-switch-asks", asked && n > 0, ptext ? ptext->text() : QString());
            pageCheck("preset-keep", allKeys() == before && pbar && !pbar->isVisible() && combo &&
                                         combo->currentData().toString() == "editing");
            if (combo && classic >= 0) {
                combo->setCurrentIndex(classic);
                emit combo->activated(classic);
            }
            settle();
            if (sw) sw->click();
            settle();
            pageCheck("preset-switch", reg.preset() == "classic" && !reg.isCustom() && pbar && !pbar->isVisible());
        }

        // Export, change, Import: the keys come back.
        {
            reg.pinPreset(QStringLiteral("editing"));
            reg.setOverride("playback.step-back", freeKey("playback.step-back"));
            const QString exported = allKeys();
            const QString path = tmp.path() + QStringLiteral("/test.jcshortcuts");
            const bool wrote = page->exportFile(path);
            reg.setPreset(QStringLiteral("classic"));
            reg.setOverride("playback.go-end", QKeySequence(Qt::Key_F12));
            const bool differs = allKeys() != exported;
            const bool read = page->importFile(path);
            pageCheck("export-import-roundtrip", wrote && differs && read && allKeys() == exported);
            const QString bad = tmp.path() + QStringLiteral("/bad.jcshortcuts");
            QFile f(bad);
            if (f.open(QIODevice::WriteOnly)) {
                f.write("{not json");
                f.close();
            }
            auto* notice = dlg.findChild<QLabel*>(QStringLiteral("prefs.shortcuts.notice"));
            const bool refused = !page->importFile(bad) && allKeys() == exported && notice && notice->isVisible() &&
                                 notice->text().startsWith("Could not import");
            pageCheck("import-bad-file-notice", refused, notice ? notice->text() : QString());
        }

        // Enter on a row opens the capture; Escape closes it.
        {
            page->selectCommand(QStringLiteral("playback.step-back"));
            page->list()->setFocus();
            press(page->list(), Qt::Key_Return);
            const bool opened = page->captureEditor() != nullptr;
            press(page->captureEditor(), Qt::Key_Escape);
            pageCheck("enter-opens-capture", opened && !page->captureEditor());
        }

        // A conflict bar left open across a preset switch, the reset dot or Reset to preset must
        // not let Replace act on the old measurement. Editing: V is Flip; capture V for Step Back
        // (the bar names Flip), then switch to 1.7 (V = Go to End, Flip = Ctrl+8), then Replace.
        auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("prefs.shortcuts.preset"));
        auto* sw = dlg.findChild<QPushButton*>(QStringLiteral("prefs.shortcuts.preset.switch"));
        auto* resetAll = dlg.findChild<QPushButton*>(QStringLiteral("prefs.shortcuts.reset"));
        auto* pbar = dlg.findChild<QWidget*>(QStringLiteral("prefs.shortcuts.presetbar"));
        const char* back = "playback.step-back";
        const QKeySequence v(Qt::Key_V);
        auto chooseClassic = [&] {
            const int classic = combo ? combo->findData(QStringLiteral("classic")) : -1;
            if (classic < 0) return;
            combo->setCurrentIndex(classic);
            emit combo->activated(classic);
            settle();
        };
        auto vOnOneCommand = [&] {  // no command shares V with another in a clashing scope
            for (const sc::Command& c : reg.commands())
                if (reg.keys(c.id).contains(v) && !reg.conflicts(c.id, v).isEmpty()) return false;
            return true;
        };
        for (const bool custom : {false, true}) {
            reg.pinPreset(QStringLiteral("editing"));
            if (custom) reg.setOverride("playback.go-start", freeKey("playback.go-start"));
            capture(page, back, 0, Qt::Key_V);
            settle();
            const bool named = bar && bar->isVisible() && barText && barText->text().contains("Flip");
            chooseClassic();
            if (custom && sw) sw->click();  // custom keys: the preset bar asks first
            settle();
            const bool hidden = bar && !bar->isVisible() && pbar && !pbar->isVisible();
            if (replace) replace->click();  // a hidden button still clicks: must do nothing
            settle();
            const QByteArray name = custom ? QByteArray("stale-conflict-preset-switch-custom")
                                           : QByteArray("stale-conflict-preset-switch");
            pageCheck(name.constData(),
                      named && hidden && reg.preset() == "classic" && vOnOneCommand() &&
                          reg.keys(back) == reg.presetKeys(back),
                      QStringLiteral("named=%1 hidden=%2 back=%3 goEnd=%4")
                          .arg(named).arg(hidden).arg(keysOf(back), keysOf("playback.go-end")));
        }
        {   // The reset dot and Reset to preset close the bar too.
            reg.pinPreset(QStringLiteral("editing"));
            reg.setOverride("playback.go-start", freeKey("playback.go-start"));
            capture(page, back, 0, Qt::Key_V);
            settle();
            const bool shown1 = bar && bar->isVisible();
            page->resetRowByClick(QStringLiteral("playback.go-start"));
            settle();
            const bool dotHides = bar && !bar->isVisible();
            if (replace) replace->click();
            settle();
            const bool dotOk = shown1 && dotHides && reg.keys(back) == reg.presetKeys(back) &&
                               reg.key("plate.flip") == v;
            reg.setOverride("playback.go-start", freeKey("playback.go-start"));
            capture(page, back, 0, Qt::Key_V);
            settle();
            const bool shown2 = bar && bar->isVisible();
            if (resetAll) resetAll->click();
            settle();
            const bool resetHides = bar && !bar->isVisible();
            if (replace) replace->click();
            settle();
            const bool resetOk = shown2 && resetHides && reg.keys(back) == reg.presetKeys(back) &&
                                 reg.key("plate.flip") == v && !reg.isCustom();
            pageCheck("stale-conflict-reset", dotOk && resetOk,
                      QStringLiteral("dot=%1 reset=%2 flip=%3").arg(dotOk).arg(resetOk).arg(keysOf("plate.flip")));
        }

        // Escape and Return stay on the page: the dialog's Escape (reject) reverts every page and
        // its Return presses Done.
        {
            reg.pinPreset(QStringLiteral("editing"));
            reg.setOverride("playback.go-start", freeKey("playback.go-start"));  // an edit Escape must not lose
            const QString edited = allKeys();
            page->search()->setText(QStringLiteral("zzzz"));
            page->search()->setFocus();
            press(page->search(), Qt::Key_Escape);
            pageCheck("escape-clears-search", dlg.isVisible() && page->search()->text().isEmpty() && allKeys() == edited,
                      QStringLiteral("visible=%1 text=%2").arg(dlg.isVisible()).arg(page->search()->text()));

            capture(page, back, 0, Qt::Key_V);
            settle();
            const bool shown = bar && bar->isVisible();
            page->list()->setFocus();
            press(page->list(), Qt::Key_Escape);
            pageCheck("escape-cancels-conflict", shown && dlg.isVisible() && bar && !bar->isVisible() && allKeys() == edited,
                      QStringLiteral("shown=%1 visible=%2").arg(shown).arg(dlg.isVisible()));

            chooseClassic();  // custom keys: the preset bar asks
            const bool asked = pbar && pbar->isVisible();
            page->search()->setFocus();
            press(page->search(), Qt::Key_Escape);
            pageCheck("escape-keeps-preset", asked && dlg.isVisible() && pbar && !pbar->isVisible() &&
                                                 allKeys() == edited && combo && combo->currentData().toString() == "editing",
                      QStringLiteral("asked=%1 visible=%2").arg(asked).arg(dlg.isVisible()));

            page->search()->setText(QStringLiteral("play forward"));
            settle();
            page->search()->setFocus();
            press(page->search(), Qt::Key_Return);
            const QString cur = page->list()->currentIndex().siblingAtColumn(0).data(Qt::UserRole + 1).toString();
            pageCheck("return-in-search-goes-to-list",
                      dlg.isVisible() && dlg.result() != QDialog::Accepted && cur == QString::fromUtf8(fwd) &&
                          QApplication::focusWidget() == page->list(),
                      QStringLiteral("visible=%1 current=%2").arg(dlg.isVisible()).arg(cur));
            page->search()->clear();
            settle();
        }

        // selectCommand + commandSelected.
        {
            QString got;
            const QMetaObject::Connection c =
                connect(page, &ShortcutsPage_Qt::commandSelected, this, [&got](const QString& id) { got = id; });
            page->search()->setText(QStringLiteral("zzzz-nothing"));
            page->selectCommand(QStringLiteral("plate.fit"));
            disconnect(c);
            pageCheck("select-command", got == "plate.fit" && page->search()->text().isEmpty(), got);
        }

        page->search()->clear();
        settle(50);
        if (!shots.isEmpty()) {
            QDir().mkpath(shots);
            dlg.grab().save(shots + "/shortcuts-page.png");
        }

        // Cancel / Escape / close: every key back to what it was when the dialog opened.
        dlg.reject();
        settle();
        pageCheck("reject-restores", allKeys() == pinned && !reg.isCustom());
    }

    std::printf("SHORTCUTS-PAGE-TEST %s\n", g_pageFails == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
    return g_pageFails == 0 ? 0 : 2;
}
