#include "RemoteSessionGroups_qt.h"

#include <QSettings>

namespace jefe::qt {
namespace {

constexpr const char* kGroupsKey = "Remote/groupNames";
constexpr const char* kActiveKey = "Remote/activeGroup";
constexpr const char* kDefaultGroup = "Default";

/**
 * Group names become QSettings subkeys, so a name containing '/' would silently
 * create a nested key and make the group unloadable under its own name. Strip
 * the separators rather than rejecting the name -- the user typed a label, not
 * a path, and losing a slash is less surprising than a rejected save.
 */
QString settingsKeyFor(const QString& name) {
    QString k = name;
    k.replace('/', '_');
    k.replace('\\', '_');
    return k;
}

QString base(const QString& name) {
    return QStringLiteral("Remote/groups/") + settingsKeyFor(name) + QLatin1Char('/');
}

}  // namespace

QStringList sessionGroupNames() {
    QSettings s;
    QStringList names = s.value(kGroupsKey).toStringList();
    if (names.isEmpty()) {
        // Seed rather than return empty: every caller would otherwise need its
        // own "what if there are none" branch, and hosting always happens under
        // some group.
        names << QLatin1String(kDefaultGroup);
        s.setValue(kGroupsKey, names);
    }
    return names;
}

SessionGroup loadSessionGroup(const QString& name) {
    SessionGroup g;
    g.name = name;
    QSettings s;
    const QString b = base(name);
    g.requireKnock = s.value(b + "requireKnock", true).toBool();
    g.password = s.value(b + "password").toString();
    g.idleTimeoutMinutes = s.value(b + "idleTimeoutMinutes", 30).toInt();
    g.maxParticipants = s.value(b + "maxParticipants", 8).toInt();
    g.defaultSessionName = s.value(b + "defaultSessionName").toString();
    g.coordinatorUrl = s.value(b + "coordinatorUrl").toString();
    return g;
}

void saveSessionGroup(const SessionGroup& group) {
    if (group.name.isEmpty()) return;
    QSettings s;
    QStringList names = s.value(kGroupsKey).toStringList();
    if (!names.contains(group.name)) {
        names << group.name;
        s.setValue(kGroupsKey, names);
    }
    const QString b = base(group.name);
    s.setValue(b + "requireKnock", group.requireKnock);
    s.setValue(b + "password", group.password);
    s.setValue(b + "idleTimeoutMinutes", group.idleTimeoutMinutes);
    s.setValue(b + "maxParticipants", group.maxParticipants);
    s.setValue(b + "defaultSessionName", group.defaultSessionName);
    s.setValue(b + "coordinatorUrl", group.coordinatorUrl);
}

bool removeSessionGroup(const QString& name) {
    QSettings s;
    QStringList names = s.value(kGroupsKey).toStringList();
    // Refuse to remove the last group: hosting always happens under one, and an
    // empty list would just be re-seeded with "Default" on the next read,
    // silently discarding the user's rename.
    if (names.size() <= 1 || !names.contains(name)) return false;
    names.removeAll(name);
    s.setValue(kGroupsKey, names);
    s.remove(QStringLiteral("Remote/groups/") + settingsKeyFor(name));
    if (s.value(kActiveKey).toString() == name) {
        s.setValue(kActiveKey, names.first());
    }
    return true;
}

QString activeSessionGroup() {
    const QStringList names = sessionGroupNames();
    const QString active = QSettings().value(kActiveKey).toString();
    return names.contains(active) ? active : names.first();
}

void setActiveSessionGroup(const QString& name) {
    QSettings().setValue(kActiveKey, name);
}

}  // namespace jefe::qt
