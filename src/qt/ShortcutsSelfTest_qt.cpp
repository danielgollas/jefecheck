// SPDX-License-Identifier: GPL-2.0-or-later
// Headless self-test for the shortcut registry (--shortcuts-test). Needs a QApplication
// (QAction) but no window.
#include "Shortcuts_qt.h"

#include <QAction>
#include <QWidget>
#include <QtGlobal>

#include <cstdio>

namespace jefe::shortcuts {

namespace {

int g_fails = 0;

// Counts qWarning()s instead of printing them, so expected warnings stay off the output.
int g_warnings = 0;
void countWarnings(QtMsgType type, const QMessageLogContext&, const QString&)
{
    if (type == QtWarningMsg) ++g_warnings;
}

struct CaptureWarnings {
    QtMessageHandler previous;
    CaptureWarnings() : previous(qInstallMessageHandler(countWarnings)) { g_warnings = 0; }
    ~CaptureWarnings() { qInstallMessageHandler(previous); }
};

void check(const char* name, bool cond, const QString& detail = QString())
{
    std::printf("SHORTCUTS-TEST %s %s %s\n", name, cond ? "PASS" : "FAIL",
                detail.toUtf8().constData());
    if (!cond) ++g_fails;
}

QString keysText(const QList<QKeySequence>& ks)
{
    QStringList out;
    for (const QKeySequence& k : ks) out << k.toString(QKeySequence::PortableText);
    return out.join(QStringLiteral(" | "));
}

Command cmd(const char* id, Scope scope, QList<QKeySequence> editing,
            QList<QKeySequence> classic = {}, std::function<void()> run = {})
{
    return Command{id, QString::fromUtf8(id), Category::Playback, scope,
                   std::move(editing), std::move(classic), std::move(run)};
}

void testIdsUnique(Registry& reg)
{
    reg.reset();
    reg.registerCommand(cmd("t.a", Scope::Player, {QKeySequence("I")}));
    int warnings = 0;
    {
        CaptureWarnings capture;
        reg.registerCommand(cmd("t.a", Scope::Global, {QKeySequence("J")}));
        warnings = g_warnings;
    }
    const bool ok = reg.commands().size() == 1 && reg.key("t.a") == QKeySequence("I") &&
                    reg.commands().first().scope == Scope::Player && warnings == 1;
    check("ids-unique", ok, QStringLiteral("commands=%1 warnings=%2 key=%3")
                                .arg(reg.commands().size())
                                .arg(warnings)
                                .arg(reg.key("t.a").toString(QKeySequence::PortableText)));
}

void testActionKey(Registry& reg)
{
    reg.reset();
    reg.registerCommand({"t.a", QStringLiteral("A"), Category::Playback, Scope::Player,
                         {QKeySequence("Space")}, {QKeySequence("Space")}, [] {}});
    QAction* a = reg.action("t.a");
    const bool ok = a && a->shortcut() == QKeySequence("Space") &&
                    a->objectName() == QStringLiteral("shortcut.t.a") &&
                    a->text() == QStringLiteral("A") &&
                    a->shortcutContext() == Qt::WidgetShortcut;
    check("action-key", ok,
          a ? QStringLiteral("shortcut=%1 objectName=%2 text=%3")
                  .arg(a->shortcut().toString(QKeySequence::PortableText), a->objectName(), a->text())
            : QStringLiteral("no action"));

    reg.registerCommand(cmd("t.g", Scope::Global, {QKeySequence("F")}));
    QAction* g = reg.action("t.g");
    check("action-context-global", g && g->shortcutContext() == Qt::ApplicationShortcut);
    check("action-unknown-null", reg.action("t.nope") == nullptr);
}

void testTwoKeys(Registry& reg)
{
    reg.reset();
    reg.registerCommand(cmd("t.a", Scope::Player, {QKeySequence(","), QKeySequence("<")}));
    QAction* a = reg.action("t.a");
    const bool ok = a && a->shortcuts().size() == 2 && reg.keys("t.a").size() == 2 &&
                    reg.key("t.a") == QKeySequence(",");
    check("two-keys", ok, a ? keysText(a->shortcuts()) : QStringLiteral("no action"));
}

void testPresetSwitch(Registry& reg)
{
    reg.reset();
    reg.registerCommand(cmd("t.a", Scope::Player, {QKeySequence("I")}, {QKeySequence("Shift+I")}));
    int changes = 0;
    auto conn = QObject::connect(&reg, &Registry::changed, [&changes] { ++changes; });
    const bool startsEditing = reg.preset() == QStringLiteral("editing") &&
                               reg.key("t.a") == QKeySequence("I");
    reg.setPreset(QStringLiteral("classic"));
    QObject::disconnect(conn);
    const bool ok = startsEditing && reg.preset() == QStringLiteral("classic") &&
                    reg.key("t.a") == QKeySequence("Shift+I") &&
                    reg.action("t.a")->shortcut() == QKeySequence("Shift+I") && changes == 1;
    check("preset-switch", ok, QStringLiteral("key=%1 changed=%2")
                                   .arg(reg.key("t.a").toString(QKeySequence::PortableText))
                                   .arg(changes));
}

void testConflictScope(Registry& reg)
{
    reg.reset();
    reg.registerCommand(cmd("t.a", Scope::Player, {QKeySequence("I")}));
    reg.registerCommand(cmd("t.b", Scope::Player, {QKeySequence("I")}));
    reg.registerCommand(cmd("t.g", Scope::Global, {QKeySequence("F")}));
    reg.registerCommand(cmd("t.p", Scope::Player, {QKeySequence("P")}));

    const QStringList same = reg.conflicts("t.b", QKeySequence("I"));
    check("conflict-scope same-scope", same == QStringList{QStringLiteral("t.a")}, same.join(','));

    const QStringList cross = reg.conflicts("t.p", QKeySequence("F"));
    check("conflict-scope global-vs-player", cross == QStringList{QStringLiteral("t.g")},
          cross.join(','));

    const QStringList none = reg.conflicts("t.p", QKeySequence("O"));
    check("conflict-scope none", none.isEmpty(), none.join(','));

    check("command-for-key", reg.commandForKey(Scope::Global, QKeySequence("F")) == "t.g" &&
                                 reg.commandForKey(Scope::Player, QKeySequence("F")).isEmpty() &&
                                 reg.commandForKey(Scope::Player, QKeySequence("P")) == "t.p");
}

void testRunSignal(Registry& reg)
{
    reg.reset();
    int runs = 0;
    reg.registerCommand(cmd("t.a", Scope::Player, {QKeySequence("I")}, {}, [&runs] { ++runs; }));
    QString ranId;
    Category ranCat = Category::Help;
    int ranCount = 0;
    auto conn = QObject::connect(&reg, &Registry::commandRan,
                                 [&](const QString& id, Category c) { ranId = id; ranCat = c; ++ranCount; });
    reg.action("t.a")->trigger();
    const bool ok = runs == 1 && ranCount == 1 && ranId == QStringLiteral("t.a") &&
                    ranCat == Category::Playback;
    check("run-signal", ok, QStringLiteral("runs=%1 ran=%2 id=%3").arg(runs).arg(ranCount).arg(ranId));

    int rebound = 0;
    reg.bindRun("t.a", [&rebound] { ++rebound; });
    reg.action("t.a")->trigger();
    QObject::disconnect(conn);
    check("bind-run", runs == 1 && rebound == 1 && ranCount == 2,
          QStringLiteral("old=%1 new=%2").arg(runs).arg(rebound));
}

void testAttach(Registry& reg)
{
    reg.reset();
    reg.registerCommand(cmd("t.g", Scope::Global, {QKeySequence("F")}));
    reg.registerCommand(cmd("t.p", Scope::Player, {QKeySequence("I")}));
    QWidget window;
    QWidget playerA, playerB;
    reg.attach(&window, {&playerA, &playerB});
    QAction* g = reg.action("t.g");
    QAction* p = reg.action("t.p");
    const bool ok = window.actions() == QList<QAction*>{g} &&
                    playerA.actions() == QList<QAction*>{p} &&
                    playerB.actions() == QList<QAction*>{p};
    check("attach", ok, QStringLiteral("window=%1 playerA=%2 playerB=%3")
                            .arg(window.actions().size())
                            .arg(playerA.actions().size())
                            .arg(playerB.actions().size()));
}

void testLabel(Registry& reg)
{
    reg.reset();
    reg.registerCommand(cmd("t.a", Scope::Player, {QKeySequence("Shift+I")}));
    reg.registerCommand(cmd("t.u", Scope::Player, {}));
    const QString want = reg.key("t.a").toString(QKeySequence::NativeText);
    const bool ok = !want.isEmpty() && reg.label("t.a") == want && reg.label("t.u").isEmpty();
    check("label", ok, QStringLiteral("label=%1 unbound=%2").arg(reg.label("t.a"), reg.label("t.u")));
}

void testHoldKey(Registry& reg)
{
    reg.reset();
    reg.registerHoldKey({"t.h", QStringLiteral("Hold"), Qt::Key_G, Qt::Key_unknown, {}});
    int warnings = 0;
    {
        CaptureWarnings capture;
        reg.registerHoldKey({"t.h", QStringLiteral("Dup"), Qt::Key_E, Qt::Key_E, {}});
        warnings = g_warnings;
    }
    const bool editingOk =
        reg.holdKey("t.h") == Qt::Key_G && reg.holdKeys().size() == 1 && warnings == 1;
    reg.setPreset(QStringLiteral("classic"));
    check("hold-key", editingOk && reg.holdKey("t.h") == Qt::Key_unknown &&
                          reg.holdKey("t.nope") == Qt::Key_unknown);
}

void testReset(Registry& reg)
{
    reg.reset();
    reg.registerCommand(cmd("t.a", Scope::Player, {QKeySequence("I")}));
    reg.registerHoldKey({"t.h", QStringLiteral("Hold"), Qt::Key_G, Qt::Key_G, {}});
    reg.setOverride("t.a", QKeySequence("K"));
    reg.setPreset(QStringLiteral("classic"));
    QAction* before = reg.action("t.a");
    bool destroyed = false;
    QObject::connect(before, &QObject::destroyed, [&destroyed] { destroyed = true; });
    reg.reset();
    const bool ok = reg.commands().isEmpty() && reg.holdKeys().isEmpty() && !reg.isCustom() &&
                    reg.preset() == QStringLiteral("editing") && reg.action("t.a") == nullptr &&
                    destroyed;
    check("reset", ok);
}

}  // namespace

int shortcutsSelfTest()
{
    g_fails = 0;
    Registry& reg = Registry::instance();
    testIdsUnique(reg);
    testActionKey(reg);
    testTwoKeys(reg);
    testPresetSwitch(reg);
    testConflictScope(reg);
    testRunSignal(reg);
    testAttach(reg);
    testLabel(reg);
    testHoldKey(reg);
    testReset(reg);
    reg.reset();
    std::printf("SHORTCUTS-TEST %s\n", g_fails == 0 ? "PASS" : "FAIL");
    return g_fails;
}

}  // namespace jefe::shortcuts
