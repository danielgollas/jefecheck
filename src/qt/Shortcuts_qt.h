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
    void setPreset(const QString& preset);   // keeps no overrides
    void setOverride(const char* id, const QKeySequence& key);  // empty = unbound
    void clearOverride(const char* id);
    bool isCustom() const;                   // any override present

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
    void applyKeys();                        // re-sets every action's shortcuts, emits changed() once
    void runCommand(const QString& id);

    QList<Command> commands_;
    QHash<QString, int> index_;
    QList<QAction*> actions_;                // parallel to commands_
    QList<HoldKey> holdKeys_;
    QHash<QString, int> holdIndex_;
    QHash<QString, QList<QKeySequence>> overrides_;
    QString preset_ = QStringLiteral("editing");
};

// --shortcuts-test: headless registry self-test. Returns the failure count.
int shortcutsSelfTest();

}  // namespace jefe::shortcuts
