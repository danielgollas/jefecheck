// SPDX-License-Identifier: GPL-2.0-or-later
// On-screen help overlay (H) text, generated from the shortcut registry so it always shows
// the keys in force. Layout, wording and section headings follow the fallback string in
// gfcPlateManager's constructor; mouse lines (not registry commands) are kept verbatim.
#include "Shortcuts_qt.h"

#include <QStringList>

namespace jefe::shortcuts {

namespace {

// One "Name - key" item. `ids` are joined as "a / b / c"; `extras` add a parenthesised
// suffix such as "(+Shift = all plates)" built from the extra command's actual key.
struct Extra { const char* id; const char* phrase; };
// A numbered family (favourite slots 1-5, tracks A-D): drawn as one run, "Shift+1-5", when its
// keys are consecutive with the same modifiers; otherwise its own line lists them key by key.
struct Family { const char* name; QList<const char*> ids; };
struct Item {
    const char* name;
    QList<const char*> ids;
    QList<Extra> extras;
    QList<Family> families = {};      // non-empty: a run item ("Name a / b - run / run")
    const char* holdWith = nullptr;   // a hold key with `with` keys: "Name - L held + Up / Down"
};
// Items share one line; `fixed` is a constant line, or a constant tail after the items.
struct Row { QList<Item> items; const char* fixed = nullptr; };
struct Section { const char* heading; QList<Row> rows; };

constexpr const char* kAll = "all plates";

bool isHold(const Registry& reg, const char* id)
{
    for (const HoldKey& h : reg.holdKeys())
        if (QString::fromUtf8(h.id) == QString::fromUtf8(id)) return true;
    return false;
}

// Every current key of a command (or the hold key), in registry order. PortableText
// ("Ctrl+Alt+F", "Left"): the overlay's glyph atlas has no arrow or macOS modifier symbols,
// so native text ("⌥⌘F", "←") would draw as gaps.
QList<QKeySequence> keysOf(const Registry& reg, const char* id)
{
    if (isHold(reg, id)) {
        const Qt::Key k = reg.holdKey(id);
        return k == Qt::Key_unknown ? QList<QKeySequence>{} : QList<QKeySequence>{QKeySequence(k)};
    }
    return reg.keys(id);
}

QString portable(const QKeySequence& k) { return k.toString(QKeySequence::PortableText); }

QString labelOf(const Registry& reg, const char* id)
{
    const QList<QKeySequence> ks = keysOf(reg, id);
    return ks.isEmpty() ? QString() : portable(ks.first());
}

// "+Shift" / "+Alt+Shift" when `extra` is `base` plus modifiers; empty otherwise.
QString modifierDelta(const QKeySequence& base, const QKeySequence& extra)
{
    if (base.isEmpty() || extra.isEmpty()) return {};
    const QKeyCombination b = base[0], e = extra[0];
    if (b.key() != e.key()) return {};
    const Qt::KeyboardModifiers added = e.keyboardModifiers() & ~b.keyboardModifiers();
    if (!added || (b.keyboardModifiers() & ~e.keyboardModifiers())) return {};
    QString out;
    if (added & Qt::ControlModifier) out += QStringLiteral("+Ctrl");
    if (added & Qt::MetaModifier) out += QStringLiteral("+Meta");
    if (added & Qt::AltModifier) out += QStringLiteral("+Alt");
    if (added & Qt::ShiftModifier) out += QStringLiteral("+Shift");
    return out;
}

// Returns the line text, or empty when one of the item's commands is unbound (a one-command
// item with extras keeps its line for the extras still bound: "Set IN Point - none  (...)").
// One command: all its keys, "F11 or Meta+Ctrl+F". Several: their first keys joined with
// " / ", then any second keys in brackets, "Left / Right (or X / C)".
QString itemText(const Registry& reg, const Item& it, QStringList* idsOut, QStringList* extraOut)
{
    QList<QStringList> perId;
    QStringList ids;
    bool baseUnbound = false;
    for (const char* id : it.ids) {
        QStringList labels;
        for (const QKeySequence& k : keysOf(reg, id)) labels << portable(k);
        if (labels.isEmpty()) {
            if (it.ids.size() != 1 || it.extras.isEmpty()) return {};
            baseUnbound = true;
            break;
        }
        perId << labels;
        ids << QString::fromUtf8(id);
    }
    QString keys;
    if (baseUnbound) {
        keys = QStringLiteral("none");
    } else if (perId.size() == 1) {
        keys = perId.first().join(QStringLiteral(" or "));
    } else {
        QStringList first, more;
        for (const QStringList& labels : perId) {
            first << labels.first();
            more << labels.mid(1);
        }
        keys = first.join(QStringLiteral(" / "));
        if (!more.isEmpty()) keys += QStringLiteral(" (or %1)").arg(more.join(QStringLiteral(" / ")));
    }
    QString text = QStringLiteral("%1 - %2").arg(QString::fromUtf8(it.name), keys);
    QStringList notes;
    const QKeySequence base = reg.key(it.ids.first());
    for (const Extra& x : it.extras) {
        QStringList forms;
        for (const QKeySequence& k : keysOf(reg, x.id)) {
            const QString delta = modifierDelta(base, k);
            forms << (delta.isEmpty() ? portable(k) : delta);
        }
        if (forms.isEmpty()) continue;                               // unbound: no note
        if (extraOut) *extraOut << QString::fromUtf8(x.id);
        notes << QStringLiteral("%1 = %2").arg(forms.join(QStringLiteral(" or ")), QString::fromUtf8(x.phrase));
    }
    if (baseUnbound && notes.isEmpty()) return {};   // nothing on the line is bound
    if (!notes.isEmpty()) text += QStringLiteral("  (%1)").arg(notes.join(QStringLiteral(", ")));
    if (idsOut) *idsOut << ids;
    return text;
}

// "Shift+1-5" for slots whose j-th keys are consecutive with equal modifiers; empty otherwise.
QString runText(const Registry& reg, const QList<const char*>& ids, int j)
{
    QList<QKeyCombination> combos;
    for (const char* id : ids) {
        const QList<QKeySequence> ks = keysOf(reg, id);
        if (ks.size() <= j) return {};
        combos << ks[j][0];
    }
    if (combos.size() < 2) return {};
    for (int i = 1; i < combos.size(); ++i)
        if (combos[i].keyboardModifiers() != combos[0].keyboardModifiers() ||
            int(combos[i].key()) != int(combos[0].key()) + i)
            return {};
    return portable(QKeySequence(combos.first())) + QLatin1Char('-') + portable(QKeySequence(combos.last().key()));
}

// The family's keys as runs ("Shift+1-5", or "Meta+Shift+1-5 (or Ctrl+Shift+1-5)" for two keys
// per slot); empty when they are not runs.
QString familyRun(const Registry& reg, const Family& f)
{
    if (f.ids.isEmpty()) return {};
    const int n = int(keysOf(reg, f.ids.first()).size());
    if (n == 0) return {};
    for (const char* id : f.ids)
        if (keysOf(reg, id).size() != n) return {};
    QStringList runs;
    for (int j = 0; j < n; ++j) {
        const QString r = runText(reg, f.ids, j);
        if (r.isEmpty()) return {};
        runs << r;
    }
    return runs.size() == 1 ? runs.first() : QStringLiteral("%1 (or %2)").arg(runs.first(), runs.mid(1).join(QStringLiteral(" / ")));
}

QStringList idList(const QList<const char*>& ids)
{
    QStringList out;
    for (const char* id : ids) out << QString::fromUtf8(id);
    return out;
}

// A run item: one "Name a / b - run / run" part when every family is a run (`part`, with the
// families in `ranges`); otherwise a line per family (`extraLines`), runs where they still are.
void rangeItem(const Registry& reg, const Item& it, QString* part, QList<QStringList>* ranges,
               QList<HelpLine>* extraLines)
{
    QStringList names, runs;
    bool allRuns = true;
    for (const Family& f : it.families) {
        const QString r = familyRun(reg, f);
        if (r.isEmpty()) allRuns = false;
        if (f.name && *f.name) names << QString::fromUtf8(f.name);
        runs << r;
    }
    const QString title = QString::fromUtf8(it.name);
    if (allRuns) {
        *part = QStringLiteral("%1 - %2").arg(names.isEmpty() ? title : title + QLatin1Char(' ') + names.join(QStringLiteral(" / ")),
                                                runs.join(QStringLiteral(" / ")));
        for (const Family& f : it.families) *ranges << idList(f.ids);
        return;
    }
    for (int i = 0; i < it.families.size(); ++i) {
        const Family& f = it.families[i];
        const QString name = (f.name && *f.name) ? title + QLatin1Char(' ') + QString::fromUtf8(f.name) : title;
        if (!runs[i].isEmpty()) {
            *extraLines << HelpLine{QStringLiteral("%1 - %2").arg(name, runs[i]), {}, {}, {idList(f.ids)}};
            continue;
        }
        QStringList slotKeys, ids;
        for (const char* id : f.ids) {
            QStringList labels;
            for (const QKeySequence& k : keysOf(reg, id)) labels << portable(k);
            if (labels.isEmpty()) continue;
            slotKeys << labels.join(QStringLiteral(" or "));
            ids << QString::fromUtf8(id);
        }
        if (!slotKeys.isEmpty())
            *extraLines << HelpLine{QStringLiteral("%1 - %2").arg(name, slotKeys.join(QStringLiteral(" / "))), ids, {}, {}};
    }
}

// "Name - L held + Up / Down"; empty when the hold key is unbound.
QString holdWithText(const Registry& reg, const Item& it, QStringList* idsOut)
{
    const Qt::Key hk = reg.holdKey(it.holdWith);
    if (hk == Qt::Key_unknown) return {};
    QStringList with;
    for (const HoldKey& h : reg.holdKeys())
        if (QString::fromUtf8(h.id) == QString::fromUtf8(it.holdWith))
            for (const auto& w : h.with) with << portable(QKeySequence(w.first));
    if (with.isEmpty()) return {};
    if (idsOut) *idsOut << QString::fromUtf8(it.holdWith);
    return QStringLiteral("%1 - %2 held + %3").arg(QString::fromUtf8(it.name), portable(QKeySequence(hk)),
                                                   with.join(QStringLiteral(" / ")));
}

QList<Section> sections()
{
    return {
        {"VIEWPORT LAYOUTS",
         {{{{"Single", {"layout.single"}, {}}}}, {{{"Side by Side", {"layout.side"}, {}}}},
          {{{"Top and Bottom", {"layout.top-bottom"}, {}}}}, {{{"2x2", {"layout.quad"}, {}}}}}},
        {"WINDOW / GUI",
         {{{{"Toggle Fullscreen", {"view.toggle-fullscreen"}, {}}}},
          {{{"Hide Controls (bars, menu, docks)", {"panels.hide-controls"}, {}}}},
          {{{"Toggle this Help", {"help.toggle-overlay"}, {}}}},
          {{{"Show Histogram", {"view.toggle-histogram"}, {{"view.toggle-histogram-all", kAll}}}}},
          {{{"Preferences", {"file.preferences"}, {}}, {"Notes Overlay", {"view.toggle-note-overlay"}, {}},
            {"User Manual", {"help.manual"}, {}}}}}},
        {"WINDOW MENU  (show / hide)",
         {{{{"Plate Manager", {"panels.toggle-plate-manager"}, {}}, {"FX", {"panels.toggle-fx"}, {}},
            {"LUTs", {"panels.toggle-luts"}, {}}}},
          {{{"Remote Session", {"panels.open-remote"}, {}}, {"Render", {"panels.open-render"}, {}},
            {"Notes", {"panels.toggle-notes"}, {}}}},
          {{{"Timeline", {"panels.toggle-timeline"}, {}}, {"Playlist", {"panels.toggle-playlist"}, {}}}}}},
        {"PLAYBACK",
         {{{{"Play / Pause", {"playback.play-pause"}, {}},
            {"Reverse / Pause / Forward", {"playback.jkl-reverse", "playback.jkl-pause", "playback.jkl-forward"}, {}},
            {"Play Reverse / Forward", {"playback.play-reverse", "playback.play-forward"}, {}}}},
          {{{"Play Direction (reverse / forward)",
             {"playback.direction-reverse", "playback.direction-forward"}, {}}}},
          {{{"Step one frame (back / forward)", {"playback.step-back", "playback.step-forward"}, {}}}},
          {{{"First / Last Frame", {"playback.go-start", "playback.go-end"}, {}}}},
          {{{"Loop Mode (once / loop / bounce)",
             {"playback.loop-once", "playback.loop", "playback.bounce"}, {}}}},
          {{{"Cycle Track on Active Plate", {"plate.track-prev", "plate.track-next"}, {}},
            {"Show Track A-D", {}, {},
             {{"", {"plate.show-track-1", "plate.show-track-2", "plate.show-track-3", "plate.show-track-4"}}}}}},
          {{{"Set IN Point", {"inout.set-in"},
             {{"inout.go-in", "go to"}, {"inout.reset-in", "timeline start"}, {"inout.set-in-load", "load from here"}}}}},
          {{{"Set OUT Point", {"inout.set-out"}, {{"inout.go-out", "go to"}, {"inout.reset-out", "timeline end"}}},
            {"Clear In and Out", {"inout.clear-both"}, {}}}},
          {{}, "Load from this point on - Alt+LMB (on a track or the timeline; loads all tracks)"}}},
        {"TRANSFORMS  (active plate)",
         {{{{"Fit to Screen", {"plate.fit"}, {{"plate.fit-all", kAll}}}}},
          {{{"Flip Vertical", {"plate.flip"}, {{"plate.flip-all", kAll}}}}},
          {{{"Flop Horizontal", {"plate.flop"}, {{"plate.flop-all", kAll}}}}},
          {{{"Text Overlay Mode", {"plate.text-mode"}, {{"plate.text-mode-all", kAll}}}}},
          {{{"Reset Plate", {"plate.reset"}, {{"plate.reset-all", kAll}}}}},
          {{{"Reset Color Correction", {"color.reset"}, {{"color.reset-all", kAll}}}}},
          {{}, "Pan - LMB drag  (+Alt = all plates)"},
          {{}, "Zoom - Mouse Wheel"}}},
        {"COLOR CORRECTION  (active plate)",
         {{{{"Gamma / Exposure (hold + LMB drag)", {"color.drag-gamma", "color.drag-exposure"}, {}}}},
          {{{"Brightness / Contrast / Saturation (hold + LMB drag)",
             {"color.drag-brightness", "color.drag-contrast", "color.drag-saturation"}, {}}}},
          {{{"Channels (red / green / blue / alpha)",
             {"color.channel-r", "color.channel-g", "color.channel-b", "color.channel-a"}, {}}}},
          {{{"Previous / Next LUT", {"plate.lut-prev", "plate.lut-next"}, {}},
            {"Previous / Next LUT", {}, {}, {}, "plate.lut-hold"}}},
          {{{"CC favourite", {}, {},
             {{"apply", {"color.cc-fav-apply-1", "color.cc-fav-apply-2", "color.cc-fav-apply-3",
                         "color.cc-fav-apply-4", "color.cc-fav-apply-5"}},
              {"save", {"color.cc-fav-save-1", "color.cc-fav-save-2", "color.cc-fav-save-3",
                        "color.cc-fav-save-4", "color.cc-fav-save-5"}}}}}},
          {{{"FX favourite", {}, {},
             {{"load", {"fx.fav-load-1", "fx.fav-load-2", "fx.fav-load-3", "fx.fav-load-4", "fx.fav-load-5"}},
              {"append", {"fx.fav-append-1", "fx.fav-append-2", "fx.fav-append-3", "fx.fav-append-4",
                          "fx.fav-append-5"}},
              {"save", {"fx.fav-save-1", "fx.fav-save-2", "fx.fav-save-3", "fx.fav-save-4", "fx.fav-save-5"}}}}}}}},
        {"REMOTE SESSIONS",
         {{{}, "Remote Pointer - Shift + LMB drag"},
          {{{"Enter Chat", {"remote.chat-type"}, {}}, {"Show / Hide Chat", {"remote.chat-toggle"}, {}}},
           "Cancel - Escape"}}},
    };
}

}  // namespace

QList<HelpLine> buildHelpLines(const Registry& reg)
{
    QList<HelpLine> out;
    const QString toggle = labelOf(reg, "help.toggle-overlay");
    out << HelpLine{} << HelpLine{} << HelpLine{} << HelpLine{QStringLiteral("JefeCheck - Quick Help"), {}, {}, {}};
    if (!toggle.isEmpty())
        out << HelpLine{QStringLiteral("(toggle on/off with '%1')").arg(toggle.toLower()), {}, {}, {}};
    out << HelpLine{QStringLiteral("-------------"), {}, {}, {}} << HelpLine{};
    for (const Section& s : sections()) {
        out << HelpLine{QString::fromLatin1(s.heading), {}, {}, {}};
        for (const Row& r : s.rows) {
            if (r.items.isEmpty()) {
                if (r.fixed) out << HelpLine{QString::fromUtf8(r.fixed), {}, {}, {}};
                continue;
            }
            QStringList parts, ids, extras;
            QList<QStringList> ranges;
            QList<HelpLine> after;  // families that stopped being runs, one line each
            for (const Item& it : r.items) {
                QString t;
                if (!it.families.isEmpty()) rangeItem(reg, it, &t, &ranges, &after);
                else if (it.holdWith) t = holdWithText(reg, it, &ids);
                else t = itemText(reg, it, &ids, &extras);
                if (!t.isEmpty()) parts << t;
            }
            if (r.fixed) parts << QString::fromUtf8(r.fixed);
            if (!parts.isEmpty()) out << HelpLine{parts.join(QStringLiteral("   ")), ids, extras, ranges};
            out << after;
        }
        out << HelpLine{};
    }
    while (!out.isEmpty() && out.last().text.isEmpty()) out.removeLast();
    return out;
}

QStringList helpPlacedIds()
{
    QStringList ids;
    for (const Section& s : sections())
        for (const Row& r : s.rows)
            for (const Item& it : r.items) {
                for (const char* id : it.ids) ids << QString::fromUtf8(id);
                for (const Extra& x : it.extras) ids << QString::fromUtf8(x.id);
                for (const Family& f : it.families) ids << idList(f.ids);
                if (it.holdWith) ids << QString::fromUtf8(it.holdWith);
            }
    return ids;
}

QString buildHelpText(const Registry& reg)
{
    QStringList lines;
    for (const HelpLine& l : buildHelpLines(reg)) lines << l.text;
    return lines.join(QLatin1Char('\n'));
}

QString tooltipWithKey(const QString& text, const char* id)
{
    const QString k = Registry::instance().label(id);
    return k.isEmpty() ? text : QStringLiteral("%1 (%2)").arg(text, k);
}

}  // namespace jefe::shortcuts
