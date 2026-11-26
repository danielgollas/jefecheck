// SPDX-License-Identifier: GPL-2.0-or-later
// Keyboard shortcut registry. See Shortcuts_qt.h.
#include "Shortcuts_qt.h"

#include <QWidget>
#include <QtGlobal>

namespace jefe::shortcuts {

namespace {

const QString kEditing = QStringLiteral("editing");
const QString kClassic = QStringLiteral("classic");

// Global keys clash with every scope; Player keys clash with Player and Global.
// With two scopes every pair clashes; kept as a function so the rule has one home.
bool scopesClash(Scope, Scope) { return true; }

}  // namespace

Registry& Registry::instance()
{
    static Registry* reg = new Registry();  // lives for the app; never destroyed at exit
    return *reg;
}

int Registry::indexOf(const QString& id) const { return index_.value(id, -1); }

void Registry::registerCommand(Command c)
{
    const QString id = QString::fromUtf8(c.id ? c.id : "");
    if (id.isEmpty() || index_.contains(id)) {
        qWarning("Shortcuts: command id \"%s\" is empty or already registered; ignored",
                 qPrintable(id));
        return;
    }

    auto* a = new QAction(c.name, this);
    a->setObjectName(QStringLiteral("shortcut.") + id);
    a->setShortcutContext(c.scope == Scope::Global ? Qt::ApplicationShortcut : Qt::WidgetShortcut);
    connect(a, &QAction::triggered, this, [this, id] { runCommand(id); });

    index_.insert(id, int(commands_.size()));
    commands_.append(std::move(c));
    actions_.append(a);
    a->setShortcuts(effectiveKeys(int(commands_.size()) - 1));
}

void Registry::registerHoldKey(HoldKey h)
{
    const QString id = QString::fromUtf8(h.id ? h.id : "");
    if (id.isEmpty() || holdIndex_.contains(id)) {
        qWarning("Shortcuts: hold-key id \"%s\" is empty or already registered; ignored",
                 qPrintable(id));
        return;
    }
    holdIndex_.insert(id, int(holdKeys_.size()));
    holdKeys_.append(std::move(h));
}

void Registry::bindRun(const char* id, std::function<void()> fn)
{
    const int i = indexOf(QString::fromUtf8(id));
    if (i < 0) {
        qWarning("Shortcuts: bindRun for unknown command \"%s\"", id);
        return;
    }
    commands_[i].run = std::move(fn);
}

void Registry::attach(QWidget* window, const QList<QWidget*>& playerWidgets)
{
    for (int i = 0; i < commands_.size(); ++i) {
        QAction* a = actions_[i];
        if (commands_[i].scope == Scope::Global) {
            if (window) window->addAction(a);
        } else {
            for (QWidget* w : playerWidgets)
                if (w) w->addAction(a);
        }
    }
}

void Registry::runCommand(const QString& id)
{
    const int i = indexOf(id);
    if (i < 0) return;
    const Category category = commands_[i].category;
    // Copy: run() may re-enter the registry (e.g. bindRun) and invalidate the reference.
    const std::function<void()> fn = commands_[i].run;
    if (fn) fn();
    emit commandRan(id, category);
}

QAction* Registry::action(const char* id) const
{
    const int i = indexOf(QString::fromUtf8(id));
    return i < 0 ? nullptr : actions_[i];
}

QList<QKeySequence> Registry::effectiveKeys(int i) const
{
    const Command& c = commands_[i];
    const auto ov = overrides_.constFind(QString::fromUtf8(c.id));
    if (ov != overrides_.cend()) return *ov;
    return preset_ == kClassic ? c.classic : c.editing;
}

QList<QKeySequence> Registry::keys(const char* id) const
{
    const int i = indexOf(QString::fromUtf8(id));
    return i < 0 ? QList<QKeySequence>{} : effectiveKeys(i);
}

QKeySequence Registry::key(const char* id) const
{
    const QList<QKeySequence> ks = keys(id);
    return ks.isEmpty() ? QKeySequence() : ks.first();
}

QString Registry::label(const char* id) const
{
    return key(id).toString(QKeySequence::NativeText);
}

Qt::Key Registry::holdKey(const char* id) const
{
    const int i = holdIndex_.value(QString::fromUtf8(id), -1);
    if (i < 0) return Qt::Key_unknown;
    const HoldKey& h = holdKeys_[i];
    return preset_ == kClassic ? h.classic : h.editing;
}

QString Registry::preset() const { return preset_; }

void Registry::setPreset(const QString& preset)
{
    if (preset != kEditing && preset != kClassic) {
        qWarning("Shortcuts: unknown preset \"%s\"; ignored", qPrintable(preset));
        return;
    }
    preset_ = preset;
    overrides_.clear();
    applyKeys();
}

void Registry::setOverride(const char* id, const QKeySequence& key)
{
    const QString sid = QString::fromUtf8(id);
    if (indexOf(sid) < 0) {
        qWarning("Shortcuts: override for unknown command \"%s\"", id);
        return;
    }
    overrides_.insert(sid, key.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{key});
    applyKeys();
}

void Registry::clearOverride(const char* id)
{
    if (overrides_.remove(QString::fromUtf8(id)) > 0) applyKeys();
}

bool Registry::isCustom() const { return !overrides_.isEmpty(); }

void Registry::applyKeys()
{
    for (int i = 0; i < commands_.size(); ++i) actions_[i]->setShortcuts(effectiveKeys(i));
    emit changed();
}

QStringList Registry::conflicts(const char* id, const QKeySequence& key) const
{
    QStringList out;
    if (key.isEmpty()) return out;
    const QString sid = QString::fromUtf8(id);
    const int self = indexOf(sid);
    // An unregistered id is treated as Player, the narrower scope (it still clashes with Global).
    const Scope scope = self < 0 ? Scope::Player : commands_[self].scope;
    for (int i = 0; i < commands_.size(); ++i) {
        if (i == self || !scopesClash(scope, commands_[i].scope)) continue;
        if (effectiveKeys(i).contains(key)) out << QString::fromUtf8(commands_[i].id);
    }
    return out;
}

QString Registry::commandForKey(Scope scope, const QKeySequence& key) const
{
    if (key.isEmpty()) return {};
    for (int i = 0; i < commands_.size(); ++i)
        if (commands_[i].scope == scope && effectiveKeys(i).contains(key))
            return QString::fromUtf8(commands_[i].id);
    return {};
}

void Registry::reset()
{
    qDeleteAll(actions_);
    actions_.clear();
    commands_.clear();
    index_.clear();
    holdKeys_.clear();
    holdIndex_.clear();
    overrides_.clear();
    preset_ = kEditing;
}

}  // namespace jefe::shortcuts
