// SPDX-License-Identifier: GPL-2.0-or-later
// --shortcuts-focus-test: keys fire only where they should. Each check focuses a real
// widget and sends the key to QApplication::focusWidget(), the way a key press arrives,
// so Qt's shortcut map (ShortcutOverride first, then the actions' contexts) decides.
#include "MainWindow_qt.h"

#include "GlViewport_qt.h"
#include "PlaylistPanel_qt.h"
#include "RenderDialog_qt.h"
#include "SequenceLoadBridge_qt.h"
#include "Shortcuts_qt.h"
#include "TimelinePanel_qt.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>
#include <tuple>

namespace {

void settleFor(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// Launched from a terminal, macOS may refuse to activate the app; focus and WidgetShortcut
// need an active window, so force Qt's notion of it (test only).
void makeActive(QWidget* w) {
    w->raise();
    w->activateWindow();
    settleFor(150);
    if (QApplication::activeWindow() != w->window()) {
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        QApplication::setActiveWindow(w->window());
        QT_WARNING_POP
    }
}

bool focusOn(QWidget* w) {
    makeActive(w->window());
    w->setFocus(Qt::OtherFocusReason);
    settleFor(100);
    return QApplication::focusWidget() == w;
}

// A press and release delivered to the focus widget, as the platform delivers them.
void press(Qt::Key k, const QString& text, Qt::KeyboardModifiers m = Qt::NoModifier) {
    QWidget* target = QApplication::focusWidget();
    if (!target) return;
    QKeyEvent down(QEvent::KeyPress, k, m, text);
    QApplication::sendEvent(target, &down);
    QKeyEvent up(QEvent::KeyRelease, k, m, text);
    QApplication::sendEvent(target, &up);
    settleFor(30);
}

// A left click as the platform delivers it. QApplication gives click focus only for
// spontaneous (platform) presses, which a sent QMouseEvent is not, so first do what
// QApplicationPrivate::giveFocusAccordingToFocusPolicy does for one: walk up from the clicked
// widget to its window and focus the first enabled widget whose policy takes click focus,
// stopping early at one that already has focus.
void click(QWidget* w, QPoint at) {
    QPoint pos = at;
    for (QWidget* f = w; f; f = f->parentWidget()) {
        QWidget* proxy = f->focusProxy() ? f->focusProxy() : f;
        if (f->isEnabled() && f->rect().contains(pos) &&
            (proxy->focusPolicy() & Qt::ClickFocus) == Qt::ClickFocus) {
            f->setFocus(Qt::MouseFocusReason);
            break;
        }
        if (f->isWindow() || proxy->hasFocus()) break;
        pos += f->pos();
    }
    const QPointF local(at), global(w->mapToGlobal(at));
    QMouseEvent down(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &down);
    QMouseEvent up(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &up);
    settleFor(50);
}

QLineEdit* firstEditableLineEdit(QWidget* root) {
    for (QLineEdit* e : root->findChildren<QLineEdit*>())
        if (e->isVisible() && e->isEnabled() && !e->isReadOnly() && e->echoMode() == QLineEdit::Normal)
            return e;
    return nullptr;
}

}  // namespace

int MainWindow_Qt::runHeadlessShortcutsFocusTest(bool realKeys) {
    namespace sc = jefe::shortcuts;
    using namespace jefe::qt;
    bool allPass = true;
    auto report = [&](const char* check, bool ok, const QString& detail) {
        if (!ok) allPass = false;
        printf("SHORTCUTS-FOCUS-TEST %s %s %s\n", check, ok ? "PASS" : "FAIL", detail.toLocal8Bit().constData());
        fflush(stdout);
    };
    auto skip = [](const char* check, const QString& why) {
        printf("SHORTCUTS-FOCUS-TEST %s SKIP %s\n", check, why.toLocal8Bit().constData());
        fflush(stdout);
    };
    if (!viewport_ || !timelineDock_ || !timelinePanelWidget_ || !playlistPanelWidget_ || !remoteDock_) {
        printf("SHORTCUTS-FOCUS-TEST FAIL: window not built\n");
        fflush(stdout);
        return 2;
    }

    // Every registry command that runs, in order; each check clears it first.
    QStringList ran;
    const QMetaObject::Connection spy = connect(&sc::Registry::instance(), &sc::Registry::commandRan, this,
            [&ran](const QString& id, sc::Category) { ran << id; });

    // Media: a 5-frame sequence on plate A, so step, play and in-point keys have room to act.
    QTemporaryDir tmp;
    if (!tmp.isValid()) { printf("SHORTCUTS-FOCUS-TEST FAIL: no temp dir\n"); fflush(stdout); return 2; }
    QImage img(64, 64, QImage::Format_RGB888);
    for (int i = 1; i <= 5; ++i) {
        img.fill(QColor(40 * i, 90, 160));
        if (!img.save(QDir(tmp.path()).filePath(QString("seq.%1.png").arg(i, 4, 10, QChar('0'))))) {
            printf("SHORTCUTS-FOCUS-TEST FAIL: cannot write test frames\n");
            fflush(stdout);
            return 2;
        }
    }
    loadFileIntoPlate(0, QDir(tmp.path()).filePath("seq.0001.png"));
    {
        QElapsedTimer t;
        t.start();
        while (getToFrame() - getFromFrame() < 4 && t.elapsed() < 5000) settleFor(50);
    }
    report("MEDIA", getToFrame() - getFromFrame() >= 4,
           QString("frames %1..%2").arg(getFromFrame()).arg(getToFrame()));
    if (isPlaying()) pausePlayback();

    {   // Control: on the focused viewport the same keys do run, so the checks below are not vacuous.
        // V twice and Down then Up leave flip and the track as they were.
        QStringList bad;
        if (!focusOn(viewport_)) bad << "viewport has no focus";
        const int inBefore = getInPoint();
        for (auto [k, t, id] : {std::tuple{Qt::Key_F, "f", "plate.fit"}, std::tuple{Qt::Key_V, "v", "plate.flip"},
                                std::tuple{Qt::Key_V, "v", "plate.flip"}, std::tuple{Qt::Key_I, "i", "inout.set-in"},
                                std::tuple{Qt::Key_Down, "", "plate.track-next"}, std::tuple{Qt::Key_Up, "", "plate.track-prev"}}) {
            ran.clear();
            press(k, QString::fromLatin1(t));
            if (!ran.contains(QLatin1String(id))) bad << QString("%1 did not run on the viewport").arg(id);
        }
        setInPoint(inBefore);
        report("CONTROL viewport", bad.isEmpty(), bad.isEmpty() ? QString("F V I Down Up run their commands") : bad.join("; "));
    }

    {   // Playlist list focused: Down moves the selection, the plate's track stays.
        QStringList bad;
        clearPlaylist();
        addPlaylistFile(QDir(tmp.path()).filePath("seq.0001.png").toStdString());
        addPlaylistFile(QDir(tmp.path()).filePath("seq.0003.png").toStdString());
        playlistPanelWidget_->refreshList();
        playlistDock_->show();
        auto* list = playlistPanelWidget_->findChild<QListWidget*>("playlist.list");
        if (!list || list->count() < 2) {
            bad << QString("playlist has %1 items").arg(list ? list->count() : -1);
        } else {
            list->setCurrentRow(0);
            if (!focusOn(list)) bad << "list has no focus";
            const int trackBefore = getTrackOnPlate(getActivePlate());
            ran.clear();
            press(Qt::Key_Down, QString());
            if (list->currentRow() != 1) bad << QString("selection row %1, not 1").arg(list->currentRow());
            if (getTrackOnPlate(getActivePlate()) != trackBefore) bad << "active plate's track changed";
            if (!ran.isEmpty()) bad << "ran " + ran.join(",");
        }
        clearPlaylist();
        playlistPanelWidget_->refreshList();
        report("PLAYLIST down", bad.isEmpty(), bad.isEmpty() ? QString("row 0 -> 1, track unchanged, no command") : bad.join("; "));
    }

    {   // Render dialog output path focused: I types, the In point stays.
        QStringList bad;
        setInPoint(getFromFrame());
        stepFrame(+1);
        stepFrame(+1);
        settleFor(50);
        const int inBefore = getInPoint();
        if (getCurrentFrame() == inBefore) bad << "playhead at the in point (check would be vacuous)";
        RenderDialog_Qt dlg(this);
        dlg.show();
        settleFor(100);
        auto* path = dlg.findChild<QLineEdit*>("dialog.render.path.edit");
        if (!path) {
            bad << "no dialog.render.path.edit";
        } else {
            path->setText("/tmp/out");
            if (!focusOn(path)) bad << "path edit has no focus";
            path->end(false);
            ran.clear();
            press(Qt::Key_I, "i");
            if (!path->text().endsWith('i')) bad << QString("text is \"%1\"").arg(path->text());
            if (getInPoint() != inBefore) bad << QString("in point %1 -> %2").arg(inBefore).arg(getInPoint());
            if (!ran.isEmpty()) bad << "ran " + ran.join(",");
        }
        dlg.hide();
        report("RENDER-PATH i", bad.isEmpty(), bad.isEmpty() ? QString("text ends with i, in point %1 unchanged").arg(inBefore) : bad.join("; "));
    }

    {   // Chat mode: the viewport claims every key, F goes to the message, Fit does not run.
        // remoteChatBegin only flips the compose flag, so no connection is needed.
        QStringList bad;
        if (!focusOn(viewport_)) bad << "viewport has no focus";
        remoteChatCancel();
        remoteChatBegin();
        ran.clear();
        press(Qt::Key_F, "f");
        const QString text = QString::fromStdString(remoteChatText());
        if (!text.endsWith('f')) bad << QString("chat text is \"%1\"").arg(text);
        if (!ran.isEmpty()) bad << "ran " + ran.join(",");
        remoteChatCancel();
        report("CHAT f", bad.isEmpty(), bad.isEmpty() ? QString("chat buffer \"f\", plate.fit not run") : bad.join("; "));
    }

    {   // Viewport focused: Space toggles playback.
        QStringList bad;
        if (!focusOn(viewport_)) bad << "viewport has no focus";
        const bool before = isPlaying();
        ran.clear();
        press(Qt::Key_Space, " ");
        settleFor(50);
        const bool after = isPlaying();
        if (after == before) bad << QString("isPlaying stayed %1").arg(before);
        if (after) pausePlayback();
        settleFor(50);
        report("VIEWPORT space", bad.isEmpty(), bad.isEmpty() ? QString("isPlaying %1 -> %2 (%3)").arg(before).arg(after).arg(ran.join(",")) : bad.join("; "));
    }

    const bool timelineWasFloating = timelineDock_->isFloating();
    {   // Docked Timeline: a click on the scrubber or the tracks leaves the viewport focused, so
        // a W hold still registers on it (hold drags, Return-to-chat and Escape need that).
        QStringList bad;
        timelineDock_->show();
        timelineDock_->setFloating(false);
        settleFor(300);
        for (QWidget* w : {static_cast<QWidget*>(timelinePanelWidget_->scrubber()),
                           static_cast<QWidget*>(timelinePanelWidget_->tracks())}) {
            if (!focusOn(viewport_)) bad << "viewport has no focus";
            click(w, QPoint(2, w->height() / 2));
            QWidget* fw = QApplication::focusWidget();
            if (fw != viewport_)
                bad << QString("after clicking %1 focus is %2").arg(w->objectName(), fw ? fw->objectName() : "none");
            QKeyEvent down(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier, "w");
            if (fw) QApplication::sendEvent(fw, &down);
            if (!viewport_->isHolding("color.drag-gamma")) bad << QString("W hold not seen after clicking %1").arg(w->objectName());
            QKeyEvent up(QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier, "w");
            if (fw) QApplication::sendEvent(fw, &up);
            settleFor(30);
        }
        if (isPlaying()) pausePlayback();
        report("DOCKED-TIMELINE click", bad.isEmpty(),
               bad.isEmpty() ? QString("scrubber and tracks clicks keep viewport focus, W hold registers") : bad.join("; "));
    }

    {   // Floated Timeline: a click gives the scrubber focus and it gets Player keys (Review Focus 1).
        QStringList bad;
        const bool wasFloating = timelineWasFloating;
        timelineDock_->show();
        timelineDock_->setFloating(true);
        settleFor(300);
        if (isPlaying()) pausePlayback();
        auto* scrub = timelinePanelWidget_->scrubber();
        if (!timelineDock_->isFloating()) bad << "dock did not float";
        makeActive(scrub->window());
        click(scrub, QPoint(2, scrub->height() / 2));
        if (QApplication::focusWidget() != scrub)
            bad << QString("click did not give the scrubber focus (focus is %1)")
                       .arg(QApplication::focusWidget() ? QApplication::focusWidget()->objectName() : "none");
        rewindPlayback();
        settleFor(50);
        const int before = getCurrentFrame();
        ran.clear();
        press(Qt::Key_Right, QString());
        settleFor(50);
        const int after = getCurrentFrame();
        if (after != before + 1) bad << QString("frame %1 -> %2").arg(before).arg(after);
        if (!ran.contains("playback.step-forward")) bad << "step-forward did not run";
        timelineDock_->setFloating(wasFloating);
        settleFor(200);
        if (!wasFloating && QApplication::focusWidget() == scrub) bad << "scrubber kept focus after docking";
        report("FLOATING-TIMELINE right", bad.isEmpty(), bad.isEmpty() ? QString("frame %1 -> %2").arg(before).arg(after) : bad.join("; "));
    }

    // The FX dock has no text field; the Remote Session dock is the floating dock with one.
    const bool remoteWasFloating = remoteDock_->isFloating();
    const bool remoteWasVisible = remoteDock_->isVisible();
    remoteDock_->show();
    remoteDock_->setFloating(true);
    settleFor(300);
    QLineEdit* dockEdit = firstEditableLineEdit(remoteDock_);

    {   // Floating dock text field: the menu-hosted letters type (Review Focus 4).
        QStringList bad;
        if (!remoteDock_->isFloating()) bad << "dock did not float";
        if (!dockEdit) {
            bad << "no editable QLineEdit in the Remote dock";
        } else {
            const QString saved = dockEdit->text();
            struct L { Qt::Key k; const char* t; };
            for (L l : {L{Qt::Key_F, "f"}, L{Qt::Key_H, "h"}, L{Qt::Key_M, "m"}, L{Qt::Key_T, "t"}, L{Qt::Key_V, "v"}}) {
                dockEdit->clear();
                if (!focusOn(dockEdit)) { bad << QString("%1: edit has no focus").arg(l.t); continue; }
                ran.clear();
                press(l.k, l.t);
                if (dockEdit->text() != QLatin1String(l.t)) bad << QString("%1: text \"%2\"").arg(l.t, dockEdit->text());
                if (!ran.isEmpty()) bad << QString("%1: ran %2").arg(l.t, ran.join(","));
            }
            dockEdit->setText(saved);
        }
        report("FLOATING-DOCK letters", bad.isEmpty(),
               bad.isEmpty() ? QString("F H M T V typed into %1, no command ran").arg(dockEdit->objectName()) : bad.join("; "));
    }

    if (!realKeys) {
        skip("REAL-KEY f", "pass --real-keys to send one through System Events");
    } else if (!dockEdit) {
        report("REAL-KEY f", false, "no editable QLineEdit in the Remote dock");
    } else {
        // One real keystroke through System Events. A synthetic QKeyEvent skips the native
        // menu bar's key-equivalent pass; this one goes through it.
        auto osa = [](const QString& script, QString* out = nullptr) {
            QProcess p;
            p.start("osascript", {"-e", script});
            while (p.state() != QProcess::NotRunning) settleFor(20);
            if (out) *out = QString::fromLocal8Bit(p.readAllStandardOutput() + p.readAllStandardError()).trimmed();
            return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
        };
        const QString saved = dockEdit->text();
        const QString pid = QString::number(QCoreApplication::applicationPid());
        osa(QString("tell application \"System Events\" to set frontmost of "
                    "(first process whose unix id is %1) to true").arg(pid));
        settleFor(300);
        dockEdit->clear();
        focusOn(dockEdit);
        settleFor(200);
        // The keystroke goes to the frontmost app: never send it unless that is us.
        QString frontPid, keyOut;
        osa("tell application \"System Events\" to get unix id of (first process whose frontmost is true)", &frontPid);
        if (frontPid != pid) {
            skip("REAL-KEY f", QString("JefeCheck is not frontmost (%1)").arg(frontPid));
        } else if (QApplication::focusWidget() != dockEdit) {
            report("REAL-KEY f", false, "edit has no focus");
        } else {
            ran.clear();
            if (!osa("tell application \"System Events\" to keystroke \"f\"", &keyOut)) {
                skip("REAL-KEY f", "osascript keystroke failed: " + keyOut);
            } else {
                settleFor(500);
                QStringList bad;
                if (dockEdit->text() != QLatin1String("f")) bad << QString("text \"%1\"").arg(dockEdit->text());
                if (!ran.isEmpty()) bad << "ran " + ran.join(",");
                report("REAL-KEY f", bad.isEmpty(),
                       bad.isEmpty() ? QString("typed into %1, plate.fit not run").arg(dockEdit->objectName()) : bad.join("; "));
                // Control: the same real keystroke on the viewport does run Fit.
                focusOn(viewport_);
                ran.clear();
                osa("tell application \"System Events\" to keystroke \"f\"");
                settleFor(500);
                report("REAL-KEY f control", ran.contains("plate.fit"),
                       ran.contains("plate.fit") ? QString("viewport focused: plate.fit ran")
                                                 : QString("viewport focused: ran [%1]").arg(ran.join(",")));
            }
        }
        dockEdit->setText(saved);
    }

    remoteDock_->setFloating(remoteWasFloating);
    if (!remoteWasVisible) remoteDock_->hide();
    settleFor(100);

    disconnect(spy);
    printf("SHORTCUTS-FOCUS-TEST %s\n", allPass ? "PASS" : "FAIL");
    fflush(stdout);
    return allPass ? 0 : 2;
}
