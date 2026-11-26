// SPDX-License-Identifier: GPL-2.0-or-later
// Headless self-test for the shortcut registry (--shortcuts-test). Needs a QApplication
// (QAction) but no window.
#include "Shortcuts_qt.h"
#include "PreferencesWindow_qt.h"

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

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <memory>
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
                               reg.key("t.d") == QKeySequence(",") && reg.holdKey("t.h") == Qt::Key_Comma,
          before + " vs " + after);
}

void registerPersistTwo(Registry& reg)
{
    registerTwo(reg);
    reg.registerCommand(cmd("t.two", Scope::Player, {QKeySequence("A")}));
    reg.registerCommand(cmd("t.semi", Scope::Player, {QKeySequence("B")}));
    reg.registerCommand(cmd("t.comma", Scope::Global, {QKeySequence("E")}));
}

// A two-key override survives save -> load in INI and native QSettings, including keys that
// are the separator characters themselves.
void testPersistTwoKeys(Registry& reg)
{
    const QString jsonPath = tempPath(".jcshortcuts");
    writeFile(jsonPath, R"({"format":"jefecheck-shortcuts","version":1,"basedOn":"editing",
        "keys":{"t.two":["X","Ctrl+Y"],"t.semi":[";","Shift+;"],"t.comma":["Ctrl+,","Meta+Ctrl+F"]}})");
    QStringList bad;
    for (const bool native : {false, true}) {
        registerPersistTwo(reg);
        QString err;
        if (!reg.importFrom(jsonPath, &err)) bad << "import:" + err;
        const QString before = allKeys(reg);
        const QString name = QStringLiteral("shortcuts-test-") + QUuid::createUuid().toString(QUuid::Id128);
        const QString path = tempPath(".ini");
        auto open = [&] {
            return native ? std::make_unique<QSettings>(QSettings::NativeFormat, QSettings::UserScope,
                                                        QStringLiteral("JefeCheckTest"), name)
                          : std::make_unique<QSettings>(path, QSettings::IniFormat);
        };
        QString stored;
        {
            auto out = open();
            reg.save(*out);
            out->sync();
            stored = out->value("Shortcuts/overrides/t.two").toString();
        }
        registerPersistTwo(reg);
        auto in = open();
        reg.load(*in);
        const QString after = allKeys(reg);
        const bool two = reg.keys("t.two") == QList<QKeySequence>{QKeySequence("X"), QKeySequence("Ctrl+Y")} &&
                         reg.action("t.two")->shortcuts().size() == 2;
        if (before != after || !two || stored != "X; Ctrl+Y")
            bad << QStringLiteral("%1: stored=%2 %3 vs %4").arg(native ? "native" : "ini", stored, before, after);
        const QString file = in->fileName();
        in->clear();
        in->sync();
        in.reset();
        QFile::remove(file);
    }
    QFile::remove(jsonPath);
    check("persist-two-keys", bad.isEmpty(), bad.join(' '));
}

// A preset name from a later version reads as editing and is written back unchanged until the
// user picks a preset.
void testPersistUnknownPreset(Registry& reg)
{
    registerTwo(reg);
    const QString path = tempPath(".ini");
    {
        QSettings s(path, QSettings::IniFormat);
        s.setValue("Shortcuts/preset", "studio-2027");
        s.sync();
    }
    QSettings s(path, QSettings::IniFormat);
    reg.load(s);
    const bool asEditing = reg.preset() == "editing" && reg.key("t.a") == QKeySequence("I");
    reg.save(s);
    s.sync();
    const QString kept = s.value("Shortcuts/preset").toString();
    reg.setPreset(QStringLiteral("editing"));
    reg.save(s);
    s.sync();
    const QString picked = s.value("Shortcuts/preset").toString();
    QFile::remove(path);
    check("persist-unknown-preset", asEditing && kept == "studio-2027" && picked == "editing",
          QStringLiteral("kept=%1 picked=%2").arg(kept, picked));
}

// The Preferences dialog: Cancel reverts the registry to what it was when the dialog opened;
// Done writes it through QSettings (redirected to a temp ini so the user's own are untouched);
// Done with an unchanged preset keeps a later version's preset name.
void testPrefsCancelDone(Registry& reg)
{
    const QString dir = QDir::tempPath() + QStringLiteral("/jefe-prefs-test-") +
                        QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(dir);
    const QSettings::Format oldFmt = QSettings::defaultFormat();
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    const Registry::Snapshot orig = reg.snapshot();
    reg.reset();
    registerAllCommands(reg);
    reg.setPreset(QStringLiteral("editing"));
    const QKeySequence base = reg.key("playback.play-pause");

    bool cancelOk = false, doneOk = false, unknownOk = false;
    {
        PreferencesWindow_Qt dlg;
        reg.setOverride("playback.play-pause", QKeySequence("Ctrl+Alt+Q"));
        const bool changed = reg.key("playback.play-pause") == QKeySequence("Ctrl+Alt+Q");
        reg.setPreset(QStringLiteral("classic"));
        dlg.cancelForTest();
        cancelOk = changed && reg.preset() == "editing" && reg.key("playback.play-pause") == base;
    }
    {
        PreferencesWindow_Qt dlg;
        reg.setOverride("playback.play-pause", QKeySequence("Ctrl+Alt+Q"));
        dlg.commitForTest();
        QSettings s;
        s.sync();
        doneOk = s.value("Shortcuts/overrides/playback.play-pause").toString().contains("Ctrl+Alt+Q") &&
                 s.value("Shortcuts/preset").toString() == "editing";
    }
    {
        QSettings s;
        s.setValue("Shortcuts/preset", "studio-2027");
        s.sync();
        reg.load(s);
        PreferencesWindow_Qt dlg;
        dlg.commitForTest();  // no setPreset call: the preset did not change
        QSettings t;
        t.sync();
        unknownOk = t.value("Shortcuts/preset").toString() == "studio-2027";
    }
    QSettings::setDefaultFormat(oldFmt);
    QDir(dir).removeRecursively();
    reg.restore(orig);
    check("prefs-cancel", cancelOk);
    check("prefs-done", doneOk);
    check("prefs-done-keeps-unknown-preset", unknownOk);
}

// A stored value this build cannot parse (for a registered id) falls back to the preset and is
// written back verbatim; setting that command's key replaces it.
void testPersistUnreadableKept(Registry& reg)
{
    registerTwo(reg);
    const QString path = tempPath(".ini");
    {
        QSettings s(path, QSettings::IniFormat);
        s.setValue("Shortcuts/overrides/t.a", "Hyper+NoSuchKey");
        s.setValue("Shortcuts/overrides/t.b", "K");
        s.setValue("Shortcuts/holdOverrides/t.h", "Ctrl+W");
        s.sync();
    }
    QSettings s(path, QSettings::IniFormat);
    reg.load(s);
    const bool fallback = reg.key("t.a") == QKeySequence("I") && reg.key("t.b") == QKeySequence("K") &&
                          reg.holdKey("t.h") == Qt::Key_W;
    reg.save(s);
    s.sync();
    const QString keptA = s.value("Shortcuts/overrides/t.a").toString();
    const QString keptH = s.value("Shortcuts/holdOverrides/t.h").toString();
    const bool keptB = s.value("Shortcuts/overrides/t.b").toString() == "K";
    reg.setOverride("t.a", QKeySequence("J"));
    reg.save(s);
    s.sync();
    const QString replaced = s.value("Shortcuts/overrides/t.a").toString();
    QFile::remove(path);
    check("persist-unreadable-kept",
          fallback && keptA == "Hyper+NoSuchKey" && keptH == "Ctrl+W" && keptB && replaced == "J",
          QStringLiteral("a=%1 hold=%2 replaced=%3").arg(keptA, keptH, replaced));
}

// ---- Presets ---------------------------------------------------------------------------

struct ParityRow {
    Scope scope;
    const char* key;  // PortableText
    const char* id;
};

constexpr Scope P = Scope::Player;
constexpr Scope G = Scope::Global;

QKeySequence portableKey(const QByteArray& text);
QKeySequence portableKey(const char* text)
{
    return QKeySequence::fromString(QString::fromLatin1(text), QKeySequence::PortableText);
}

QKeySequence portableKey(const QByteArray& text) { return portableKey(text.constData()); }

// FLTK's FL_CTRL is the physical Control key: Qt::META on macOS (where Qt::CTRL is Cmd),
// Qt::CTRL elsewhere. Rows of the 1.7 table written "Ctrl+..." mean that key.
QKeySequence physicalCtrl(const char* text)
{
    QKeySequence k = portableKey(text);
#ifdef Q_OS_MACOS
    if (!k.isEmpty() && (k[0].keyboardModifiers() & Qt::ControlModifier)) {
        const Qt::KeyboardModifiers m = (k[0].keyboardModifiers() & ~Qt::ControlModifier) | Qt::MetaModifier;
        k = QKeySequence(QKeyCombination(m, k[0].key()));
    }
#endif
    return k;
}

// Checks one table: every row's key resolves to its id in `scope` under `preset`.
QStringList checkRows(Registry& reg, const char* preset, const ParityRow* rows, int n, bool physical,
                      int* count)
{
    QStringList bad;
    for (int i = 0; i < n; ++i) {
        const ParityRow& r = rows[i];
        const QKeySequence key = physical ? physicalCtrl(r.key) : portableKey(r.key);
        ++*count;
        const QString got = reg.commandForKey(r.scope, key);
        if (got != QString::fromLatin1(r.id))
            bad << QStringLiteral("%1:%2->%3(want %4)")
                       .arg(preset, key.toString(QKeySequence::PortableText), got, r.id);
    }
    return bad;
}

QStringList checkHolds(Registry& reg, const char* preset, const QList<QPair<const char*, Qt::Key>>& holds,
                       int* count)
{
    QStringList bad;
    for (const auto& h : holds) {
        ++*count;
        if (reg.holdKey(h.first) != h.second)
            bad << QStringLiteral("%1:hold %2=%3").arg(preset, h.first).arg(int(reg.holdKey(h.first)));
    }
    return bad;
}

QStringList checkUnbound(Registry& reg, const char* preset, const QStringList& ids)
{
    QStringList bad;
    for (const QString& id : ids) {
        const QByteArray b = id.toUtf8();
        if (!reg.keys(b.constData()).isEmpty()) bad << QStringLiteral("%1:%2 bound to %3").arg(preset, id, keysText(reg.keys(b.constData())));
    }
    return bad;
}

// The Editing table's ancestor: every key qt-experimental handled before the presets
// (GlViewport_Qt::keyPressEvent, MainWindow_Qt's QShortcuts and menu keys), with the rows the
// spec changed for Editing updated: X / C no longer step, Shift+I / Shift+O go to In / Out,
// Alt+I / Alt+O reset them, Shift+Alt+I sets In and loads.
const ParityRow kParity20[] = {
    // Viewport keyPressEvent (Player).
    {P, "Space", "playback.play-pause"},
    {P, "Left", "playback.step-back"},
    {P, "Right", "playback.step-forward"},
    {P, "Home", "playback.go-start"},
    {P, "End", "playback.go-end"},
    {P, ",", "playback.direction-reverse"},
    {P, "<", "playback.direction-reverse"},
    {P, ".", "playback.direction-forward"},
    {P, ">", "playback.direction-forward"},
    {P, "8", "playback.loop-once"},
    {P, "9", "playback.loop"},
    {P, "0", "playback.bounce"},
    {P, "I", "inout.set-in"},
    {P, "O", "inout.set-out"},
    {P, "Shift+I", "inout.go-in"},
    {P, "Shift+O", "inout.go-out"},
    {P, "Alt+I", "inout.reset-in"},
    {P, "Alt+O", "inout.reset-out"},
    {P, "Shift+Alt+I", "inout.set-in-load"},
    {P, "Up", "plate.track-prev"},
    {P, "Down", "plate.track-next"},
    {P, "Return", "remote.chat-type"},
    {P, "Enter", "remote.chat-type"},
    // Layouts (MainWindow QShortcuts and viewport Ctrl+1-4; registered once, Global).
    {G, "Ctrl+1", "layout.single"},
    {G, "Ctrl+2", "layout.side"},
    {G, "Ctrl+3", "layout.top-bottom"},
    {G, "Ctrl+4", "layout.quad"},
    // Plate transforms: View menu (active) and QShortcuts (all plates).
    {G, "F", "plate.fit"},
    {G, "Shift+F", "plate.fit-all"},
    {G, "V", "plate.flip"},
    {G, "Shift+V", "plate.flip-all"},
    {G, "M", "plate.flop"},
    {G, "Shift+M", "plate.flop-all"},
    {G, "T", "plate.text-mode"},
    {G, "Alt+T", "plate.text-mode-all"},
    {G, "Ctrl+R", "plate.reset"},
    {G, "Ctrl+Alt+R", "plate.reset-all"},
    {G, "Shift+R", "color.reset"},
    {G, "Shift+Alt+R", "color.reset-all"},
    // File menu.
    {G, "Ctrl+O", "file.quick-load"},
    {G, "Ctrl+L", "file.load-manager"},
    {G, "Ctrl+Shift+O", "file.open-session"},
    {G, "Ctrl+P", "file.preferences"},
    {G, "Ctrl+,", "file.preferences"},
    // View menu.
    {G, "F11", "view.toggle-fullscreen"},
    {G, "Meta+Ctrl+F", "view.toggle-fullscreen"},
    {G, "Ctrl+H", "view.toggle-histogram"},
    {G, "Ctrl+Alt+H", "view.toggle-histogram-all"},
    {G, "N", "view.toggle-note-overlay"},
    // Window menu.
    {G, "F2", "panels.toggle-plate-manager"},
    {G, "F3", "panels.toggle-fx"},
    {G, "F4", "panels.toggle-luts"},
    {G, "F5", "panels.open-remote"},
    {G, "F6", "panels.open-render"},
    {G, "F7", "panels.toggle-notes"},
    {G, "F8", "panels.toggle-timeline"},
    {G, "F9", "panels.toggle-playlist"},
    {G, "Ctrl+Alt+F", "panels.hide-controls"},
#ifdef Q_OS_MACOS
    {G, "Ctrl+M", "panels.minimize"},
#endif
    // Help menu.
    {G, "F1", "help.manual"},
    {G, "H", "help.toggle-overlay"},
};

const QList<QPair<const char*, Qt::Key>> kDragHolds = {
    {"color.drag-gamma", Qt::Key_W},      {"color.drag-exposure", Qt::Key_E},
    {"color.drag-brightness", Qt::Key_Q}, {"color.drag-contrast", Qt::Key_D},
    {"color.drag-saturation", Qt::Key_S},
};

void testParity20(Registry& reg)
{
    reg.reset();
    registerAllCommands(reg);
    reg.setPreset(QStringLiteral("editing"));
    int rows = 0;
    QStringList bad = checkRows(reg, "editing", kParity20, int(std::size(kParity20)), false, &rows);
    // Standard keys: exactly the one key the menu constructs (QKeySequence(StandardKey)).
    for (const auto& [standardKey, id] : {std::pair{QKeySequence::Save, "file.save-session"},
                                          std::pair{QKeySequence::Quit, "file.quit"}}) {
        const QKeySequence menuKey(standardKey);
        ++rows;
        if (reg.keys(id) != QList<QKeySequence>{menuKey})
            bad << QStringLiteral("editing:%1 keys=%2 (want only %3)")
                       .arg(id, keysText(reg.keys(id)), menuKey.toString(QKeySequence::PortableText));
    }
    bad << checkHolds(reg, "editing", kDragHolds, &rows);
    for (const Command& c : reg.commands())
        if (c.editing.size() > 2 || c.classic.size() > 2) bad << QStringLiteral(">2 keys: %1").arg(c.id);
    check("parity-2.0", bad.isEmpty(),
          QStringLiteral("rows=%1 commands=%2 holds=%3 %4")
              .arg(rows).arg(reg.commands().size()).arg(reg.holdKeys().size()).arg(bad.join(' ')));
}

// One row per line of the spec's preset table, Editing column.
const ParityRow kEditing[] = {
    {P, "Space", "playback.play-pause"},                                     // Play / Pause
    {P, "J", "playback.jkl-reverse"}, {P, "K", "playback.jkl-pause"},        // J / K / L
    {P, "L", "playback.jkl-forward"},
    {P, "Left", "playback.step-back"}, {P, "Right", "playback.step-forward"},  // Step
    {P, "Home", "playback.go-start"}, {P, "End", "playback.go-end"},         // Start / end
    {P, ",", "playback.direction-reverse"}, {P, ".", "playback.direction-forward"},  // Direction
    {P, "<", "playback.direction-reverse"}, {P, ">", "playback.direction-forward"},
    {P, "8", "playback.loop-once"}, {P, "9", "playback.loop"}, {P, "0", "playback.bounce"},
    {P, "I", "inout.set-in"}, {P, "O", "inout.set-out"},                     // Set In / Out
    {P, "Shift+I", "inout.go-in"}, {P, "Shift+O", "inout.go-out"},           // Go to In / Out
    {P, "Alt+I", "inout.reset-in"}, {P, "Alt+O", "inout.reset-out"},         // Reset to ends
    {P, "Alt+X", "inout.clear-both"},                                        // Clear In and Out
    {P, "Shift+Alt+I", "inout.set-in-load"},                                 // Set In and load
    {P, "Up", "plate.track-prev"}, {P, "Down", "plate.track-next"},          // Cycle track
    {P, "1", "plate.show-track-1"}, {P, "2", "plate.show-track-2"},          // Show track A-D
    {P, "3", "plate.show-track-3"}, {P, "4", "plate.show-track-4"},
    {P, "R", "color.channel-r"}, {P, "G", "color.channel-g"},                // Channels
    {P, "B", "color.channel-b"}, {P, "A", "color.channel-a"},
    {P, "Shift+1", "color.cc-fav-apply-1"}, {P, "Shift+5", "color.cc-fav-apply-5"},  // CC favourites
#ifdef Q_OS_MACOS  // Cmd+Shift+3/4/5 and Cmd+F5 belong to macOS: Control+Shift+digit, Option+Fn
    {P, "Meta+Shift+1", "color.cc-fav-save-1"}, {P, "Meta+Shift+5", "color.cc-fav-save-5"},
    {P, "Shift+F1", "fx.fav-load-1"}, {P, "Shift+F5", "fx.fav-load-5"},       // FX favourites
    {P, "Alt+F1", "fx.fav-append-1"}, {P, "Alt+F5", "fx.fav-append-5"},
#else
    {P, "Ctrl+Shift+1", "color.cc-fav-save-1"}, {P, "Ctrl+Shift+5", "color.cc-fav-save-5"},
    {P, "Shift+F1", "fx.fav-load-1"}, {P, "Shift+F5", "fx.fav-load-5"},       // FX favourites
    {P, "Ctrl+F1", "fx.fav-append-1"}, {P, "Ctrl+F5", "fx.fav-append-5"},
#endif
    {P, "Ctrl+Shift+F1", "fx.fav-save-1"}, {P, "Ctrl+Shift+F5", "fx.fav-save-5"},
    {G, "F", "plate.fit"}, {G, "Shift+F", "plate.fit-all"},                  // Fit
    {G, "V", "plate.flip"}, {G, "Shift+V", "plate.flip-all"},                // Flip
    {G, "M", "plate.flop"}, {G, "Shift+M", "plate.flop-all"},                // Flop
    {G, "T", "plate.text-mode"}, {G, "Alt+T", "plate.text-mode-all"},        // Text mode
    {G, "Ctrl+R", "plate.reset"}, {G, "Ctrl+Alt+R", "plate.reset-all"},      // Reset plate
    {G, "Shift+R", "color.reset"}, {G, "Shift+Alt+R", "color.reset-all"},    // Reset colour
    {G, "F11", "view.toggle-fullscreen"}, {G, "Meta+Ctrl+F", "view.toggle-fullscreen"},
    {G, "Ctrl+Alt+F", "panels.hide-controls"},                               // Hide controls
    {G, "H", "help.toggle-overlay"},                                         // On-screen help
    {G, "Ctrl+H", "view.toggle-histogram"}, {G, "Ctrl+Alt+H", "view.toggle-histogram-all"},
    {G, "Ctrl+L", "file.load-manager"},                                      // Load manager
    {G, "Ctrl+P", "file.preferences"}, {G, "Ctrl+,", "file.preferences"},    // Preferences
    {P, "Return", "remote.chat-type"},                                       // Chat
    {G, "Ctrl+1", "layout.single"}, {G, "Ctrl+2", "layout.side"},            // Layouts
    {G, "Ctrl+3", "layout.top-bottom"}, {G, "Ctrl+4", "layout.quad"},
    {G, "F2", "panels.toggle-plate-manager"}, {G, "F9", "panels.toggle-playlist"},  // Panels
    {G, "N", "view.toggle-note-overlay"}, {G, "F1", "help.manual"},
    {G, "Ctrl+O", "file.quick-load"}, {G, "Ctrl+Shift+O", "file.open-session"},
    {P, "=", "timeline.zoom-in"}, {P, "-", "timeline.zoom-out"},             // Timeline zoom
    {P, "\\", "timeline.zoom-fit"}, {P, "Shift+\\", "timeline.zoom-inout"},
};

void testPresetEditing(Registry& reg)
{
    reg.reset();
    registerAllCommands(reg);
    reg.setPreset(QStringLiteral("editing"));
    int rows = 0;
    QStringList bad = checkRows(reg, "editing", kEditing, int(std::size(kEditing)), false, &rows);
    for (const auto& [standardKey, id] : {std::pair{QKeySequence::Save, "file.save-session"},
                                          std::pair{QKeySequence::Quit, "file.quit"}}) {
        ++rows;
        if (reg.commandForKey(G, QKeySequence(standardKey)) != QLatin1String(id)) bad << QStringLiteral("standard %1").arg(id);
    }
    bad << checkHolds(reg, "editing", kDragHolds + QList<QPair<const char*, Qt::Key>>{{"plate.lut-hold", Qt::Key_unknown}}, &rows);
    // Unbound in Editing: previous / next LUT, show-hide chat, the 1.7-only play keys; X / C no longer step.
    bad << checkUnbound(reg, "editing", {"plate.lut-prev", "plate.lut-next", "remote.chat-toggle",
                                         "playback.play-reverse", "playback.play-forward"});
    for (const char* key : {"X", "C", "Z", "Y"})
        if (!reg.commandForKey(P, portableKey(key)).isEmpty() || !reg.commandForKey(G, portableKey(key)).isEmpty())
            bad << QStringLiteral("editing:%1 is bound").arg(key);
    check("preset-editing", bad.isEmpty(), QStringLiteral("rows=%1 %2").arg(rows).arg(bad.join(' ')));
}

// Every key shipped JefeCheck 1.7 handled (`main`: UICallbacks.cpp globalCB, GlViewport.cpp
// FL_KEYDOWN, the mainWindow.fl menu and transport-button shortcuts) -> its command under
// classic. "Ctrl" here is FLTK's FL_CTRL, the physical Control key (physicalCtrl()).
const ParityRow kParity17[] = {
    // GlViewport.cpp FL_KEYDOWN (not in chat mode).
    {G, "F2", "panels.toggle-plate-manager"},  // fxControlWindow1 (FX stack); 2.0 panel key kept
    {G, "F3", "panels.toggle-fx"},             // fxw.fxWindow
    {G, "F4", "panels.toggle-luts"},           // lutw.lutWindow
    {G, "F5", "panels.open-remote"},           // rmw.remoteWindow
    {G, "F6", "panels.open-render"},           // rw.renderWindow
    {P, "Space", "playback.play-pause"},       // playbackManager.pause()
    {P, "Left", "playback.step-back"},         // oneFrameRev()
    {P, "Right", "playback.step-forward"},     // oneFrameFwd()
    {P, "Up", "plate.track-prev"},             // track-1 (wraps); with L held: scrollLUT(-1), see hold-with
    {P, "Down", "plate.track-next"},           // track+1 (wraps); with L held: scrollLUT(+1)
    // mainWindow.fl transport buttons: playRevButton '<' (startPlayRev), playFwdButton '>'.
    {P, "<", "playback.play-reverse"},
    {P, ">", "playback.play-forward"},
    // UICallbacks.cpp globalCB, FL_CTRL block.
    {G, "Ctrl+1", "layout.single"},            // setFramingMode(FRAMINGSINGLE_ID)
    {G, "Ctrl+2", "layout.side"},
    {G, "Ctrl+3", "layout.top-bottom"},
    {G, "Ctrl+4", "layout.quad"},
    {P, "Ctrl+Shift+1", "color.cc-fav-save-1"},  // saveFavoriteColorCorrectionFromPlate(0)
    {P, "Ctrl+Shift+2", "color.cc-fav-save-2"},
    {P, "Ctrl+Shift+3", "color.cc-fav-save-3"},
    {P, "Ctrl+Shift+4", "color.cc-fav-save-4"},
    {P, "Ctrl+Shift+5", "color.cc-fav-save-5"},
    {G, "Ctrl+8", "plate.flip"},               // toggleFlip(active)
    {G, "Ctrl+Alt+8", "plate.flip-all"},       // toggleFlipAll()
    {G, "Ctrl+9", "plate.flop"},               // toggleFlop(active)
    {G, "Ctrl+Alt+9", "plate.flop-all"},       // toggleFlopAll()
    // globalCB, main switch.
    {P, ".", "playback.direction-forward"},    // setDirection(1)
    {P, ",", "playback.direction-reverse"},    // setDirection(-1)
    {P, "C", "playback.step-forward"},         // oneFrameFwd()
    {P, "X", "playback.step-back"},            // oneFrameRev()
    {P, "V", "playback.go-end"},               // ffwd()
    {P, "Z", "playback.go-start"},             // rew()
    {P, "8", "playback.loop-once"},            // LOOPMODEONCE_ID
    {P, "9", "playback.loop"},                 // LOOPMODELOOP_ID
    {P, "0", "playback.bounce"},               // LOOPMODEBOUNCE_ID
    {P, "1", "plate.show-track-1"},            // setTrackOnPlate(active, 0)
    {P, "2", "plate.show-track-2"},
    {P, "3", "plate.show-track-3"},
    {P, "4", "plate.show-track-4"},
    {P, "Shift+1", "color.cc-fav-apply-1"},    // setFavoriteColorCorrectionOnPlate(0)
    {P, "Shift+2", "color.cc-fav-apply-2"},
    {P, "Shift+3", "color.cc-fav-apply-3"},
    {P, "Shift+4", "color.cc-fav-apply-4"},
    {P, "Shift+5", "color.cc-fav-apply-5"},
    {G, "Ctrl+L", "file.load-manager"},        // lw.loadWindow->show()
    {P, "Alt+I", "inout.set-in-load"},         // startLoadingAllAt + setInPoint(current)
    {P, "Shift+I", "inout.reset-in"},          // setInPoint(1)
    {P, "I", "inout.set-in"},                  // setInPoint(current)
    {P, "Shift+O", "inout.reset-out"},         // setOutPoint(max track length)
    {P, "O", "inout.set-out"},                 // setOutPoint(current)
    {P, "Ctrl+Shift+F1", "fx.fav-save-1"},     // saveFavoriteFromPlate(0)
    {P, "Ctrl+Shift+F5", "fx.fav-save-5"},
    {P, "Ctrl+F1", "fx.fav-append-1"},         // appendFavoriteOnPlate(0)
    {P, "Ctrl+F5", "fx.fav-append-5"},
    {P, "Shift+F1", "fx.fav-load-1"},          // setFavoriteOnPlate(0)
    {P, "Shift+F2", "fx.fav-load-2"},
    {P, "Shift+F3", "fx.fav-load-3"},
    {P, "Shift+F4", "fx.fav-load-4"},
    {P, "Shift+F5", "fx.fav-load-5"},
    {G, "Ctrl+Alt+R", "plate.reset-all"},      // resetAllPlates()
    {G, "Ctrl+R", "plate.reset"},              // resetPlate(active)
    {G, "Shift+Alt+R", "color.reset-all"},     // resetAllColorCorrections()
    {G, "Shift+R", "color.reset"},             // resetColorCorrection(active)
    {P, "R", "color.channel-r"},               // toggleChannelR
    {P, "G", "color.channel-g"},
    {P, "B", "color.channel-b"},
    {P, "A", "color.channel-a"},
    {G, "Alt+T", "plate.text-mode-all"},       // toggleTextModeAll()
    {G, "T", "plate.text-mode"},               // toggleTextMode(active)
    {G, "Ctrl+Alt+H", "view.toggle-histogram-all"},  // toggleHistogramModeAll()
    {G, "Ctrl+H", "view.toggle-histogram"},    // toggleHistogramMode(active)
    {G, "H", "help.toggle-overlay"},           // toggleHelp()
    {G, "Ctrl+Q", "file.quit"},                // confirmQuit()
    {G, "Ctrl+Alt+F", "panels.hide-controls"}, // toggleHideControls()
    {G, "Alt+F", "plate.fit-all"},             // fitToViewportAll()
    {G, "F", "plate.fit"},                     // fitToViewport(active)
    {G, "Ctrl+F", "view.toggle-fullscreen"},   // toggleFullscreen()
    {P, "Ctrl+Y", "remote.chat-toggle"},       // chat fade counter: show / fade the chat
    {P, "Y", "remote.chat-type"},              // gChatMode = 1
    // mainWindow.fl menu shortcuts not already above.
    {G, "Ctrl+P", "panels.toggle-playlist"},   // File / Dialogs: Playlist Manager
    {G, "Ctrl+S", "file.save-session"},        // File: Save Session
    {G, "Ctrl+O", "file.open-session"},        // File: Open Session
    {G, "F1", "help.manual"},                  // Help: User Manual
};

void testParity17(Registry& reg)
{
    reg.reset();
    registerAllCommands(reg);
    reg.setPreset(QStringLiteral("classic"));
    int rows = 0;
    QStringList bad = checkRows(reg, "classic", kParity17, int(std::size(kParity17)), true, &rows);
    bad << checkHolds(reg, "classic", kDragHolds + QList<QPair<const char*, Qt::Key>>{{"plate.lut-hold", Qt::Key_L}}, &rows);
    // 2.0 keys kept where 1.7 left them free (spec rules).
    const ParityRow kept[] = {
        {G, "F7", "panels.toggle-notes"}, {G, "F8", "panels.toggle-timeline"}, {G, "F9", "panels.toggle-playlist"},
        {G, "N", "view.toggle-note-overlay"}, {G, "M", "plate.flop"}, {G, "Shift+M", "plate.flop-all"},
        {G, "Shift+F", "plate.fit-all"}, {G, "F11", "view.toggle-fullscreen"}, {G, "Ctrl+,", "file.preferences"},
        {G, "Ctrl+Shift+O", "file.open-session"}, {P, "Home", "playback.go-start"}, {P, "End", "playback.go-end"},
        {P, "=", "timeline.zoom-in"}, {P, "-", "timeline.zoom-out"}, {P, "\\", "timeline.zoom-fit"},
        {P, "Shift+\\", "timeline.zoom-inout"},
    };
    bad << checkRows(reg, "classic", kept, int(std::size(kept)), false, &rows);
    ++rows;
    if (reg.commandForKey(G, QKeySequence(QKeySequence::Quit)) != QLatin1String("file.quit")) bad << "classic: standard Quit";
    bad << checkUnbound(reg, "classic", {"playback.jkl-reverse", "playback.jkl-pause", "playback.jkl-forward",
                                         "inout.go-in", "inout.go-out", "inout.clear-both",
                                         "plate.lut-prev", "plate.lut-next"});
    reg.setPreset(QStringLiteral("editing"));
    check("parity-1.7", bad.isEmpty(), QStringLiteral("rows=%1 %2").arg(rows).arg(bad.join(' ')));
}

void testClassicNoJKL(Registry& reg)
{
    reg.reset();
    registerAllCommands(reg);
    reg.setPreset(QStringLiteral("classic"));
    QStringList bad;
    for (const char* key : {"J", "K", "L"})
        for (Scope s : {P, G})
            if (!reg.commandForKey(s, portableKey(key)).isEmpty())
                bad << QStringLiteral("%1 -> %2").arg(key, reg.commandForKey(s, portableKey(key)));
    if (reg.holdKey("plate.lut-hold") != Qt::Key_L) bad << "L is not the LUT hold key";
    reg.setPreset(QStringLiteral("editing"));
    check("classic-no-jkl", bad.isEmpty(), bad.join(' '));
}

void testJKL(Registry& reg)
{
    reg.reset();
    registerAllCommands(reg);
    reg.setPreset(QStringLiteral("editing"));
    bool playing = true;
    int direction = -1;
    QStringList calls;
    const Transport fake{
        [&] { return playing; },
        [&](int d) { direction = d; calls << QStringLiteral("dir%1").arg(d); },
        [&] { playing = !playing; calls << QStringLiteral("toggle"); },
    };
    reg.bindRun("playback.jkl-reverse", [&] { runJKL(-1, fake); });
    reg.bindRun("playback.jkl-pause", [&] { runJKL(0, fake); });
    reg.bindRun("playback.jkl-forward", [&] { runJKL(+1, fake); });
    auto press = [&](const char* key) {
        const QString id = reg.commandForKey(P, portableKey(key));
        QAction* a = id.isEmpty() ? nullptr : reg.action(id.toUtf8().constData());
        if (a) a->trigger();
        return a != nullptr;
    };
    QStringList bad;
    if (!press("L") || direction != 1 || !playing) bad << QStringLiteral("L from backward: dir=%1 playing=%2").arg(direction).arg(playing);
    if (!press("L") || direction != 1 || !playing) bad << "L while playing forward stopped";
    if (!press("K") || playing) bad << "K did not pause";
    if (!press("K") || playing) bad << "K while paused started";
    if (!press("J") || direction != -1 || !playing) bad << QStringLiteral("J: dir=%1 playing=%2").arg(direction).arg(playing);
    check("jkl", bad.isEmpty(), bad.isEmpty() ? QString() : bad.join("; ") + " calls=" + calls.join(','));
    reg.reset();
}

void testHoldWith(Registry& reg)
{
    reg.reset();
    registerAllCommands(reg);
    reg.setPreset(QStringLiteral("classic"));
    int prev = 0, next = 0, trackPrev = 0;
    reg.bindHoldWith("plate.lut-hold", Qt::Key_Up, [&] { ++prev; });
    reg.bindHoldWith("plate.lut-hold", Qt::Key_Down, [&] { ++next; });
    reg.bindRun("plate.track-prev", [&] { ++trackPrev; });
    QStringList bad;
    const QSet<int> holdingL{Qt::Key_L}, none;
    // L held: the viewport claims Up (so plate.track-prev's action does not fire) and runs the with action.
    if (!reg.holdWithClaims(holdingL, Qt::Key_Up, Qt::NoModifier)) bad << "L held does not claim Up";
    if (!reg.runHoldWith(holdingL, Qt::Key_Up, Qt::NoModifier) || prev != 1) bad << "L+Up did not run LUT previous";
    if (!reg.runHoldWith(holdingL, Qt::Key_Down, Qt::NoModifier) || next != 1) bad << "L+Down did not run LUT next";
    if (trackPrev != 0) bad << "L+Up ran plate.track-prev";
    if (reg.holdWithClaims(holdingL, Qt::Key_Up, Qt::ShiftModifier)) bad << "L held claims Shift+Up";
    if (reg.holdWithClaims(holdingL, Qt::Key_Left, Qt::NoModifier)) bad << "L held claims Left";
    // Up without L: nothing claims it and the Player action on Up is plate.track-prev.
    if (reg.holdWithClaims(none, Qt::Key_Up, Qt::NoModifier) || reg.runHoldWith(none, Qt::Key_Up, Qt::NoModifier))
        bad << "Up without L claimed";
    const QString up = reg.commandForKey(P, portableKey("Up"));
    if (up != QLatin1String("plate.track-prev")) bad << "Up -> " + up;
    else reg.action("plate.track-prev")->trigger();
    if (trackPrev != 1) bad << "Up without L did not run plate.track-prev";
    // Editing binds no LUT hold: L held claims nothing.
    reg.setPreset(QStringLiteral("editing"));
    if (reg.holdWithClaims(holdingL, Qt::Key_Up, Qt::NoModifier)) bad << "editing: L+Up claimed";
    check("hold-with", bad.isEmpty(), bad.join("; "));
    reg.reset();
}

void testReserved(Registry& reg)
{
    reg.reset();
    registerAllCommands(reg);
    QStringList bad;
    for (const char* preset : {"editing", "classic"}) {
        reg.setPreset(QString::fromLatin1(preset));
        const QStringList c = reg.conflicts("some.new-command", portableKey("="));
        if (!c.contains(QStringLiteral("timeline.zoom-in"))) bad << QStringLiteral("%1: = conflicts [%2]").arg(preset, c.join(','));
        for (const char* id : {"timeline.zoom-in", "timeline.zoom-out", "timeline.zoom-fit", "timeline.zoom-inout"}) {
            const int i = [&] {
                for (int j = 0; j < reg.commands().size(); ++j)
                    if (QString::fromUtf8(reg.commands()[j].id) == QLatin1String(id)) return j;
                return -1;
            }();
            if (i < 0) bad << QStringLiteral("%1 not registered").arg(id);
            else if (reg.commands()[i].run) bad << QStringLiteral("%1 has a run").arg(id);
        }
    }
    reg.setPreset(QStringLiteral("editing"));
    check("reserved", bad.isEmpty(), bad.join(' '));
}

// A command key equal to a hold key (bare, same preset) is a conflict, both ways round.
void testHoldConflict(Registry& reg)
{
    reg.reset();
    reg.registerCommand(cmd("t.a", P, {QKeySequence("L")}, {QKeySequence("K")}));
    reg.registerHoldKey({"t.h", QStringLiteral("Hold"), Qt::Key_L, Qt::Key_L, {}});
    reg.registerHoldKey({"t.h2", QStringLiteral("Hold 2"), Qt::Key_W, Qt::Key_L, {}});
    QStringList bad;
    const QStringList c = reg.conflicts("t.a", QKeySequence("L"));
    if (!c.contains("t.h")) bad << "command L vs hold L: [" + c.join(',') + "]";
    if (!reg.conflicts("t.a", QKeySequence("Shift+L")).isEmpty()) bad << "Shift+L conflicts with a hold key";
    const QStringList h = reg.conflicts("t.h", QKeySequence(Qt::Key_L));
    if (!h.contains("t.a")) bad << "hold L vs command L: [" + h.join(',') + "]";
    if (h.contains("t.h")) bad << "hold conflicts with itself";
    if (reg.conflicts("t.h", QKeySequence(Qt::Key_W)).value(0) != QLatin1String("t.h2")) bad << "hold W vs hold W";
    reg.setPreset(QStringLiteral("classic"));
    if (!reg.conflicts("t.h", QKeySequence(Qt::Key_L)).contains("t.h2")) bad << "classic: two holds on L";
    if (reg.conflicts("t.h", QKeySequence(Qt::Key_L)).contains("t.a")) bad << "classic: t.a is on K";
    check("hold-conflict", bad.isEmpty(), bad.join("; "));
    reg.reset();
}

void testDefaultPreset(Registry& reg)
{
    registerTwo(reg);
    const QString path = tempPath(".ini");
    QSettings s(path, QSettings::IniFormat);
    reg.setDefaultPreset(QStringLiteral("classic"));
    reg.load(s);
    const bool classic = reg.preset() == "classic" && reg.key("t.a") == QKeySequence("Shift+I");
    reg.setDefaultPreset(QStringLiteral("editing"));
    reg.load(s);
    const bool editing = reg.preset() == "editing";
    s.setValue("Shortcuts/preset", "classic");
    reg.load(s);
    const bool stored = reg.preset() == "classic";
    QFile::remove(path);
    // The app's registration makes Editing the default.
    reg.reset();
    reg.setDefaultPreset(QStringLiteral("classic"));
    registerAllCommands(reg);
    const bool app = reg.defaultPreset() == "editing";
    reg.reset();
    check("default-preset", classic && editing && stored && app,
          QStringLiteral("classic=%1 editing=%2 stored=%3 app=%4").arg(classic).arg(editing).arg(stored).arg(app));
}

// macOS keeps some keys for itself; a command on one would never fire (or would fire the system
// action). No preset may use them. Known exceptions, deliberately not listed: Cmd+H (2.0's
// histogram; macOS "Hide" from the app menu), Cmd+M (panels.minimize means the system action).
void testMacosReserved(Registry& reg)
{
#ifdef Q_OS_MACOS
    const char* reserved[] = {"Ctrl+Shift+3", "Ctrl+Shift+4", "Ctrl+Shift+5", "Ctrl+F5", "Ctrl+Alt+F5",
                              "Ctrl+Tab", "Ctrl+Space", "Ctrl+Alt+Esc"};  // Qt Ctrl = Cmd here
    reg.reset();
    registerAllCommands(reg);
    QStringList bad;
    for (const char* preset : {"editing", "classic"}) {
        reg.setPreset(QString::fromLatin1(preset));
        for (const Command& c : reg.commands())
            for (const QKeySequence& key : reg.keys(c.id)) {
                for (const char* r : reserved)
                    if (key == portableKey(r)) bad << QStringLiteral("%1:%2 %3").arg(preset, c.id, r);
                // Control+F1-F5 are macOS keyboard-navigation keys. Allowed only in classic, where
                // they are 1.7's FX-favourite append keys (parity).
                const QKeyCombination kc = key[0];
                if (QString::fromLatin1(preset) == QLatin1String("editing") &&
                    kc.keyboardModifiers() == Qt::MetaModifier && kc.key() >= Qt::Key_F1 && kc.key() <= Qt::Key_F5)
                    bad << QStringLiteral("editing:%1 %2").arg(c.id, key.toString(QKeySequence::PortableText));
            }
    }
    // 1.7 ignored Cmd, so Cmd+Shift+1 applied colour favourite 1 and Cmd+Shift+F1 loaded FX
    // favourite 1: in classic those keys must not save (overwrite) a favourite.
    reg.setPreset(QStringLiteral("classic"));
    for (int i = 1; i <= 5; ++i) {
        const QByteArray n = QByteArray::number(i);
        if (!reg.commandForKey(P, portableKey("Ctrl+Shift+" + n)).isEmpty() ||
            !reg.commandForKey(P, portableKey("Ctrl+Shift+F" + n)).isEmpty())
            bad << "classic: Cmd+Shift+" + QString::fromLatin1(n) + " or Cmd+Shift+F" + QString::fromLatin1(n) + " is bound";
    }
    reg.setPreset(QStringLiteral("editing"));
    check("macos-reserved", bad.isEmpty(), bad.join(' '));
    reg.reset();
#else
    (void)reg;
#endif
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
        for (const HoldKey& h : reg.holdKeys()) {
            const Qt::Key k = reg.holdKey(h.id);
            if (k == Qt::Key_unknown) continue;
            const QStringList others = reg.conflicts(h.id, QKeySequence(k));
            if (!others.isEmpty())
                bad << QStringLiteral("%1:hold %2 %3 vs %4")
                           .arg(preset, h.id, QKeySequence(k).toString(QKeySequence::PortableText), others.join(','));
        }
    }
    reg.setPreset(QStringLiteral("editing"));
    check("presets-conflict-free", !reg.commands().isEmpty() && bad.isEmpty(),
          QStringLiteral("commands=%1 holds=%2 %3").arg(reg.commands().size()).arg(reg.holdKeys().size()).arg(bad.join(' ')));
}

// Help lines name every bound key of every id they place (and ranges their first and last).
QStringList helpLabelProblems(Registry& reg, const QList<HelpLine>& lines, QStringList* shownOut)
{
    QStringList missing;
    auto keysOfId = [&reg](const QString& id) {
        const QByteArray b = id.toUtf8();
        QList<QKeySequence> ks = reg.keys(b.constData());
        if (ks.isEmpty() && reg.holdKey(b.constData()) != Qt::Key_unknown)
            ks << QKeySequence(reg.holdKey(b.constData()));
        return ks;
    };
    // A key name after " - ", " / ", " or " or "(or " (so "F" does not match inside "F11" or "Shift+F").
    auto showsKey = [](const QString& line, const QString& label, const QString& after = QString()) {
        const QRegularExpression re(QStringLiteral(R"((?: - | / | or |\(or ))") + QRegularExpression::escape(label) +
                                    QRegularExpression::escape(after) + QStringLiteral(R"((?=$|\s|\)))"));
        return re.match(line).hasMatch();
    };
    for (const HelpLine& l : lines) {
        for (const QString& id : l.ids) {
            if (shownOut) *shownOut << id;
            const QList<QKeySequence> ks = keysOfId(id);
            if (ks.isEmpty()) missing << id;
            for (const QKeySequence& k : ks)
                if (!showsKey(l.text, k.toString(QKeySequence::PortableText)))
                    missing << id + "=" + k.toString(QKeySequence::PortableText);
        }
        for (const QStringList& range : l.ranges) {
            if (shownOut) *shownOut << range;
            const QList<QKeySequence> first = keysOfId(range.first()), last = keysOfId(range.last());
            if (first.isEmpty() || first.size() != last.size()) { missing << "range " + range.first(); continue; }
            for (int j = 0; j < first.size(); ++j) {
                // Every slot is bound, with the same modifiers, on consecutive keys.
                for (int i = 0; i < range.size(); ++i) {
                    const QList<QKeySequence> ks = keysOfId(range[i]);
                    if (ks.size() <= j || ks[j][0].keyboardModifiers() != first[j][0].keyboardModifiers() ||
                        int(ks[j][0].key()) != int(first[j][0].key()) + i)
                        missing << QStringLiteral("range %1 not a run at %2").arg(range.first(), range[i]);
                }
                const QString lastName = QKeySequence(last[j][0].key()).toString(QKeySequence::PortableText);
                if (!showsKey(l.text, first[j].toString(QKeySequence::PortableText), "-" + lastName))
                    missing << QStringLiteral("range %1=%2-%3").arg(range.first(), first[j].toString(QKeySequence::PortableText), lastName);
            }
        }
    }
    // Extras read "(+Shift = ...)" when they are the line's first key plus modifiers, else
    // "(Key = ...)"; every key of the extra is there.
    auto delta = [](const QKeySequence& base, const QKeySequence& extra) {
        const QKeyCombination b = base[0], e = extra[0];
        const Qt::KeyboardModifiers added = e.keyboardModifiers() & ~b.keyboardModifiers();
        if (b.key() != e.key() || !added || (b.keyboardModifiers() & ~e.keyboardModifiers())) return QString();
        QString out;
        if (added & Qt::ControlModifier) out += "+Ctrl";
        if (added & Qt::MetaModifier) out += "+Meta";
        if (added & Qt::AltModifier) out += "+Alt";
        if (added & Qt::ShiftModifier) out += "+Shift";
        return out;
    };
    for (const HelpLine& l : lines)
        for (const QString& id : l.extraIds) {
            if (shownOut) *shownOut << id;
            const QList<QKeySequence> base = keysOfId(l.ids.isEmpty() ? QString() : l.ids.first());
            const QList<QKeySequence> ks = keysOfId(id);
            if (base.isEmpty() || ks.isEmpty()) missing << id;
            for (const QKeySequence& k : ks) {
                const QString d = base.isEmpty() ? QString() : delta(base.first(), k);
                // Each key form opens a note or follows " or ", and is followed by " = " or " or ".
                const QString form = d.isEmpty() ? k.toString(QKeySequence::PortableText) : d;
                const QRegularExpression re(QStringLiteral(R"((?:\(|, | or ))") + QRegularExpression::escape(form) +
                                            QStringLiteral(R"((?: = | or ))"));
                if (!re.match(l.text).hasMatch()) missing << id + "=" + form;
            }
        }
    return missing;
}

void testHelpText(Registry& reg)
{
    reg.reset();
    registerAllCommands(reg);
    const QStringList headings = {"VIEWPORT LAYOUTS", "WINDOW / GUI", "WINDOW MENU", "PLAYBACK",
                                  "TRANSFORMS", "COLOR CORRECTION", "REMOTE SESSIONS"};
    // Commands the overlay deliberately leaves out: file actions the menu shows, and the
    // timeline project's reserved keys (no run yet).
    const QStringList notInOverlay = {"file.quick-load", "file.load-manager", "file.save-session",
                                      "file.open-session", "file.quit", "panels.minimize",
                                      "timeline.zoom-in", "timeline.zoom-out", "timeline.zoom-fit",
                                      "timeline.zoom-inout"};
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
        const bool isHold = std::any_of(reg.holdKeys().begin(), reg.holdKeys().end(),
                                        [&](const HoldKey& h) { return QString::fromUtf8(h.id) == id; });
        if (!reg.action(b.constData()) && !isHold) unknown << id;
    }
    int maxLines = 0;
    for (const char* preset : {"editing", "classic"}) {
        reg.setPreset(QString::fromLatin1(preset));
        const QList<HelpLine> lines = buildHelpLines(reg);
        const QString text = buildHelpText(reg);
        maxLines = std::max(maxLines, int(lines.size()));
        QStringList missing;
        for (const QString& h : headings)
            if (!text.contains(h)) missing << h;
        QStringList shown;
        missing << helpLabelProblems(reg, lines, &shown);
        // Every bound command the overlay places is on it in this preset.
        for (const QString& id : placed) {
            const QByteArray b = id.toUtf8();
            const bool bound = !reg.keys(b.constData()).isEmpty() || reg.holdKey(b.constData()) != Qt::Key_unknown;
            if (bound && !shown.contains(id)) missing << "not shown:" + id;
        }
        const bool mouse = text.contains("Alt+LMB") && text.contains("Pan - LMB drag")
                           && text.contains("Zoom - Mouse Wheel") && text.contains("Shift + LMB drag");
        const QByteArray name = QStringLiteral("help-labels-%1").arg(preset).toUtf8();
        check(name.constData(), missing.isEmpty() && unplaced.isEmpty() && unknown.isEmpty() && mouse &&
                                    shown.contains("playback.play-pause"),
              QStringLiteral("lines=%1 missing=%2 unplaced=%3 unknown=%4")
                  .arg(lines.size()).arg(missing.join(','), unplaced.join(','), unknown.join(',')));
        if (qEnvironmentVariableIsSet("JEFE_SHORTCUTS_DUMP"))
            std::printf("---- help text (%s) ----\n%s\n---- end ----\n", preset, text.toUtf8().constData());
    }
    reg.setPreset(QStringLiteral("editing"));
    const QString text = buildHelpText(reg);
    QStringList missing;
    // The keys the old hand-written overlay listed, now from the registry, and the new run lines.
    for (const char* want : {"Toggle Fullscreen - F11 or ", "Step one frame (back / forward) - Left / Right",
                             "Play Direction (reverse / forward) - , / . (or < / >)", "Preferences - Ctrl+P or Ctrl+,",
                             "Show Track A-D - 1-4",
#ifdef Q_OS_MACOS
                             "CC favourite apply / save - Shift+1-5 / Meta+Shift+1-5",
                             "FX favourite load / append / save - Shift+F1-F5 / Alt+F1-F5 / Ctrl+Shift+F1-F5",
#else
                             "CC favourite apply / save - Shift+1-5 / Ctrl+Shift+1-5",
                             "FX favourite load / append / save - Shift+F1-F5 / Ctrl+F1-F5 / Ctrl+Shift+F1-F5",
#endif
                             "Channels (red / green / blue / alpha) - R / G / B / A",
                             "Reverse / Pause / Forward - J / K / L", "Enter Chat - Return or Enter"})
        if (!text.contains(QString::fromUtf8(want))) missing << QStringLiteral("\"%1\"").arg(want);
    reg.setPreset(QStringLiteral("classic"));
    const QString classicText = buildHelpText(reg);
    for (const QString& want : {QStringLiteral("Previous / Next LUT - L held + Up / Down"),
                                QStringLiteral("Step one frame (back / forward) - Left / Right (or X / C)"),
                                QStringLiteral("Play Reverse / Forward - < / >"), QStringLiteral("Enter Chat - Y")})
        if (!classicText.contains(want)) missing << QStringLiteral("classic \"%1\"").arg(want);
    if (classicText.contains("J / K / L")) missing << "classic shows J / K / L";
    reg.setPreset(QStringLiteral("editing"));
    check("help-runs", missing.isEmpty(), missing.join(','));

    // A family whose keys stop forming a run is listed key by key.
    reg.setOverride("color.cc-fav-apply-3", QKeySequence("Shift+9"));
    const QList<HelpLine> broken = buildHelpLines(reg);
    const QString brokenText = buildHelpText(reg);
    const QStringList brokenProblems = helpLabelProblems(reg, broken, nullptr);
    const QString saveRun = QStringLiteral("CC favourite save - %1-5").arg(reg.key("color.cc-fav-save-1").toString(QKeySequence::PortableText));
    const bool brokenOk = !brokenText.contains("apply / save") && brokenText.contains(" / Shift+9 / ") &&
                          brokenText.contains(saveRun) && brokenProblems.isEmpty();
    check("help-run-broken", brokenOk,
          brokenOk ? QString() : brokenProblems.join(',') + " | " + brokenText.section("CC favourite", 1).left(200));
    reg.clearOverride("color.cc-fav-apply-3");

    // The overlay is drawn centred and unclipped: it may grow to at most 60 lines (53 in
    // gfcPlateManager's fallback helpMessage, plus the brought-back 1.7 commands).
    check("help-fits", maxLines <= 60, QStringLiteral("lines=%1").arg(maxLines));
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

// JEFE_SHORTCUTS_DUMP=1: print both presets' keys (for the docs and reviews).
void dumpPresets(Registry& reg)
{
    if (!qEnvironmentVariableIsSet("JEFE_SHORTCUTS_DUMP")) return;
    reg.reset();
    registerAllCommands(reg);
    std::printf("---- key table: id | editing | classic ----\n");
    for (const Command& c : reg.commands())
        std::printf("%s | %s | %s\n", c.id, keysText(c.editing).toUtf8().constData(), keysText(c.classic).toUtf8().constData());
    for (const HoldKey& h : reg.holdKeys())
        std::printf("%s (hold) | %s | %s\n", h.id,
                    h.editing == Qt::Key_unknown ? "" : QKeySequence(h.editing).toString(QKeySequence::PortableText).toUtf8().constData(),
                    h.classic == Qt::Key_unknown ? "" : QKeySequence(h.classic).toString(QKeySequence::PortableText).toUtf8().constData());
    std::printf("---- end ----\n");
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
    if (files == 0) {  // a release build run away from its source tree: nothing to grep
        std::printf("SHORTCUTS-TEST tooltip-grep SKIP no source files under %s/src/qt\n", JEFE_SOURCE_DIR);
        return;
    }
    check("tooltip-grep", hits.isEmpty(), QStringLiteral("files=%1 hits=%2").arg(files).arg(hits.join(',')));
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
    testPersistTwoKeys(reg);
    testPersistUnknownPreset(reg);
    testPersistUnreadableKept(reg);
    testPrefsCancelDone(reg);
    testParity20(reg);
    testPresetEditing(reg);
    testParity17(reg);
    testClassicNoJKL(reg);
    testJKL(reg);
    testHoldWith(reg);
    testReserved(reg);
    testHoldConflict(reg);
    testDefaultPreset(reg);
    testMacosReserved(reg);
    testPresetsConflictFree(reg);
    testHelpText(reg);
    dumpPresets(reg);
    testTooltipGrep();
    reg.reset();
    std::printf("SHORTCUTS-TEST %s\n", g_fails == 0 ? "PASS" : "FAIL");
    return g_fails;
}

}  // namespace jefe::shortcuts
