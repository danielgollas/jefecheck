// SPDX-License-Identifier: GPL-2.0-or-later
// On-screen help overlay (H) text, generated from the shortcut registry so it always shows
// the keys in force. Layout and section headings follow the fallback string in
// gfcPlateManager's constructor; mouse lines (not registry commands) are kept verbatim.
#include "Shortcuts_qt.h"

#include <QStringList>

namespace jefe::shortcuts {

namespace {

struct Entry { const char* id; bool hold; };

struct Section {
    const char* heading;
    QList<Entry> entries;
    QStringList trailer;     // fixed lines (mouse / chat) after the registry lines
};

QString keyText(const Registry& reg, const Entry& e)
{
    if (e.hold) {
        const Qt::Key k = reg.holdKey(e.id);
        return k == Qt::Key_unknown ? QString() : QKeySequence(k).toString(QKeySequence::NativeText);
    }
    return reg.label(e.id);
}

QString nameOf(const Registry& reg, const Entry& e)
{
    if (e.hold) {
        for (const HoldKey& h : reg.holdKeys())
            if (QString::fromUtf8(h.id) == QString::fromUtf8(e.id)) return h.name;
        return {};
    }
    for (const Command& c : reg.commands())
        if (QString::fromUtf8(c.id) == QString::fromUtf8(e.id)) return c.name;
    return {};
}

}  // namespace

QString buildHelpText(const Registry& reg)
{
    const QList<Section> sections = {
        {"VIEWPORT LAYOUTS",
         {{"layout.single", false}, {"layout.side", false}, {"layout.top-bottom", false},
          {"layout.quad", false}}, {}},
        {"WINDOW / GUI",
         {{"view.toggle-fullscreen", false}, {"panels.hide-controls", false},
          {"help.toggle-overlay", false}, {"view.toggle-histogram", false},
          {"view.toggle-histogram-all", false}, {"view.toggle-note-overlay", false},
          {"file.preferences", false}, {"help.manual", false}}, {}},
        {"WINDOW MENU  (show / hide)",
         {{"panels.toggle-plate-manager", false}, {"panels.toggle-fx", false},
          {"panels.toggle-luts", false}, {"panels.open-remote", false},
          {"panels.open-render", false}, {"panels.toggle-notes", false},
          {"panels.toggle-timeline", false}, {"panels.toggle-playlist", false}}, {}},
        {"PLAYBACK",
         {{"playback.play-pause", false}, {"playback.direction-reverse", false},
          {"playback.direction-forward", false}, {"playback.step-back", false},
          {"playback.step-forward", false}, {"playback.go-start", false},
          {"playback.go-end", false}, {"playback.loop-once", false}, {"playback.loop", false},
          {"playback.bounce", false}, {"plate.track-prev", false}, {"plate.track-next", false},
          {"inout.set-in", false}, {"inout.set-out", false}, {"inout.reset-in", false},
          {"inout.reset-out", false}, {"inout.set-in-load", false}},
         {QStringLiteral("Load from this point on - Alt+LMB (on a track or the timeline; loads all tracks)")}},
        {"TRANSFORMS  (active plate)",
         {{"plate.fit", false}, {"plate.fit-all", false}, {"plate.flip", false},
          {"plate.flip-all", false}, {"plate.flop", false}, {"plate.flop-all", false},
          {"plate.text-mode", false}, {"plate.text-mode-all", false}, {"plate.reset", false},
          {"plate.reset-all", false}, {"color.reset", false}, {"color.reset-all", false}},
         {QStringLiteral("Pan - LMB drag  (+Alt = all plates)"), QStringLiteral("Zoom - Mouse Wheel")}},
        {"COLOR CORRECTION  (hold key + LMB drag)",
         {{"color.drag-gamma", true}, {"color.drag-exposure", true}, {"color.drag-brightness", true},
          {"color.drag-contrast", true}, {"color.drag-saturation", true}}, {}},
        {"REMOTE SESSIONS", {},
         {QStringLiteral("Remote Pointer - Shift + LMB drag"),
          QStringLiteral("Enter Chat - Return    Cancel - Escape")}},
    };

    QStringList out;
    out << QString() << QString() << QString()
        << QStringLiteral("JefeCheck - Quick Help")
        << QStringLiteral("(toggle on/off with '%1')").arg(reg.label("help.toggle-overlay").toLower())
        << QStringLiteral("-------------") << QString();
    for (const Section& s : sections) {
        out << QString::fromLatin1(s.heading);
        for (const Entry& e : s.entries) {
            const QString k = keyText(reg, e);
            if (k.isEmpty()) continue;                     // unbound: no line
            out << QStringLiteral("%1 - %2").arg(nameOf(reg, e), k);
        }
        out << s.trailer << QString();
    }
    while (!out.isEmpty() && out.last().isEmpty()) out.removeLast();
    return out.join(QLatin1Char('\n'));
}

QString tooltipWithKey(const QString& text, const char* id)
{
    const QString k = Registry::instance().label(id);
    return k.isEmpty() ? text : QStringLiteral("%1 (%2)").arg(text, k);
}

}  // namespace jefe::shortcuts
