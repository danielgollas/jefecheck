// SPDX-License-Identifier: GPL-2.0-or-later
// Keyboard shortcut registry. See Shortcuts_qt.h.
#include "Shortcuts_qt.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QWidget>
#include <QtGlobal>

namespace jefe::shortcuts {

namespace {

const QString kEditing = QStringLiteral("editing");
const QString kClassic = QStringLiteral("classic");

// Global keys clash with every scope; Player keys clash with Player and Global.
// With two scopes every pair clashes; kept as a function so the rule has one home.
bool scopesClash(Scope, Scope) { return true; }

const char* const kFormat = "jefecheck-shortcuts";

QString keyText(const QKeySequence& k) { return k.toString(QKeySequence::PortableText); }
QString holdText(Qt::Key k)
{
    return k == Qt::Key_unknown ? QString() : keyText(QKeySequence(k));
}

// One shortcut: a single key plus modifiers (no chords). Empty text = unbound (valid).
bool parseKey(const QString& text, QKeySequence* out)
{
    *out = QKeySequence();
    if (text.isEmpty()) return true;
    const QKeySequence k = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (k.isEmpty() || k.count() != 1 || k[0].key() == Qt::Key_unknown) return false;
    *out = k;
    return true;
}

// A stored override: QKeySequence::listToString(keys, PortableText), so "A; B" for two keys
// and "A" for one (what earlier builds wrote). QKeySequence::listFromString splits blindly on
// "; ", which breaks when a key is ";" itself ("; ; A"), so try each "; " as the split point
// until both sides read as keys. Empty text = unbound.
QString keysSettingText(const QList<QKeySequence>& ks)
{
    return QKeySequence::listToString(ks, QKeySequence::PortableText);
}

bool parseKeyList(const QString& text, QList<QKeySequence>* out)
{
    out->clear();
    if (text.isEmpty()) return true;
    QKeySequence k;
    if (parseKey(text, &k)) {
        out->append(k);
        return true;
    }
    const QString sep = QStringLiteral("; ");
    for (int at = text.indexOf(sep, 1); at >= 0; at = text.indexOf(sep, at + 1)) {
        QList<QKeySequence> rest;
        if (parseKey(text.left(at), &k) && !k.isEmpty() && parseKeyList(text.mid(at + sep.size()), &rest) &&
            !rest.isEmpty()) {
            out->append(k);
            out->append(rest);
            return true;
        }
    }
    return false;
}

// Hold keys are a bare key (no modifiers).
bool parseHold(const QString& text, Qt::Key* out)
{
    *out = Qt::Key_unknown;
    if (text.isEmpty()) return true;
    const QKeySequence k = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (k.isEmpty() || k.count() != 1 || k[0].keyboardModifiers() != Qt::NoModifier ||
        k[0].key() == Qt::Key_unknown)
        return false;
    *out = k[0].key();
    return true;
}

// JSON value for a command: "" / "K" for 0-1 keys, an array for 2.
QJsonValue keysToJson(const QList<QKeySequence>& ks)
{
    if (ks.size() <= 1) return ks.isEmpty() ? QString() : keyText(ks.first());
    QJsonArray a;
    for (const QKeySequence& k : ks) a.append(keyText(k));
    return a;
}

bool jsonToKeys(const QJsonValue& v, QList<QKeySequence>* out)
{
    out->clear();
    QStringList texts;
    if (v.isString()) texts << v.toString();
    else if (v.isArray()) {
        for (const QJsonValue& e : v.toArray()) {
            if (!e.isString() || e.toString().isEmpty()) return false;
            texts << e.toString();
        }
    } else return false;
    for (const QString& t : texts) {
        QKeySequence k;
        if (!parseKey(t, &k)) return false;
        if (!k.isEmpty()) out->append(k);
    }
    return true;
}

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
    const auto ov = holdOverrides_.constFind(QString::fromUtf8(h.id));
    if (ov != holdOverrides_.cend()) return *ov;
    return preset_ == kClassic ? h.classic : h.editing;
}

QString Registry::preset() const { return preset_; }

void Registry::setPreset(const QString& preset)
{
    if (preset != kEditing && preset != kClassic) {
        qWarning("Shortcuts: unknown preset \"%s\"; ignored", qPrintable(preset));
        return;
    }
    unknownPreset_.clear();  // an explicit pick replaces a preset name this build did not know
    if (preset == preset_) return;
    preset_ = preset;
    overrides_.clear();
    holdOverrides_.clear();
    unreadableOverrides_.clear();
    unreadableHoldOverrides_.clear();
    applyKeys();
}

void Registry::pinPreset(const QString& preset)
{
    if (preset != kEditing && preset != kClassic) return;
    preset_ = preset;
    unknownPreset_.clear();
    overrides_.clear();
    holdOverrides_.clear();
    unreadableOverrides_.clear();
    unreadableHoldOverrides_.clear();
    applyKeys();
}

QList<QKeySequence> Registry::presetKeys(int i) const
{
    return preset_ == kClassic ? commands_[i].classic : commands_[i].editing;
}

void Registry::storeOverride(const QString& id, const QList<QKeySequence>& keys)
{
    const int i = indexOf(id);
    unreadableOverrides_.remove(id);
    if (i >= 0 && keys == presetKeys(i)) overrides_.remove(id);
    else overrides_.insert(id, keys);
}

void Registry::storeHoldOverride(const QString& id, Qt::Key key)
{
    const int i = holdIndex_.value(id, -1);
    const Qt::Key preset = i < 0 ? Qt::Key_unknown
                                 : (preset_ == kClassic ? holdKeys_[i].classic : holdKeys_[i].editing);
    unreadableHoldOverrides_.remove(id);
    if (i >= 0 && key == preset) holdOverrides_.remove(id);
    else holdOverrides_.insert(id, key);
}

void Registry::setOverride(const char* id, const QKeySequence& key)
{
    const QString sid = QString::fromUtf8(id);
    if (indexOf(sid) < 0) {
        qWarning("Shortcuts: override for unknown command \"%s\"", id);
        return;
    }
    const auto before = overrides_.value(sid);
    const bool had = overrides_.contains(sid);
    storeOverride(sid, key.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{key});
    if (had == overrides_.contains(sid) && before == overrides_.value(sid)) return;
    applyKeys();
}

void Registry::clearOverride(const char* id)
{
    unreadableOverrides_.remove(QString::fromUtf8(id));
    if (overrides_.remove(QString::fromUtf8(id)) > 0) applyKeys();
}

void Registry::setHoldOverride(const char* id, Qt::Key key)
{
    const QString sid = QString::fromUtf8(id);
    if (!holdIndex_.contains(sid)) {
        qWarning("Shortcuts: override for unknown hold key \"%s\"", id);
        return;
    }
    const bool had = holdOverrides_.contains(sid);
    const Qt::Key before = holdOverrides_.value(sid, Qt::Key_unknown);
    storeHoldOverride(sid, key);
    if (had == holdOverrides_.contains(sid) && before == holdOverrides_.value(sid, Qt::Key_unknown))
        return;
    applyKeys();
}

void Registry::clearHoldOverride(const char* id)
{
    unreadableHoldOverrides_.remove(QString::fromUtf8(id));
    if (holdOverrides_.remove(QString::fromUtf8(id)) > 0) applyKeys();
}

bool Registry::isCustom() const { return !overrides_.isEmpty() || !holdOverrides_.isEmpty(); }

void Registry::load(QSettings& s)
{
    const QString p = s.value(QStringLiteral("Shortcuts/preset"), defaultPreset_).toString();
    // A preset this build does not know (a later version's) behaves as the default but is
    // written back as it was until the user picks one.
    preset_ = (p == kClassic || p == kEditing) ? p : defaultPreset_;
    unknownPreset_ = (p == kClassic || p == kEditing) ? QString() : p;
    overrides_.clear();
    holdOverrides_.clear();
    orphanOverrides_.clear();
    orphanHoldOverrides_.clear();
    unreadableOverrides_.clear();
    unreadableHoldOverrides_.clear();

    s.beginGroup(QStringLiteral("Shortcuts/overrides"));
    for (const QString& id : s.childKeys()) {
        const QString text = s.value(id).toString();
        if (indexOf(id) < 0) {
            orphanOverrides_.insert(id, text);
            continue;
        }
        QList<QKeySequence> ks;
        if (!parseKeyList(text, &ks)) {
            // Unreadable here (a later version's key name?): the preset applies, the text is kept.
            unreadableOverrides_.insert(id, text);
            continue;
        }
        storeOverride(id, ks);
    }
    s.endGroup();

    s.beginGroup(QStringLiteral("Shortcuts/holdOverrides"));
    for (const QString& id : s.childKeys()) {
        const QString text = s.value(id).toString();
        if (!holdIndex_.contains(id)) {
            orphanHoldOverrides_.insert(id, text);
            continue;
        }
        Qt::Key k;
        if (!parseHold(text, &k)) {
            unreadableHoldOverrides_.insert(id, text);
            continue;
        }
        storeHoldOverride(id, k);
    }
    s.endGroup();
    applyKeys();
}

void Registry::save(QSettings& s) const
{
    s.setValue(QStringLiteral("Shortcuts/preset"), unknownPreset_.isEmpty() ? preset_ : unknownPreset_);
    s.remove(QStringLiteral("Shortcuts/overrides"));
    s.remove(QStringLiteral("Shortcuts/holdOverrides"));
    for (auto it = overrides_.cbegin(); it != overrides_.cend(); ++it)
        s.setValue(QStringLiteral("Shortcuts/overrides/") + it.key(),
                   keysSettingText(it.value()));
    for (const auto* raw : {&orphanOverrides_, &unreadableOverrides_})
        for (auto it = raw->cbegin(); it != raw->cend(); ++it)
            s.setValue(QStringLiteral("Shortcuts/overrides/") + it.key(), it.value());
    for (auto it = holdOverrides_.cbegin(); it != holdOverrides_.cend(); ++it)
        s.setValue(QStringLiteral("Shortcuts/holdOverrides/") + it.key(), holdText(it.value()));
    for (const auto* raw : {&orphanHoldOverrides_, &unreadableHoldOverrides_})
        for (auto it = raw->cbegin(); it != raw->cend(); ++it)
            s.setValue(QStringLiteral("Shortcuts/holdOverrides/") + it.key(), it.value());
}

Registry::Snapshot Registry::snapshot() const
{
    return {preset_,           overrides_,          holdOverrides_,
            orphanOverrides_,  orphanHoldOverrides_, unknownPreset_,
            unreadableOverrides_, unreadableHoldOverrides_};
}

void Registry::restore(const Snapshot& s)
{
    preset_ = s.preset == kClassic ? kClassic : kEditing;
    overrides_ = s.overrides;
    holdOverrides_ = s.holdOverrides;
    orphanOverrides_ = s.orphanOverrides;
    orphanHoldOverrides_ = s.orphanHoldOverrides;
    unknownPreset_ = s.unknownPreset;
    unreadableOverrides_ = s.unreadableOverrides;
    unreadableHoldOverrides_ = s.unreadableHoldOverrides;
    applyKeys();
}

bool Registry::exportTo(const QString& path) const
{
    QJsonObject keys, hold;
    for (int i = 0; i < commands_.size(); ++i)
        keys.insert(QString::fromUtf8(commands_[i].id), keysToJson(effectiveKeys(i)));
    for (const HoldKey& h : holdKeys_)
        hold.insert(QString::fromUtf8(h.id), holdText(holdKey(h.id)));
    QJsonObject root;
    root.insert(QStringLiteral("format"), QString::fromLatin1(kFormat));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("basedOn"), preset_);
    root.insert(QStringLiteral("keys"), keys);
    root.insert(QStringLiteral("hold"), hold);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool Registry::importFrom(const QString& path, QString* error)
{
    QString dummy;
    QString& err = error ? *error : dummy;
    err.clear();
    auto fail = [&err](const QString& msg) { err = msg; return false; };

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail(QStringLiteral("Cannot read the file."));
    const QByteArray data = f.readAll();
    if (data.trimmed().isEmpty()) return fail(QStringLiteral("The file is empty."));
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("Not a shortcuts file (invalid JSON)."));
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kFormat))
        return fail(QStringLiteral("Not a JefeCheck shortcuts file."));
    const QJsonValue ver = root.value(QStringLiteral("version"));
    if (!ver.isDouble() || ver.toInt() < 1)
        return fail(QStringLiteral("The file has no valid version."));
    if (ver.toInt() > 1)
        return fail(QStringLiteral("The file was made by a newer JefeCheck (version %1).").arg(ver.toInt()));
    const QJsonValue based = root.value(QStringLiteral("basedOn"));
    QString newPreset = kEditing;
    if (!based.isUndefined()) {
        newPreset = based.toString();
        if (newPreset != kEditing && newPreset != kClassic)
            return fail(QStringLiteral("Unknown preset in the file."));
    }
    const QJsonValue keysV = root.value(QStringLiteral("keys"));
    const QJsonValue holdV = root.value(QStringLiteral("hold"));
    if ((!keysV.isUndefined() && !keysV.isObject()) || (!holdV.isUndefined() && !holdV.isObject()))
        return fail(QStringLiteral("The file's key lists are malformed."));

    // Validate everything before touching any state.
    QHash<QString, QList<QKeySequence>> newKeys;
    QHash<QString, Qt::Key> newHold;
    int unknown = 0;
    const QJsonObject keysO = keysV.toObject();
    for (auto it = keysO.begin(); it != keysO.end(); ++it) {
        QList<QKeySequence> ks;
        if (!jsonToKeys(it.value(), &ks))
            return fail(QStringLiteral("Cannot read the key for \"%1\".").arg(it.key().left(80)));
        if (indexOf(it.key()) < 0) ++unknown;
        else newKeys.insert(it.key(), ks);
    }
    const QJsonObject holdO = holdV.toObject();
    for (auto it = holdO.begin(); it != holdO.end(); ++it) {
        Qt::Key k;
        if (!it.value().isString() || !parseHold(it.value().toString(), &k))
            return fail(QStringLiteral("Cannot read the hold key for \"%1\".").arg(it.key().left(80)));
        if (!holdIndex_.contains(it.key())) ++unknown;
        else newHold.insert(it.key(), k);
    }

    preset_ = newPreset;
    unknownPreset_.clear();
    overrides_.clear();
    holdOverrides_.clear();
    unreadableOverrides_.clear();
    unreadableHoldOverrides_.clear();
    for (auto it = newKeys.cbegin(); it != newKeys.cend(); ++it) storeOverride(it.key(), it.value());
    for (auto it = newHold.cbegin(); it != newHold.cend(); ++it) storeHoldOverride(it.key(), it.value());
    applyKeys();
    if (unknown > 0)
        err = QStringLiteral("%1 unknown shortcut id(s) in the file were skipped.").arg(unknown);
    return true;
}

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
    const int selfHold = holdIndex_.value(sid, -1);
    // An unregistered id is treated as Player, the narrower scope (it still clashes with Global).
    const Scope scope = self < 0 ? Scope::Player : commands_[self].scope;
    for (int i = 0; i < commands_.size(); ++i) {
        if (i == self || !scopesClash(scope, commands_[i].scope)) continue;
        if (effectiveKeys(i).contains(key)) out << QString::fromUtf8(commands_[i].id);
    }
    // A hold key is a bare key: it clashes with a command on that bare key and with another hold.
    const QKeyCombination kc = key[0];
    if (key.count() == 1 && kc.keyboardModifiers() == Qt::NoModifier) {
        for (int i = 0; i < holdKeys_.size(); ++i) {
            if (i == selfHold) continue;
            if (holdKey(holdKeys_[i].id) == kc.key()) out << QString::fromUtf8(holdKeys_[i].id);
        }
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

void Registry::setDefaultPreset(const QString& preset)
{
    if (preset != kEditing && preset != kClassic) {
        qWarning("Shortcuts: unknown default preset \"%s\"; ignored", qPrintable(preset));
        return;
    }
    defaultPreset_ = preset;
}

const std::function<void()>* Registry::holdWithEntry(const QSet<int>& held, int key,
                                                     Qt::KeyboardModifiers mods) const
{
    if ((mods & ~Qt::KeypadModifier) != Qt::NoModifier) return nullptr;
    for (const HoldKey& h : holdKeys_) {
        if (h.with.isEmpty()) continue;
        const Qt::Key hk = holdKey(h.id);
        if (hk == Qt::Key_unknown || !held.contains(int(hk))) continue;
        for (const auto& w : h.with)
            if (int(w.first) == key) return &w.second;
    }
    return nullptr;
}

bool Registry::holdWithClaims(const QSet<int>& held, int key, Qt::KeyboardModifiers mods) const
{
    return holdWithEntry(held, key, mods) != nullptr;
}

bool Registry::runHoldWith(const QSet<int>& held, int key, Qt::KeyboardModifiers mods)
{
    const std::function<void()>* entry = holdWithEntry(held, key, mods);
    if (!entry) return false;
    const std::function<void()> fn = *entry;  // copy: fn may re-enter the registry
    if (fn) fn();
    return true;
}

void Registry::bindHoldWith(const char* holdId, Qt::Key key, std::function<void()> fn)
{
    const int i = holdIndex_.value(QString::fromUtf8(holdId), -1);
    if (i < 0) {
        qWarning("Shortcuts: bindHoldWith for unknown hold key \"%s\"", holdId);
        return;
    }
    for (auto& w : holdKeys_[i].with)
        if (w.first == key) {
            w.second = std::move(fn);
            return;
        }
    holdKeys_[i].with.append({key, std::move(fn)});
}

void runJKL(int direction, const Transport& t)
{
    if (direction == 0) {
        if (t.isPlaying()) t.togglePause();
        return;
    }
    t.setDirection(direction > 0 ? +1 : -1);
    if (!t.isPlaying()) t.togglePause();
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
    holdOverrides_.clear();
    orphanOverrides_.clear();
    orphanHoldOverrides_.clear();
    unreadableOverrides_.clear();
    unreadableHoldOverrides_.clear();
    unknownPreset_.clear();
    preset_ = kEditing;
    defaultPreset_ = kEditing;
}

}  // namespace jefe::shortcuts
