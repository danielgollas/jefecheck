// SPDX-License-Identifier: GPL-2.0-or-later
// Preferences -> Shortcuts keyboard map. See ShortcutKeyboard_qt.h.
#include "ShortcutKeyboard_qt.h"

#include "UiMetrics_qt.h"

#include <QApplication>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSet>
#include <QToolTip>

#include <algorithm>

namespace jefe::shortcuts {

namespace {

// Theme colours (jefecheck_dark.qss): widget bg, border, text, muted text, accent.
const QColor kMapBg(0x1c, 0x1c, 0x1c);
const QColor kMapBorder(0x3a, 0x3a, 0x3a);
const QColor kCapBg(0x2a, 0x2a, 0x2a);
const QColor kCapBorder(0x3a, 0x3a, 0x3a);
const QColor kModCapBg(0x24, 0x24, 0x24);
const QColor kText(0xe0, 0xe0, 0xe0);
const QColor kMuted(0xa0, 0xa0, 0xa0);
const QColor kDim(0x6a, 0x6a, 0x6a);
const QColor kAccent(0xd4, 0x77, 0x1e);
const QColor kAccentFill(0x2e, 0x26, 0x20);
const QColor kAccentText(0xe8, 0xb8, 0x84);

constexpr qreal kTotalUnits = 16;    // 15 for the main block, 1 for the navigation column
constexpr qreal kRowUnits = 6.25;    // F-row, 0.25 gap, five rows
constexpr int kRowH = 38;            // two 13 px lines: the key's name and the command's
constexpr qreal kMinUnit = 24;
constexpr qreal kPrefUnit = 46;
constexpr qreal kCapGap = 1.5;       // each side, so keys are 3 px apart

QColor mix(const QColor& a, const QColor& b, qreal t)  // t of a over b
{
    return QColor::fromRgbF(float(a.redF() * t + b.redF() * (1 - t)), float(a.greenF() * t + b.greenF() * (1 - t)),
                            float(a.blueF() * t + b.blueF() * (1 - t)));
}

bool isModifierCap(Qt::Key k)
{
    return k == Qt::Key_Shift || k == Qt::Key_Control || k == Qt::Key_Alt || k == Qt::Key_Meta ||
           k == Qt::Key_CapsLock;
}

Qt::KeyboardModifier modifierOfKey(int k)
{
    switch (k) {
    case Qt::Key_Shift: return Qt::ShiftModifier;
    case Qt::Key_Control: return Qt::ControlModifier;
    case Qt::Key_Alt: return Qt::AltModifier;
    case Qt::Key_Meta: return Qt::MetaModifier;
    default: return Qt::NoModifier;
    }
}

Qt::KeyboardModifiers layerMask()
{
    Qt::KeyboardModifiers m;
    for (Qt::KeyboardModifier x : layerModifiers()) m |= x;
    return m;
}

Category categoryOfId(const QString& id)
{
    for (const Command& c : Registry::instance().commands())
        if (id == QLatin1String(c.id)) return c.category;
    if (id.startsWith(QLatin1String("color."))) return Category::Color;
    if (id.startsWith(QLatin1String("playback."))) return Category::Playback;
    return Category::Plate;
}

// Layer order: fewer modifiers first, then by layerModifiers() order.
int layerRank(Qt::KeyboardModifiers mods)
{
    const QList<Qt::KeyboardModifier> order = layerModifiers();
    int count = 0, rank = 0;
    for (int i = 0; i < order.size(); ++i)
        if (mods & order[i]) {
            ++count;
            rank = rank * 8 + (i + 1);
        }
    return count * 100000 + rank;
}

}  // namespace

QList<KeyCap> ansiLayout()
{
#ifdef Q_OS_MACOS
    const bool mac = true;
#else
    const bool mac = false;
#endif
    auto L = [mac](const char* m, const char* o) { return QString::fromUtf8(mac ? m : o); };
    QList<KeyCap> caps;
    auto add = [&caps](Qt::Key k, const QString& label, float x, float y, float w = 1.0f) {
        caps.push_back({k, label, x, y, w});
    };
    auto row = [&](const char* chars, const QList<Qt::Key>& keys, float x0, float y) {
        const QString text = QString::fromLatin1(chars);
        for (int i = 0; i < keys.size(); ++i) add(keys[i], text.mid(i, 1), x0 + i, y);
    };

    // F-row
    add(Qt::Key_Escape, L("esc", "Esc"), 0, 0);
    for (int i = 0; i < 12; ++i) {
        const float x = 2 + i + (i >= 4 ? 0.5f : 0) + (i >= 8 ? 0.5f : 0);
        add(Qt::Key(Qt::Key_F1 + i), QStringLiteral("F%1").arg(i + 1), x, 0);
    }
    // Number row
    row("`1234567890-=",
        {Qt::Key_QuoteLeft, Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5, Qt::Key_6, Qt::Key_7, Qt::Key_8,
         Qt::Key_9, Qt::Key_0, Qt::Key_Minus, Qt::Key_Equal},
        0, 1.25f);
    add(Qt::Key_Backspace, L("⌫", "Backspace"), 13, 1.25f, 2);
    // Q row
    add(Qt::Key_Tab, L("⇥", "Tab"), 0, 2.25f, 1.5f);
    row("QWERTYUIOP[]",
        {Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T, Qt::Key_Y, Qt::Key_U, Qt::Key_I, Qt::Key_O, Qt::Key_P,
         Qt::Key_BracketLeft, Qt::Key_BracketRight},
        1.5f, 2.25f);
    add(Qt::Key_Backslash, QStringLiteral("\\"), 13.5f, 2.25f, 1.5f);
    // A row
    add(Qt::Key_CapsLock, L("⇪", "Caps"), 0, 3.25f, 1.75f);
    row("ASDFGHJKL;'",
        {Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G, Qt::Key_H, Qt::Key_J, Qt::Key_K, Qt::Key_L,
         Qt::Key_Semicolon, Qt::Key_Apostrophe},
        1.75f, 3.25f);
    add(Qt::Key_Return, L("⏎", "Enter"), 12.75f, 3.25f, 2.25f);
    // Z row
    add(Qt::Key_Shift, L("⇧", "Shift"), 0, 4.25f, 2.25f);
    row("ZXCVBNM,./",
        {Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B, Qt::Key_N, Qt::Key_M, Qt::Key_Comma, Qt::Key_Period,
         Qt::Key_Slash},
        2.25f, 4.25f);
    add(Qt::Key_Shift, L("⇧", "Shift"), 12.25f, 4.25f, 1.75f);
    // Bottom row. On macOS Qt's Control is Cmd and Meta is the Control key.
    if (mac) {
        add(Qt::Key_Meta, QStringLiteral("⌃"), 0, 5.25f, 1.25f);
        add(Qt::Key_Alt, QStringLiteral("⌥"), 1.25f, 5.25f, 1.25f);
        add(Qt::Key_Control, QStringLiteral("⌘"), 2.5f, 5.25f, 1.5f);
        add(Qt::Key_Space, QStringLiteral("space"), 4, 5.25f, 5.5f);
        add(Qt::Key_Control, QStringLiteral("⌘"), 9.5f, 5.25f, 1.5f);
        add(Qt::Key_Alt, QStringLiteral("⌥"), 11, 5.25f, 1.25f);
    } else {
        add(Qt::Key_Control, QStringLiteral("Ctrl"), 0, 5.25f, 1.5f);
        add(Qt::Key_Alt, QStringLiteral("Alt"), 1.5f, 5.25f, 1.25f);
        add(Qt::Key_Space, QStringLiteral("Space"), 2.75f, 5.25f, 6.5f);
        add(Qt::Key_Alt, QStringLiteral("Alt"), 9.25f, 5.25f, 1.25f);
        add(Qt::Key_Control, QStringLiteral("Ctrl"), 10.5f, 5.25f, 1.5f);
    }
    // Arrows (inverted T at the bottom right) and a navigation column on the right edge.
    add(Qt::Key_Up, QStringLiteral("↑"), 14, 4.25f);
    add(Qt::Key_Left, QStringLiteral("←"), 13, 5.25f);
    add(Qt::Key_Down, QStringLiteral("↓"), 14, 5.25f);
    add(Qt::Key_Right, QStringLiteral("→"), 15, 5.25f);
    add(Qt::Key_Delete, L("⌦", "Del"), 15, 0);
    add(Qt::Key_Home, QStringLiteral("Home"), 15, 1.25f);
    add(Qt::Key_End, QStringLiteral("End"), 15, 2.25f);
    add(Qt::Key_PageUp, QStringLiteral("PgUp"), 15, 3.25f);
    add(Qt::Key_PageDown, QStringLiteral("PgDn"), 15, 4.25f);
    return caps;
}

bool capForKey(const QKeyCombination& key, Qt::Key* cap, Qt::KeyboardModifiers* mods)
{
    static const QSet<int> onMap = [] {
        QSet<int> s;
        for (const KeyCap& c : ansiLayout())
            if (!isModifierCap(c.key)) s.insert(c.key);
        return s;
    }();
    static const QHash<int, int> shifted = {
        {Qt::Key_AsciiTilde, Qt::Key_QuoteLeft}, {Qt::Key_Exclam, Qt::Key_1},      {Qt::Key_At, Qt::Key_2},
        {Qt::Key_NumberSign, Qt::Key_3},         {Qt::Key_Dollar, Qt::Key_4},      {Qt::Key_Percent, Qt::Key_5},
        {Qt::Key_AsciiCircum, Qt::Key_6},        {Qt::Key_Ampersand, Qt::Key_7},   {Qt::Key_Asterisk, Qt::Key_8},
        {Qt::Key_ParenLeft, Qt::Key_9},          {Qt::Key_ParenRight, Qt::Key_0},  {Qt::Key_Underscore, Qt::Key_Minus},
        {Qt::Key_Plus, Qt::Key_Equal},           {Qt::Key_BraceLeft, Qt::Key_BracketLeft},
        {Qt::Key_BraceRight, Qt::Key_BracketRight}, {Qt::Key_Bar, Qt::Key_Backslash},
        {Qt::Key_Colon, Qt::Key_Semicolon},      {Qt::Key_QuoteDbl, Qt::Key_Apostrophe},
        {Qt::Key_Less, Qt::Key_Comma},           {Qt::Key_Greater, Qt::Key_Period}, {Qt::Key_Question, Qt::Key_Slash},
    };
    int k = key.key();
    Qt::KeyboardModifiers m = key.keyboardModifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier |
                                                         Qt::MetaModifier);
    if (k == Qt::Key_Enter) k = Qt::Key_Return;
    if (k == Qt::Key_Backtab) {
        k = Qt::Key_Tab;
        m |= Qt::ShiftModifier;
    }
    if (const auto it = shifted.constFind(k); it != shifted.constEnd()) {
        k = it.value();
        m |= Qt::ShiftModifier;
    }
    if (!onMap.contains(k)) return false;
    if (cap) *cap = Qt::Key(k);
    if (mods) *mods = m;
    return true;
}

QString shortCommandName(const QString& id, const QString& name)
{
    static const QHash<QString, QString> table = {
        {"playback.play-pause", "Play"},           {"playback.jkl-reverse", "Play Back"},
        {"playback.jkl-pause", "Pause"},           {"playback.jkl-forward", "Play Fwd"},
        {"playback.play-reverse", "Reverse"},      {"playback.play-forward", "Forward"},
        {"playback.step-back", "Step Back"},       {"playback.step-forward", "Step Fwd"},
        {"playback.go-start", "Start"},            {"playback.go-end", "End"},
        {"playback.direction-reverse", "Dir Rev"}, {"playback.direction-forward", "Dir Fwd"},
        {"playback.loop-once", "Once"},            {"playback.loop", "Loop"},
        {"playback.bounce", "Bounce"},
        {"inout.set-in", "Set In"},                {"inout.set-out", "Set Out"},
        {"inout.go-in", "Go to In"},               {"inout.go-out", "Go to Out"},
        {"inout.reset-in", "Clear In"},            {"inout.reset-out", "Clear Out"},
        {"inout.clear-both", "Clear I/O"},         {"inout.set-in-load", "In + Load"},
        {"plate.track-prev", "Prev Track"},        {"plate.track-next", "Next Track"},
        {"color.channel-r", "Red"},                {"color.channel-g", "Green"},
        {"color.channel-b", "Blue"},               {"color.channel-a", "Alpha"},
        {"plate.lut-prev", "Prev LUT"},            {"plate.lut-next", "Next LUT"},
        {"timeline.zoom-in", "Zoom In"},           {"timeline.zoom-out", "Zoom Out"},
        {"timeline.zoom-fit", "Zoom Fit"},         {"timeline.zoom-inout", "Zoom I/O"},
        {"remote.chat-type", "Chat"},              {"remote.chat-toggle", "Chat Show"},
        {"layout.single", "Single"},               {"layout.side", "Side"},
        {"layout.top-bottom", "Top/Bot"},          {"layout.quad", "2 x 2"},
        {"plate.fit", "Fit"},                      {"plate.fit-all", "Fit All"},
        {"plate.flip", "Flip"},                    {"plate.flip-all", "Flip All"},
        {"plate.flop", "Flop"},                    {"plate.flop-all", "Flop All"},
        {"plate.text-mode", "Text"},               {"plate.text-mode-all", "Text All"},
        {"plate.reset", "Reset"},                  {"plate.reset-all", "Reset All"},
        {"view.toggle-histogram", "Histogram"},    {"view.toggle-histogram-all", "Histo All"},
        {"view.toggle-note-overlay", "Notes"},     {"color.reset", "Reset CC"},
        {"color.reset-all", "Reset CCs"},          {"file.quick-load", "Load"},
        {"file.load-manager", "Load Mgr"},         {"file.save-session", "Save"},
        {"file.open-session", "Open"},             {"file.preferences", "Prefs"},
        {"file.quit", "Quit"},                     {"view.toggle-fullscreen", "Full Scr"},
        {"panels.toggle-plate-manager", "Plates"}, {"panels.toggle-fx", "FX"},
        {"panels.toggle-luts", "LUTs"},            {"panels.open-remote", "Remote"},
        {"panels.open-render", "Render"},          {"panels.toggle-notes", "Notes"},
        {"panels.toggle-timeline", "Timeline"},    {"panels.toggle-playlist", "Playlist"},
        {"panels.hide-controls", "Hide UI"},       {"panels.minimize", "Minimize"},
        {"help.manual", "Manual"},                 {"help.toggle-overlay", "Help"},
        {"color.drag-gamma", "Gamma"},             {"color.drag-exposure", "Exposure"},
        {"color.drag-brightness", "Bright"},       {"color.drag-contrast", "Contrast"},
        {"color.drag-saturation", "Sat"},          {"plate.lut-hold", "LUT"},
    };
    if (const auto it = table.constFind(id); it != table.constEnd()) return it.value();
    // Numbered families: "color.cc-fav-apply-3" -> "CC 3".
    static const QList<QPair<QString, QString>> families = {
        {"color.cc-fav-apply-", "CC %1"}, {"color.cc-fav-save-", "Save CC %1"}, {"fx.fav-load-", "FX %1"},
        {"fx.fav-append-", "+FX %1"},     {"fx.fav-save-", "Save FX %1"},       {"plate.show-track-", "Track %1"},
    };
    for (const auto& f : families)
        if (id.startsWith(f.first)) return f.second.arg(id.mid(f.first.size()));
    // Anything else: the name without "(…)", a "Group: " prefix or a trailing ellipsis.
    QString s = name;
    if (const int p = s.indexOf(QLatin1Char('(')); p > 0) s = s.left(p);
    if (const int c = s.indexOf(QLatin1String(": ")); c >= 0) s = s.mid(c + 2);
    s.remove(QChar(0x2026));
    return s.trimmed();
}

QList<Category> allCategories()
{
    return {Category::File,  Category::Playback, Category::InOut,  Category::Timeline, Category::Plate,
            Category::Color, Category::Layout,   Category::Panels, Category::Help};
}

QColor categoryColor(Category c)
{
    // The theme accent (#d4771e, hue 29) turned round the colour wheel, one step per category,
    // at the accent's saturation and lightness.
    const QList<Category> all = allCategories();
    const int i = int(all.indexOf(c));
    const int hue = (kAccent.hslHue() + i * 40) % 360;
    return QColor::fromHsl(hue, kAccent.hslSaturation(), kAccent.lightness());
}

QColor categoryTint(Category c) { return mix(categoryColor(c), kCapBg, 0.36); }

QString categoryDisplayName(Category c)
{
    switch (c) {
    case Category::File: return QObject::tr("File");
    case Category::Playback: return QObject::tr("Playback");
    case Category::InOut: return QObject::tr("In / Out");
    case Category::Timeline: return QObject::tr("Timeline");
    case Category::Plate: return QObject::tr("Plate");
    case Category::Color: return QObject::tr("Color");
    case Category::Layout: return QObject::tr("Layout");
    case Category::Panels: return QObject::tr("Panels");
    case Category::Help: return QObject::tr("Help");
    }
    return {};
}

QList<Qt::KeyboardModifier> layerModifiers()
{
#ifdef Q_OS_MACOS
    return {Qt::ShiftModifier, Qt::MetaModifier, Qt::AltModifier, Qt::ControlModifier};
#else
    return {Qt::ShiftModifier, Qt::ControlModifier, Qt::AltModifier};
#endif
}

QString modifierGlyph(Qt::KeyboardModifier m)
{
#ifdef Q_OS_MACOS
    switch (m) {
    case Qt::ShiftModifier: return QStringLiteral("⇧");
    case Qt::MetaModifier: return QStringLiteral("⌃");
    case Qt::AltModifier: return QStringLiteral("⌥");
    case Qt::ControlModifier: return QStringLiteral("⌘");
    default: return {};
    }
#else
    switch (m) {
    case Qt::ShiftModifier: return QStringLiteral("Shift");
    case Qt::ControlModifier: return QStringLiteral("Ctrl");
    case Qt::AltModifier: return QStringLiteral("Alt");
    case Qt::MetaModifier: return QStringLiteral("Meta");
    default: return {};
    }
#endif
}

}  // namespace jefe::shortcuts

namespace sc = jefe::shortcuts;
using namespace jefe::shortcuts;  // the colours and sizes above

ShortcutKeyboard_Qt::ShortcutKeyboard_Qt(QWidget* parent) : QWidget(parent), caps_(sc::ansiLayout())
{
    setObjectName(QStringLiteral("prefs.shortcuts.keymap"));
    setAccessibleName(tr("Keyboard map"));
    setFocusPolicy(Qt::NoFocus);  // never takes keys from the list or the search box
    setProperty(jefe::qt::ui::kDensityExemptProperty, true);  // custom-painted
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(&sc::Registry::instance(), &sc::Registry::changed, this, [this] { rebuild(); });
    qApp->installEventFilter(this);
    // Releases stop arriving once focus leaves the page, so a held layer ends there.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        QWidget* scope = modifierScope();
        if (!now || !(now == scope || scope->isAncestorOf(now))) setHeld(Qt::NoModifier);
    });
    rebuild();
}

ShortcutKeyboard_Qt::~ShortcutKeyboard_Qt()
{
    if (qApp) qApp->removeEventFilter(this);
}

void ShortcutKeyboard_Qt::setLayer(Qt::KeyboardModifiers mods)
{
    mods &= sc::layerMask();
    if (mods == toggled_) return;
    const Qt::KeyboardModifiers before = layer();
    toggled_ = mods;
    if (layer() != before) {
        update();
        emit layerChanged(layer());
    }
}

Qt::KeyboardModifiers ShortcutKeyboard_Qt::layer() const { return held_ ? held_ : toggled_; }

void ShortcutKeyboard_Qt::setHeld(Qt::KeyboardModifiers held)
{
    held &= sc::layerMask();
    if (held == held_) return;
    const Qt::KeyboardModifiers before = layer();
    held_ = held;
    if (layer() != before) {
        update();
        emit layerChanged(layer());
    }
}

QWidget* ShortcutKeyboard_Qt::modifierScope() const
{
    if (scope_) return scope_;
    return parentWidget() ? parentWidget() : const_cast<ShortcutKeyboard_Qt*>(this);
}

qreal ShortcutKeyboard_Qt::unit() const
{
    const qreal m = jefe::qt::ui::kGap;
    return std::max<qreal>(1.0, (width() - 2 * m) / kTotalUnits);
}

QRectF ShortcutKeyboard_Qt::capRect(const sc::KeyCap& c) const
{
    const qreal m = jefe::qt::ui::kGap;
    const qreal u = unit();
    return QRectF(m + c.x * u, m + c.y * kRowH, c.w * u, kRowH)
        .adjusted(kCapGap, kCapGap, -kCapGap, -kCapGap);
}

QRectF ShortcutKeyboard_Qt::keyRect(Qt::Key key) const
{
    for (const sc::KeyCap& c : caps_)
        if (c.key == key) return capRect(c);
    return {};
}

int ShortcutKeyboard_Qt::capAt(const QPointF& pos) const
{
    for (int i = 0; i < caps_.size(); ++i)
        if (capRect(caps_[i]).contains(pos)) return i;
    return -1;
}

void ShortcutKeyboard_Qt::rebuild()
{
    bindings_.clear();
    sc::Registry& reg = sc::Registry::instance();
    for (const sc::Command& c : reg.commands()) {
        for (const QKeySequence& k : reg.keys(c.id)) {
            Qt::Key cap = Qt::Key_unknown;
            Qt::KeyboardModifiers mods;
            if (k.isEmpty() || !sc::capForKey(k[0], &cap, &mods)) continue;
            Binding b;
            b.mods = mods;
            b.key = k;
            b.id = QString::fromLatin1(c.id);
            b.name = c.name;
            b.caption = sc::shortCommandName(b.id, c.name);
            b.category = c.category;
            bindings_[cap].push_back(b);
        }
    }
    for (const sc::HoldKey& h : reg.holdKeys()) {
        const Qt::Key k = reg.holdKey(h.id);
        Qt::Key cap = Qt::Key_unknown;
        Qt::KeyboardModifiers mods;
        if (k == Qt::Key_unknown || !sc::capForKey(QKeyCombination(k), &cap, &mods)) continue;
        Binding b;
        b.mods = mods;
        b.key = QKeySequence(k);
        b.id = QString::fromLatin1(h.id);
        b.name = h.name;
        b.caption = sc::shortCommandName(b.id, h.name);
        b.category = sc::categoryOfId(b.id);
        b.hold = true;
        bindings_[cap].push_back(b);
    }
    for (auto it = bindings_.begin(); it != bindings_.end(); ++it)
        std::stable_sort(it->begin(), it->end(), [](const Binding& a, const Binding& b) {
            return sc::layerRank(a.mods) < sc::layerRank(b.mods);
        });
    update();
}

const ShortcutKeyboard_Qt::Binding* ShortcutKeyboard_Qt::bindingOn(Qt::Key key, Qt::KeyboardModifiers mods) const
{
    const auto it = bindings_.constFind(key);
    if (it == bindings_.constEnd()) return nullptr;
    for (const Binding& b : *it)
        if (b.mods == mods) return &b;
    return nullptr;
}

QString ShortcutKeyboard_Qt::keyCaption(Qt::Key key) const
{
    const Binding* b = bindingOn(key, layer());
    return b ? b->caption : QString();
}

bool ShortcutKeyboard_Qt::showsHoldMark(Qt::Key key) const
{
    const Binding* b = bindingOn(key, layer());
    return b && b->hold;
}

QList<ShortcutKeyboard_Qt::Binding> ShortcutKeyboard_Qt::bindingsFor(Qt::Key key) const
{
    return bindings_.value(key);
}

void ShortcutKeyboard_Qt::setSelectedKey(Qt::Key key)
{
    if (key == selected_) return;
    selected_ = key;
    update();
}

QSize ShortcutKeyboard_Qt::sizeHint() const
{
    const int m = jefe::qt::ui::kGap;
    return QSize(int(kTotalUnits * kPrefUnit) + 2 * m, int(kRowUnits * kRowH) + 2 * m);
}

QSize ShortcutKeyboard_Qt::minimumSizeHint() const
{
    const int m = jefe::qt::ui::kGap;
    return QSize(int(kTotalUnits * kMinUnit) + 2 * m, int(kRowUnits * kRowH) + 2 * m);
}

void ShortcutKeyboard_Qt::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(kMapBorder, 1));
    p.setBrush(kMapBg);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

    const Qt::KeyboardModifiers shown = layer();
    const QFontMetricsF fm(font());
    const qreal pad = 4;
    for (const sc::KeyCap& c : caps_) {
        const QRectF r = capRect(c);
        const bool modCap = isModifierCap(c.key);
        const bool activeMod = modCap && (shown & sc::modifierOfKey(c.key));
        const Binding* b = modCap ? nullptr : bindingOn(c.key, shown);
        QColor fill = modCap ? kModCapBg : kCapBg, border = kCapBorder, labelColor = modCap ? kDim : kMuted;
        if (activeMod) {
            fill = kAccentFill;
            border = kAccent;
            labelColor = kAccentText;
        } else if (b) {
            fill = sc::categoryTint(b->category);
            border = sc::mix(sc::categoryColor(b->category), kCapBg, 0.7);
            labelColor = kText;
        }
        const bool selected = !modCap && c.key == selected_;
        p.setPen(QPen(selected ? kAccent : border, selected ? 2 : 1));
        p.setBrush(fill);
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);

        const QRectF inner = r.adjusted(pad, 2, -pad, -2);
        p.setPen(labelColor);
        const QString label = fm.elidedText(c.label, Qt::ElideRight, inner.width());
        p.drawText(inner, Qt::AlignLeft | Qt::AlignTop, label);
        if (b) {
            // The command's short name on the second line, if at least a few letters fit.
            const QString cap = fm.elidedText(b->caption, Qt::ElideRight, inner.width());
            if (!cap.isEmpty() && cap != QString(QChar(0x2026)) && (cap == b->caption || cap.size() >= 3)) {
                p.setPen(kText);
                p.drawText(inner, Qt::AlignLeft | Qt::AlignBottom, cap);
            }
            if (b->hold) {
                // Hold mark: a small ring in the top-right corner.
                const qreal rad = 3;
                p.setPen(QPen(kText, 1.2));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QPointF(r.right() - pad - rad, r.top() + pad + rad), rad, rad);
            }
        }
    }
}

void ShortcutKeyboard_Qt::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
    const int i = capAt(e->position());
    if (i < 0) return;
    const Qt::Key k = caps_[i].key;
    if (isModifierCap(k)) {
        const Qt::KeyboardModifier m = sc::modifierOfKey(k);
        if (m != Qt::NoModifier) setLayer(toggled_ ^ m);
        return;
    }
    setSelectedKey(k);
    emit keyClicked(k);
}

bool ShortcutKeyboard_Qt::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        const int i = capAt(he->pos());
        QString tip;
        if (i >= 0 && !isModifierCap(caps_[i].key)) {
            if (const Binding* b = bindingOn(caps_[i].key, layer()))
                tip = b->key.toString(QKeySequence::NativeText) + QStringLiteral("  ") + b->name +
                      (b->hold ? tr(" (hold and drag)") : QString());
            else
                tip = tr("Nothing on this key with these modifiers. Click to see every layer.");
        } else if (i >= 0) {
            tip = tr("Click to show this modifier's keys.");
        }
        if (tip.isEmpty()) QToolTip::hideText();
        else QToolTip::showText(he->globalPos(), tip, this);
        return true;
    }
    return QWidget::event(e);
}

bool ShortcutKeyboard_Qt::eventFilter(QObject* o, QEvent* e)
{
    // Held modifiers while a widget of the page (the map's parent) has focus. Only watched;
    // never consumed.
    if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) {
        auto* ke = static_cast<QKeyEvent*>(e);
        const Qt::KeyboardModifier m = sc::modifierOfKey(ke->key());
        auto* w = qobject_cast<QWidget*>(o);
        QWidget* scope = modifierScope();
        if (m != Qt::NoModifier && w && (w == scope || scope->isAncestorOf(w))) {
            Qt::KeyboardModifiers held = ke->modifiers();
            if (e->type() == QEvent::KeyPress) held |= m;
            else held &= ~m;
            setHeld(held);
        }
    }
    return QWidget::eventFilter(o, e);
}

void ShortcutKeyboard_Qt::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::ActivationChange && !isActiveWindow()) setHeld(Qt::NoModifier);
    QWidget::changeEvent(e);
}
