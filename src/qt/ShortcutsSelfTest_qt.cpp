// SPDX-License-Identifier: GPL-2.0-or-later
// Headless self-test for the shortcut registry (--shortcuts-test). Needs a QApplication
// (QAction) but no window.
#include "Shortcuts_qt.h"

#include <QAction>
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>
#include <QWidget>
#include <QtGlobal>

#include <cstdio>
#include <utility>

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

// ---- Task 2: overrides, persistence, import / export -------------------------------

QString tempPath(const char* suffix)
{
    return QDir::tempPath() + QStringLiteral("/jefe-shortcuts-test-") +
           QUuid::createUuid().toString(QUuid::Id128) + QString::fromLatin1(suffix);
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(data);
    return true;
}

bool oneLine(const QString& s) { return !s.isEmpty() && !s.contains(QLatin1Char('\n')); }

void registerTwo(Registry& reg)
{
    reg.reset();
    reg.registerCommand(cmd("t.a", Scope::Player, {QKeySequence("I")}, {QKeySequence("Shift+I")}));
    reg.registerCommand(cmd("t.b", Scope::Player, {QKeySequence("O")}, {QKeySequence("Shift+O")}));
    reg.registerCommand(cmd("t.g", Scope::Global, {QKeySequence("Ctrl+F")}));
    reg.registerHoldKey({"t.h", QStringLiteral("Hold"), Qt::Key_W, Qt::Key_G, {}});
}

QString allKeys(Registry& reg)
{
    QStringList out;
    for (const Command& c : reg.commands()) out << QString::fromUtf8(c.id) + "=" + keysText(reg.keys(c.id));
    for (const HoldKey& h : reg.holdKeys())
        out << QString::fromUtf8(h.id) + "=" + QString::number(int(reg.holdKey(h.id)));
    return out.join(';') + "|" + reg.preset();
}

void testOverrideSignals(Registry& reg)
{
    registerTwo(reg);
    int changes = 0;
    auto conn = QObject::connect(&reg, &Registry::changed, [&changes] { ++changes; });
    reg.setOverride("t.a", QKeySequence("K"));
    const bool setOk = reg.key("t.a") == QKeySequence("K") &&
                       reg.action("t.a")->shortcut() == QKeySequence("K") && changes == 1 &&
                       reg.isCustom();
    reg.clearOverride("t.a");
    QObject::disconnect(conn);
    check("override-set-clear", setOk && reg.key("t.a") == QKeySequence("I") &&
                                    reg.action("t.a")->shortcut() == QKeySequence("I") &&
                                    changes == 2 && !reg.isCustom(),
          QStringLiteral("changes=%1").arg(changes));
}

void testPresetSame(Registry& reg)
{
    registerTwo(reg);
    reg.setOverride("t.a", QKeySequence("K"));
    int changes = 0;
    auto conn = QObject::connect(&reg, &Registry::changed, [&changes] { ++changes; });
    reg.setPreset(QStringLiteral("editing"));
    const bool same = changes == 0 && reg.key("t.a") == QKeySequence("K");
    reg.setPreset(QStringLiteral("classic"));
    QObject::disconnect(conn);
    check("preset-same-noop", same && changes == 1 && reg.key("t.a") == QKeySequence("Shift+I") &&
                                  !reg.isCustom(),
          QStringLiteral("changes=%1").arg(changes));
}

void testOverrideDropEqual(Registry& reg)
{
    registerTwo(reg);
    reg.setOverride("t.a", QKeySequence("I"));
    const bool a = !reg.isCustom();
    reg.setOverride("t.a", QKeySequence("K"));
    reg.setOverride("t.a", QKeySequence("I"));
    check("override-drop-equal", a && !reg.isCustom());
}

void testOverrideUnbind(Registry& reg)
{
    registerTwo(reg);
    reg.setOverride("t.a", QKeySequence());
    const bool unbound = reg.keys("t.a").isEmpty() && reg.action("t.a")->shortcuts().isEmpty();
    const QString path = tempPath(".ini");
    {
        QSettings s(path, QSettings::IniFormat);
        reg.save(s);
        s.sync();
    }
    QSettings s(path, QSettings::IniFormat);
    const bool saved = s.contains("Shortcuts/overrides/t.a") &&
                       s.value("Shortcuts/overrides/t.a").toString().isEmpty();
    registerTwo(reg);
    reg.load(s);
    const bool reloaded = reg.keys("t.a").isEmpty() && reg.isCustom();
    QFile::remove(path);
    check("override-unbind", unbound && saved && reloaded);
}

void testOverrideUnknownKept(Registry& reg)
{
    registerTwo(reg);
    const QString path = tempPath(".ini");
    const QString out = tempPath(".ini");
    {
        QSettings s(path, QSettings::IniFormat);
        s.setValue("Shortcuts/overrides/gone.id", "Q");
        s.setValue("Shortcuts/holdOverrides/gone.hold", "Z");
        s.sync();
    }
    QSettings in(path, QSettings::IniFormat);
    reg.load(in);
    bool kept = false, keptHold = false;
    {
        QSettings o(out, QSettings::IniFormat);
        reg.save(o);
        o.sync();
        kept = o.value("Shortcuts/overrides/gone.id").toString() == "Q";
        keptHold = o.value("Shortcuts/holdOverrides/gone.hold").toString() == "Z";
    }
    QFile::remove(path);
    QFile::remove(out);
    check("override-unknown-kept", kept && keptHold && !reg.isCustom(),
          QStringLiteral("kept=%1 hold=%2").arg(kept).arg(keptHold));
}

void testPersistRoundtrip(Registry& reg)
{
    registerTwo(reg);
    reg.setPreset(QStringLiteral("classic"));
    reg.setOverride("t.a", QKeySequence("Ctrl+L"));
    reg.setOverride("t.g", QKeySequence("Meta+Ctrl+F"));
    reg.setHoldOverride("t.h", Qt::Key_E);
    const QString before = allKeys(reg);
    const QString path = tempPath(".ini");
    {
        QSettings s(path, QSettings::IniFormat);
        reg.save(s);
        s.sync();
    }
    registerTwo(reg);
    QSettings s(path, QSettings::IniFormat);
    reg.load(s);
    const QString after = allKeys(reg);
    const bool actionOk = reg.action("t.a")->shortcut() == QKeySequence("Ctrl+L");
    QFile::remove(path);
    check("persist-roundtrip", before == after && actionOk && before.contains("classic"),
          before + " vs " + after);
}

void testHoldOverride(Registry& reg)
{
    registerTwo(reg);
    int changes = 0;
    auto conn = QObject::connect(&reg, &Registry::changed, [&changes] { ++changes; });
    reg.setHoldOverride("t.h", Qt::Key_E);
    const bool set = reg.holdKey("t.h") == Qt::Key_E && reg.isCustom() && changes == 1;
    reg.setHoldOverride("t.h", Qt::Key_W);  // equal to the preset: dropped
    const bool dropped = reg.holdKey("t.h") == Qt::Key_W && !reg.isCustom();
    reg.setHoldOverride("t.h", Qt::Key_unknown);
    const bool unbound = reg.holdKey("t.h") == Qt::Key_unknown && reg.isCustom();
    reg.clearHoldOverride("t.h");
    QObject::disconnect(conn);
    check("hold-override", set && dropped && unbound && reg.holdKey("t.h") == Qt::Key_W);
}

void testSnapshotRestore(Registry& reg)
{
    registerTwo(reg);
    reg.setOverride("t.a", QKeySequence("K"));
    reg.setHoldOverride("t.h", Qt::Key_E);
    const QString before = allKeys(reg);
    const Registry::Snapshot snap = reg.snapshot();
    reg.setPreset(QStringLiteral("classic"));
    reg.setOverride("t.b", QKeySequence("L"));
    const bool changed = allKeys(reg) != before;
    int changes = 0;
    auto conn = QObject::connect(&reg, &Registry::changed, [&changes] { ++changes; });
    reg.restore(snap);
    QObject::disconnect(conn);
    check("snapshot-restore", changed && allKeys(reg) == before && changes == 1 &&
                                  reg.action("t.a")->shortcut() == QKeySequence("K"),
          allKeys(reg));
}

void testExportImport(Registry& reg)
{
    registerTwo(reg);
    reg.setOverride("t.a", QKeySequence("K"));
    reg.setHoldOverride("t.h", Qt::Key_E);
    const QString before = allKeys(reg);
    const QString path = tempPath(".jcshortcuts");
    const bool exported = reg.exportTo(path);
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QJsonObject keys = o.value("keys").toObject();
    bool all = true;
    for (const Command& c : reg.commands()) all = all && keys.contains(QString::fromUtf8(c.id));
    const bool fileOk = exported && o.value("format").toString() == "jefecheck-shortcuts" &&
                        o.value("version").toInt() == 1 && o.value("basedOn").toString() == "editing" &&
                        all && keys.size() == 3 && o.value("hold").toObject().value("t.h").toString() == "E";
    reg.setPreset(QStringLiteral("classic"));
    reg.setOverride("t.b", QKeySequence("L"));
    reg.setHoldOverride("t.h", Qt::Key_R);
    QString err = "x";
    const bool imported = reg.importFrom(path, &err);
    QFile::remove(path);
    check("export-import", fileOk && imported && err.isEmpty() && allKeys(reg) == before,
          allKeys(reg) + " vs " + before + " err=" + err);
}

void testImportPortable(Registry& reg)
{
    registerTwo(reg);
    const QString path = tempPath(".jcshortcuts");
    writeFile(path, R"({"format":"jefecheck-shortcuts","version":1,"basedOn":"editing",
        "keys":{"t.a":"Ctrl+L","t.b":"Meta+Ctrl+F"}})");
    QString err;
    const bool ok = reg.importFrom(path, &err);
    QFile::remove(path);
    check("import-portable", ok && reg.key("t.a") == QKeySequence(Qt::CTRL | Qt::Key_L) &&
                                 reg.key("t.b") == QKeySequence(Qt::META | Qt::CTRL | Qt::Key_F),
          keysText(reg.keys("t.a")) + " " + keysText(reg.keys("t.b")));
}

void testImportUnknownId(Registry& reg)
{
    registerTwo(reg);
    const QString path = tempPath(".jcshortcuts");
    writeFile(path, R"({"format":"jefecheck-shortcuts","version":1,"basedOn":"editing",
        "keys":{"t.a":"K","no.such":"Q","no.other":"Z"}})");
    QString err;
    const bool ok = reg.importFrom(path, &err);
    QFile::remove(path);
    check("import-unknown-id", ok && oneLine(err) && err.contains("2") && reg.key("t.a") == QKeySequence("K"),
          err);
}

void testImportBad(Registry& reg)
{
    const char* cases[] = {
        "{not json",
        R"({"format":"other","version":1,"keys":{}})",
        R"({"format":"jefecheck-shortcuts","version":2,"keys":{}})",
        "",
        R"({"format":"jefecheck-shortcuts","version":1,"basedOn":"classic","keys":{"t.a":"Ctrl+Nonsense+"}})",
        R"({"format":"jefecheck-shortcuts","version":1,"keys":{"t.a":"K"},"hold":{"t.h":"Ctrl+Nonsense+"}})",
    };
    int n = 0;
    for (const char* c : cases) {
        registerTwo(reg);
        reg.setOverride("t.a", QKeySequence("J"));
        const QString before = allKeys(reg);
        const QString path = tempPath(".jcshortcuts");
        writeFile(path, c);
        QString err;
        const bool ok = reg.importFrom(path, &err);
        QFile::remove(path);
        check(("import-bad-" + QString::number(n++)).toUtf8().constData(),
              !ok && oneLine(err) && allKeys(reg) == before, err);
    }
    registerTwo(reg);
    QString err;
    check("import-bad-missing-file", !reg.importFrom(tempPath(".nope"), &err) && oneLine(err), err);
}

void testImportConflict(Registry& reg)
{
    registerTwo(reg);
    const QString path = tempPath(".jcshortcuts");
    writeFile(path, R"({"format":"jefecheck-shortcuts","version":1,"basedOn":"editing",
        "keys":{"t.a":"Y","t.b":"Y"}})");
    QString err;
    const bool ok = reg.importFrom(path, &err);
    QFile::remove(path);
    const QStringList c = reg.conflicts("t.a", QKeySequence("Y"));
    check("import-conflict", ok && reg.key("t.a") == QKeySequence("Y") &&
                                 reg.key("t.b") == QKeySequence("Y") &&
                                 c == QStringList{QStringLiteral("t.b")},
          c.join(','));
}

void testExportTwoKeysAndUnbound(Registry& reg)
{
    registerTwo(reg);
    reg.registerCommand(cmd("t.two", Scope::Player, {QKeySequence("A"), QKeySequence("B")}));
    reg.registerCommand(cmd("t.u", Scope::Player, {QKeySequence("U")}));
    reg.setOverride("t.u", QKeySequence());
    const QString before = allKeys(reg);
    const QString path = tempPath(".jcshortcuts");
    const bool exported = reg.exportTo(path);
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    const QJsonObject keys = QJsonDocument::fromJson(f.readAll()).object().value("keys").toObject();
    f.close();
    const QJsonValue two = keys.value("t.two"), un = keys.value("t.u");
    const bool json = exported && two.isArray() && two.toArray().size() == 2 && un.isString() &&
                      un.toString().isEmpty() && keys.contains("t.u");
    reg.setOverride("t.two", QKeySequence("Z"));
    reg.setOverride("t.u", QKeySequence("Y"));
    QString err;
    const bool imported = reg.importFrom(path, &err);
    QFile::remove(path);
    check("export-two-keys-unbound",
          json && imported && reg.keys("t.two") == QList<QKeySequence>{QKeySequence("A"), QKeySequence("B")} &&
              reg.keys("t.u").isEmpty() && allKeys(reg) == before,
          allKeys(reg) + " err=" + err);
}

void testPersistComma(Registry& reg)
{
    registerTwo(reg);
    reg.registerCommand(cmd("t.c", Scope::Player, {QKeySequence("C")}));
    reg.registerCommand(cmd("t.d", Scope::Player, {QKeySequence("D")}));
    reg.setOverride("t.c", QKeySequence("Ctrl+,"));
    reg.setOverride("t.d", QKeySequence(","));
    reg.setHoldOverride("t.h", Qt::Key_Comma);
    const QString before = allKeys(reg);
    const QString path = tempPath(".ini");
    {
        QSettings s(path, QSettings::IniFormat);
        reg.save(s);
        s.sync();
    }
    registerTwo(reg);
    reg.registerCommand(cmd("t.c", Scope::Player, {QKeySequence("C")}));
    reg.registerCommand(cmd("t.d", Scope::Player, {QKeySequence("D")}));
    QSettings s(path, QSettings::IniFormat);
    reg.load(s);
    const QString after = allKeys(reg);
    QFile::remove(path);
    check("persist-comma", before == after && reg.key("t.c") == QKeySequence("Ctrl+,") &&
                               reg.key("t.d") == QKeySequence(","),
          before + " vs " + after);
}

// Every key the app binds today (GlViewport_Qt::keyPressEvent, MainWindow_Qt's QShortcuts and
// menu keys) resolves to the registered command, in both presets.
struct ParityRow {
    Scope scope;
    const char* key;  // PortableText
    const char* id;
};

const ParityRow kParity20[] = {
    // Viewport keyPressEvent (Player).
    {Scope::Player, "Space", "playback.play-pause"},
    {Scope::Player, "Left", "playback.step-back"},
    {Scope::Player, "X", "playback.step-back"},
    {Scope::Player, "Right", "playback.step-forward"},
    {Scope::Player, "C", "playback.step-forward"},
    {Scope::Player, "Home", "playback.go-start"},
    {Scope::Player, "End", "playback.go-end"},
    {Scope::Player, ",", "playback.direction-reverse"},
    {Scope::Player, "<", "playback.direction-reverse"},
    {Scope::Player, ".", "playback.direction-forward"},
    {Scope::Player, ">", "playback.direction-forward"},
    {Scope::Player, "8", "playback.loop-once"},
    {Scope::Player, "9", "playback.loop"},
    {Scope::Player, "0", "playback.bounce"},
    {Scope::Player, "I", "inout.set-in"},
    {Scope::Player, "O", "inout.set-out"},
    {Scope::Player, "Shift+I", "inout.reset-in"},
    {Scope::Player, "Shift+O", "inout.reset-out"},
    {Scope::Player, "Alt+I", "inout.set-in-load"},
    {Scope::Player, "Up", "plate.track-prev"},
    {Scope::Player, "Down", "plate.track-next"},
    // Layouts (MainWindow QShortcuts and viewport Ctrl+1-4; registered once, Global).
    {Scope::Global, "Ctrl+1", "layout.single"},
    {Scope::Global, "Ctrl+2", "layout.side"},
    {Scope::Global, "Ctrl+3", "layout.top-bottom"},
    {Scope::Global, "Ctrl+4", "layout.quad"},
    // Plate transforms: View menu (active) and QShortcuts (all plates).
    {Scope::Global, "F", "plate.fit"},
    {Scope::Global, "Shift+F", "plate.fit-all"},
    {Scope::Global, "V", "plate.flip"},
    {Scope::Global, "Shift+V", "plate.flip-all"},
    {Scope::Global, "M", "plate.flop"},
    {Scope::Global, "Shift+M", "plate.flop-all"},
    {Scope::Global, "T", "plate.text-mode"},
    {Scope::Global, "Alt+T", "plate.text-mode-all"},
    {Scope::Global, "Ctrl+R", "plate.reset"},
    {Scope::Global, "Ctrl+Alt+R", "plate.reset-all"},
    {Scope::Global, "Shift+R", "color.reset"},
    {Scope::Global, "Shift+Alt+R", "color.reset-all"},
    // File menu.
    {Scope::Global, "Ctrl+O", "file.quick-load"},
    {Scope::Global, "Ctrl+L", "file.load-manager"},
    {Scope::Global, "Ctrl+Shift+O", "file.open-session"},
    {Scope::Global, "Ctrl+P", "file.preferences"},
    {Scope::Global, "Ctrl+,", "file.preferences"},
    // View menu.
    {Scope::Global, "F11", "view.toggle-fullscreen"},
    {Scope::Global, "Meta+Ctrl+F", "view.toggle-fullscreen"},
    {Scope::Global, "Ctrl+H", "view.toggle-histogram"},
    {Scope::Global, "Ctrl+Alt+H", "view.toggle-histogram-all"},
    {Scope::Global, "N", "view.toggle-note-overlay"},
    // Window menu.
    {Scope::Global, "F2", "panels.toggle-plate-manager"},
    {Scope::Global, "F3", "panels.toggle-fx"},
    {Scope::Global, "F4", "panels.toggle-luts"},
    {Scope::Global, "F5", "panels.open-remote"},
    {Scope::Global, "F6", "panels.open-render"},
    {Scope::Global, "F7", "panels.toggle-notes"},
    {Scope::Global, "F8", "panels.toggle-timeline"},
    {Scope::Global, "F9", "panels.toggle-playlist"},
    {Scope::Global, "Ctrl+Alt+F", "panels.hide-controls"},
#ifdef Q_OS_MACOS
    {Scope::Global, "Ctrl+M", "panels.minimize"},
#endif
    // Help menu.
    {Scope::Global, "F1", "help.manual"},
    {Scope::Global, "H", "help.toggle-overlay"},
};

void testParity20(Registry& reg)
{
    reg.reset();
    registerAllCommands(reg);
    QStringList bad;
    int rows = 0;
    for (const char* preset : {"editing", "classic"}) {
        reg.setPreset(QString::fromLatin1(preset));
        auto expect = [&](Scope scope, const QKeySequence& key, const char* id) {
            ++rows;
            const QString got = reg.commandForKey(scope, key);
            if (got != QString::fromLatin1(id))
                bad << QStringLiteral("%1:%2->%3(want %4)")
                           .arg(preset, key.toString(QKeySequence::PortableText), got, id);
        };
        for (const ParityRow& r : kParity20)
            expect(r.scope, QKeySequence::fromString(QString::fromLatin1(r.key), QKeySequence::PortableText), r.id);
        // Standard keys: exactly the one key the menu constructs today (QKeySequence(StandardKey)).
        for (const auto& [standardKey, id] : {std::pair{QKeySequence::Save, "file.save-session"},
                                      std::pair{QKeySequence::Quit, "file.quit"}}) {
            const QKeySequence menuKey(standardKey);
            expect(Scope::Global, menuKey, id);
            ++rows;
            if (reg.keys(id) != QList<QKeySequence>{menuKey})
                bad << QStringLiteral("%1:%2 keys=%3 (want only %4)")
                           .arg(preset, id, keysText(reg.keys(id)),
                                menuKey.toString(QKeySequence::PortableText));
        }
        const QPair<const char*, Qt::Key> holds[] = {
            {"color.drag-gamma", Qt::Key_W},      {"color.drag-exposure", Qt::Key_E},
            {"color.drag-brightness", Qt::Key_Q}, {"color.drag-contrast", Qt::Key_D},
            {"color.drag-saturation", Qt::Key_S},
        };
        for (const auto& h : holds) {
            ++rows;
            if (reg.holdKey(h.first) != h.second)
                bad << QStringLiteral("%1:hold %2=%3").arg(preset, h.first).arg(int(reg.holdKey(h.first)));
        }
    }
    // Phase 1: both presets carry today's keys.
    for (const Command& c : reg.commands()) {
        if (c.editing != c.classic) bad << QStringLiteral("presets differ: %1").arg(c.id);
        if (c.editing.size() > 2) bad << QStringLiteral(">2 keys: %1").arg(c.id);
    }
    reg.setPreset(QStringLiteral("editing"));
    check("parity-2.0", bad.isEmpty(),
          QStringLiteral("rows=%1 commands=%2 holds=%3 %4")
              .arg(rows).arg(reg.commands().size()).arg(reg.holdKeys().size()).arg(bad.join(' ')));
}

void testPresetsConflictFree(Registry& reg)
{
    reg.reset();
    registerAllCommands(reg);
    QStringList bad;
    for (const char* preset : {"editing", "classic"}) {
        reg.setPreset(QString::fromLatin1(preset));
        for (const Command& c : reg.commands())
            for (const QKeySequence& k : reg.keys(c.id)) {
                const QStringList others = reg.conflicts(c.id, k);
                if (!others.isEmpty())
                    bad << QStringLiteral("%1:%2 %3 vs %4")
                               .arg(preset, c.id, k.toString(QKeySequence::PortableText), others.join(','));
            }
    }
    reg.setPreset(QStringLiteral("editing"));
    check("presets-conflict-free", !reg.commands().isEmpty() && bad.isEmpty(),
          QStringLiteral("commands=%1 %2").arg(reg.commands().size()).arg(bad.join(' ')));
}

void testHelpText(Registry& reg)
{
    reg.reset();
    registerAllCommands(reg);
    const QList<HelpLine> lines = buildHelpLines(reg);
    const QString text = buildHelpText(reg);
    const QStringList headings = {"VIEWPORT LAYOUTS", "WINDOW / GUI", "WINDOW MENU", "PLAYBACK",
                                  "TRANSFORMS", "COLOR CORRECTION", "REMOTE SESSIONS"};
    QStringList missing;
    for (const QString& h : headings)
        if (!text.contains(h)) missing << h;
    // Every placed id: the line that shows it also shows its current key label.
    QStringList shown;
    for (const HelpLine& l : lines)
        for (const QString& id : l.ids) {
            shown << id;
            const QByteArray b = id.toUtf8();
            QString label = reg.key(b.constData()).toString(QKeySequence::PortableText);
            if (label.isEmpty()) {
                const Qt::Key k = reg.holdKey(b.constData());
                if (k != Qt::Key_unknown) label = QKeySequence(k).toString(QKeySequence::PortableText);
            }
            if (label.isEmpty() || !l.text.contains(label)) missing << id;
        }
    // Extras read "(+Shift = ...)" when they are the base key plus modifiers, else show their label.
    for (const HelpLine& l : lines)
        for (const QString& id : l.extraIds) {
            const QByteArray b = id.toUtf8();
            if (!l.text.contains(reg.key(b.constData()).toString(QKeySequence::PortableText)) && !l.text.contains("(+") && !l.text.contains(", +"))
                missing << id;
        }
    // Every command and hold key is in the overlay unless deliberately left out here.
    const QStringList notInOverlay = {"file.quick-load", "file.load-manager", "file.save-session",
                                      "file.open-session", "file.quit", "panels.minimize"};
    const QStringList placed = helpPlacedIds();
    QStringList unplaced, unknown;
    for (const Command& c : reg.commands()) {
        const QString id = QString::fromUtf8(c.id);
        if (!placed.contains(id) && !notInOverlay.contains(id)) unplaced << id;
    }
    for (const HoldKey& h : reg.holdKeys())
        if (!placed.contains(QString::fromUtf8(h.id))) unplaced << QString::fromUtf8(h.id);
    for (const QString& id : placed) {
        const QByteArray b = id.toUtf8();
        if (!reg.action(b.constData()) && reg.holdKey(b.constData()) == Qt::Key_unknown) unknown << id;
    }
    const bool mouse = text.contains("Alt+LMB") && text.contains("Pan - LMB drag")
                       && text.contains("Zoom - Mouse Wheel") && text.contains("Shift + LMB drag");
    check("help-labels", missing.isEmpty() && unplaced.isEmpty() && unknown.isEmpty() && mouse
                             && shown.contains("playback.play-pause"),
          QStringLiteral("missing=%1 unplaced=%2 unknown=%3").arg(missing.join(','), unplaced.join(','), unknown.join(',')));

    // The overlay is drawn centred and unclipped: it must not grow past today's string
    // (53 lines in gfcPlateManager's fallback helpMessage).
    check("help-fits", lines.size() <= 53, QStringLiteral("lines=%1").arg(lines.size()));
    check("help-all-plates-suffix",
          text.contains(QStringLiteral("Fit to Screen - %1  (+Shift = all plates)").arg(reg.key("plate.fit").toString(QKeySequence::PortableText))),
          QString());

    reg.setOverride("inout.set-in", QKeySequence("K"));
    const QString after = buildHelpText(reg);
    bool inOk = after.contains("Set IN Point - K");
    for (const QString& line : after.split('\n'))
        if (line.startsWith("Set IN Point") && line.contains(" - I")) inOk = false;
    check("help-override", inOk);

    reg.setOverride("inout.set-in", QKeySequence());
    check("help-unbound-omitted", !buildHelpText(reg).contains("Set IN Point"));
    reg.setOverride("help.toggle-overlay", QKeySequence());
    check("help-header-unbound", !buildHelpText(reg).contains("toggle on/off"));
    check("tooltip-with-key",
          tooltipWithKey("Play", "playback.play-pause") == "Play (" + reg.label("playback.play-pause") + ")"
          && tooltipWithKey("Set In", "inout.set-in") == "Set In");
    reg.reset();
}

// No setToolTip in src/qt may hard-code a key name; use tooltipWithKey(text, id).
void testTooltipGrep()
{
#ifdef JEFE_SOURCE_DIR
    const QRegularExpression keyName(
        R"(\((Space|Shift\+|Ctrl\+|Alt\+|Cmd\+|F\d+|Home|End|[A-Z0-9,.<>])\))");
    QStringList hits;
    int files = 0;
    QDirIterator it(QStringLiteral(JEFE_SOURCE_DIR "/src/qt"), {"*.cpp"}, QDir::Files);
    while (it.hasNext()) {
        QFile f(it.next());
        if (!f.open(QIODevice::ReadOnly)) continue;
        ++files;
        int n = 0;
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine());
            ++n;
            if (line.contains("setToolTip(") && keyName.match(line).hasMatch())
                hits << QStringLiteral("%1:%2").arg(QFileInfo(f).fileName()).arg(n);
        }
    }
    check("tooltip-grep", files > 0 && hits.isEmpty(),
          QStringLiteral("files=%1 hits=%2").arg(files).arg(hits.join(',')));
#else
    check("tooltip-grep", false, QStringLiteral("JEFE_SOURCE_DIR not defined"));
#endif
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
    testOverrideSignals(reg);
    testPresetSame(reg);
    testOverrideDropEqual(reg);
    testOverrideUnbind(reg);
    testOverrideUnknownKept(reg);
    testPersistRoundtrip(reg);
    testHoldOverride(reg);
    testSnapshotRestore(reg);
    testExportImport(reg);
    testImportPortable(reg);
    testImportUnknownId(reg);
    testImportBad(reg);
    testImportConflict(reg);
    testExportTwoKeysAndUnbound(reg);
    testPersistComma(reg);
    testParity20(reg);
    testPresetsConflictFree(reg);
    testHelpText(reg);
    testTooltipGrep();
    reg.reset();
    std::printf("SHORTCUTS-TEST %s\n", g_fails == 0 ? "PASS" : "FAIL");
    return g_fails;
}

}  // namespace jefe::shortcuts
