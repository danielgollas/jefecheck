// SPDX-License-Identifier: GPL-2.0-or-later
// Keyboard shortcut registry: every key binding in the app is a Command (a QAction the
// registry owns) or a HoldKey (hold-a-key-and-drag). Keys come from the active preset
// ("editing" or "classic") unless the user has overridden them.
#pragma once

#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>

#include <functional>

class QSettings;
class QWidget;

namespace jefe::shortcuts {

// Global = Qt::ApplicationShortcut; Player = Qt::WidgetShortcut on the viewport and the
// timeline's painted widgets only.
enum class Scope { Global, Player };
enum class Category { File, Playback, InOut, Timeline, Plate, Color, Layout, Panels, Help };

struct Command {
    const char* id;                  // stable, e.g. "playback.play-pause"; never renamed once shipped
    QString name;                    // shown in Preferences, e.g. "Play / Pause"
    Category category;
    Scope scope;
    QList<QKeySequence> editing;     // keys in the Editing preset (0-2)
    QList<QKeySequence> classic;     // keys in the JefeCheck 1.7 preset (0-2)
    std::function<void()> run;
};

struct HoldKey {                     // hold-a-key-and-drag; not a QAction
    const char* id;                  // e.g. "color.drag-gamma"
    QString name;
    Qt::Key editing, classic;        // Qt::Key_unknown = unbound in that preset
    // Optional: hold this key, then press one of `with` (e.g. hold L + Up/Down).
    // Empty = hold-and-drag. Each entry runs its own action.
    QList<QPair<Qt::Key, std::function<void()>>> with;
};

class Registry : public QObject {
    Q_OBJECT
public:
    static Registry& instance();

    void registerCommand(Command c);         // at startup, before attach(); duplicate ids are rejected
    void registerHoldKey(HoldKey h);         // duplicate ids are rejected
    void bindRun(const char* id, std::function<void()> fn);  // replaces `run` for a registered id
    void attach(QWidget* window, const QList<QWidget*>& playerWidgets);

    QAction* action(const char* id) const;   // menus add these; never create their own keys
    QKeySequence key(const char* id) const;  // first current key, for tooltips / help
    QList<QKeySequence> keys(const char* id) const;  // all current keys
    QString label(const char* id) const;     // native text, e.g. "⇧I" on macOS; empty when unbound
    Qt::Key holdKey(const char* id) const;

    QString preset() const;                  // "editing" | "classic"
    // A different preset keeps no overrides; the same one changes no keys. Either way an
    // unknown preset name read by load() is replaced.
    void setPreset(const QString& preset);
    // An override equal to the preset's keys is dropped. Empty key = deliberately unbound.
    void setOverride(const char* id, const QKeySequence& key);
    void clearOverride(const char* id);
    void setHoldOverride(const char* id, Qt::Key key);  // Qt::Key_unknown = unbound
    void clearHoldOverride(const char* id);
    bool isCustom() const;                   // any command or hold override present

    // QSettings: Shortcuts/preset, Shortcuts/overrides/<id> (QKeySequence::listToString,
    // PortableText: "A; B"), Shortcuts/holdOverrides/<id>. Values this build cannot use (ids not
    // registered, key text it cannot parse, an unknown preset name) are kept verbatim and
    // written back by save(), so a newer version's settings survive a round trip.
    void load(QSettings& s);                 // replaces preset and overrides; emits changed() once
    void save(QSettings& s) const;

    // Preferences Cancel: capture and restore preset, overrides, hold overrides and orphans.
    struct Snapshot {
        QString preset;
        QHash<QString, QList<QKeySequence>> overrides;
        QHash<QString, Qt::Key> holdOverrides;
        QHash<QString, QString> orphanOverrides;
        QHash<QString, QString> orphanHoldOverrides;
        QString unknownPreset;
        QHash<QString, QString> unreadableOverrides;
        QHash<QString, QString> unreadableHoldOverrides;
    };
    Snapshot snapshot() const;
    void restore(const Snapshot& s);         // emits changed() once

    // .jcshortcuts (JSON). exportTo writes the preset and every command's current keys.
    // importFrom validates the whole file first; on failure nothing changes, returns false and
    // *error is one line. On success *error may carry a one-line note (skipped unknown ids).
    bool exportTo(const QString& path) const;
    bool importFrom(const QString& path, QString* error);

    // Ids of other commands already holding `key` in a clashing scope (same scope, or
    // Global vs Player).
    QStringList conflicts(const char* id, const QKeySequence& key) const;
    QString commandForKey(Scope scope, const QKeySequence& key) const;  // id or empty

    const QList<Command>& commands() const { return commands_; }
    const QList<HoldKey>& holdKeys() const { return holdKeys_; }

    void reset();                            // test-only: clears everything, preset back to "editing"

signals:
    void changed();                          // tooltips, help text, Preferences refresh
    void commandRan(const QString& id, Category category);

private:
    Registry() = default;
    int indexOf(const QString& id) const;
    QList<QKeySequence> effectiveKeys(int index) const;
    QList<QKeySequence> presetKeys(int index) const;
    void storeOverride(const QString& id, const QList<QKeySequence>& keys);  // drop-equal, no signal
    void storeHoldOverride(const QString& id, Qt::Key key);                  // drop-equal, no signal
    void applyKeys();                        // re-sets every action's shortcuts, emits changed() once
    void runCommand(const QString& id);

    QList<Command> commands_;
    QHash<QString, int> index_;
    QList<QAction*> actions_;                // parallel to commands_
    QList<HoldKey> holdKeys_;
    QHash<QString, int> holdIndex_;
    QHash<QString, QList<QKeySequence>> overrides_;
    QHash<QString, Qt::Key> holdOverrides_;
    QHash<QString, QString> orphanOverrides_;      // stored for ids not registered; written back
    QHash<QString, QString> orphanHoldOverrides_;
    QHash<QString, QString> unreadableOverrides_;  // registered id, text it cannot parse; written back
    QHash<QString, QString> unreadableHoldOverrides_;
    QString unknownPreset_;                        // stored preset name it did not know; written back
    QString preset_ = QStringLiteral("editing");
};

// Registers every command and hold key the app binds (ShortcutCommands_qt.cpp). Window-level
// commands are registered with run = nullptr; MainWindow_Qt supplies them with bindRun().
void registerAllCommands(Registry& reg);

// Help overlay (H) text from the registry's current keys, same sections as the fallback string
// in gfcPlateManager. Unbound commands have no line.
QString buildHelpText(const Registry& reg);

// The same text line by line: `ids` are the commands/hold keys whose keys the line shows
// (empty for headings, blanks and fixed mouse lines). helpPlacedIds() lists every id the
// overlay places, so a test can tell when a new command is missing from it.
struct HelpLine { QString text; QStringList ids; QStringList extraIds; };  // extras: "(+Shift = ...)" notes
QList<HelpLine> buildHelpLines(const Registry& reg);
QStringList helpPlacedIds();

// "Play / Pause (Space)"; just `text` when the command is unbound.
QString tooltipWithKey(const QString& text, const char* id);

// --shortcuts-test: headless registry self-test. Returns the failure count.
int shortcutsSelfTest();

}  // namespace jefe::shortcuts
