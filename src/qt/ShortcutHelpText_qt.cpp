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
struct Item {
    const char* name;
    QList<const char*> ids;
    QList<Extra> extras;
};
struct Row { QList<Item> items; const char* fixed = nullptr; };  // items share one line
struct Section { const char* heading; QList<Row> rows; };

constexpr const char* kAll = "all plates";

bool isHold(const Registry& reg, const char* id)
{
    for (const HoldKey& h : reg.holdKeys())
        if (QString::fromUtf8(h.id) == QString::fromUtf8(id)) return true;
    return false;
}

QString labelOf(const Registry& reg, const char* id)
{
    if (isHold(reg, id)) {
        const Qt::Key k = reg.holdKey(id);
        return k == Qt::Key_unknown ? QString() : QKeySequence(k).toString(QKeySequence::PortableText);
    }
    // PortableText ("Ctrl+Alt+F", "Left"): the overlay's glyph atlas has no arrow or macOS
    // modifier symbols, so native text ("⌥⌘F", "←") would draw as gaps.
    return reg.key(id).toString(QKeySequence::PortableText);
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

// Returns the line text, or empty when the item's first command is unbound.
QString itemText(const Registry& reg, const Item& it, QStringList* idsOut, QStringList* extraOut)
{
    QStringList labels;
    for (const char* id : it.ids) {
        const QString l = labelOf(reg, id);
        if (l.isEmpty()) return {};
        labels << l;
        if (idsOut) *idsOut << QString::fromUtf8(id);
    }
    QString text = QStringLiteral("%1 - %2").arg(QString::fromUtf8(it.name), labels.join(QStringLiteral(" / ")));
    QStringList notes;
    for (const Extra& x : it.extras) {
        const QString l = labelOf(reg, x.id);
        if (l.isEmpty()) continue;                                   // unbound: no note
        if (extraOut) *extraOut << QString::fromUtf8(x.id);
        const QString delta = modifierDelta(reg.key(it.ids.first()), reg.key(x.id));
        notes << QStringLiteral("%1 = %2").arg(delta.isEmpty() ? l : delta, QString::fromUtf8(x.phrase));
    }
    if (!notes.isEmpty()) text += QStringLiteral("  (%1)").arg(notes.join(QStringLiteral(", ")));
    return text;
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
         {{{{"Play / Pause", {"playback.play-pause"}, {}}}},
          {{{"Play Direction (reverse / forward)",
             {"playback.direction-reverse", "playback.direction-forward"}, {}}}},
          {{{"Step one frame (back / forward)", {"playback.step-back", "playback.step-forward"}, {}}}},
          {{{"First / Last Frame", {"playback.go-start", "playback.go-end"}, {}}}},
          {{{"Loop Mode (once / loop / bounce)",
             {"playback.loop-once", "playback.loop", "playback.bounce"}, {}}}},
          {{{"Cycle Track on Active Plate", {"plate.track-prev", "plate.track-next"}, {}}}},
          {{{"Set IN Point", {"inout.set-in"},
             {{"inout.reset-in", "timeline start"}, {"inout.set-in-load", "load from here"}}}}},
          {{{"Set OUT Point", {"inout.set-out"}, {{"inout.reset-out", "timeline end"}}}}},
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
        {"COLOR CORRECTION  (hold key + LMB drag)",
         {{{{"Gamma / Exposure", {"color.drag-gamma", "color.drag-exposure"}, {}}}},
          {{{"Brightness / Contrast / Saturation",
             {"color.drag-brightness", "color.drag-contrast", "color.drag-saturation"}, {}}}}}},
        {"REMOTE SESSIONS",
         {{{}, "Remote Pointer - Shift + LMB drag"},
          {{}, "Enter Chat - Return    Cancel - Escape"}}},
    };
}

}  // namespace

QList<HelpLine> buildHelpLines(const Registry& reg)
{
    QList<HelpLine> out;
    const QString toggle = labelOf(reg, "help.toggle-overlay");
    out << HelpLine{} << HelpLine{} << HelpLine{} << HelpLine{QStringLiteral("JefeCheck - Quick Help"), {}, {}};
    if (!toggle.isEmpty())
        out << HelpLine{QStringLiteral("(toggle on/off with '%1')").arg(toggle.toLower()), {}, {}};
    out << HelpLine{QStringLiteral("-------------"), {}, {}} << HelpLine{};
    for (const Section& s : sections()) {
        out << HelpLine{QString::fromLatin1(s.heading), {}, {}};
        for (const Row& r : s.rows) {
            if (r.fixed) { out << HelpLine{QString::fromUtf8(r.fixed), {}, {}}; continue; }
            QStringList parts, ids, extras;
            for (const Item& it : r.items) {
                const QString t = itemText(reg, it, &ids, &extras);
                if (!t.isEmpty()) parts << t;
            }
            if (!parts.isEmpty()) out << HelpLine{parts.join(QStringLiteral("   ")), ids, extras};
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
