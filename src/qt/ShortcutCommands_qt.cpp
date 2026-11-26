// SPDX-License-Identifier: GPL-2.0-or-later
// Every command and hold key the app binds, as registry data. Bridge-only commands call the
// jefe::qt:: function the old handler called; window-level commands (dialogs, docks, window
// state, file actions) are registered with run = nullptr and MainWindow_Qt binds them with
// Registry::bindRun().
#include "Shortcuts_qt.h"

#include "SequenceLoadBridge_qt.h"
#include "../UIConstants.h"

namespace jefe::shortcuts {

namespace {

using Keys = QList<QKeySequence>;

QKeySequence k(const char* portable)
{
    return QKeySequence::fromString(QString::fromLatin1(portable), QKeySequence::PortableText);
}

// The keys a QKeySequence::StandardKey maps to on this platform (at most two).
Keys standard(QKeySequence::StandardKey key)
{
    return QKeySequence::keyBindings(key).mid(0, 2);
}

// Phase 1: both presets carry today's keys.
void add(Registry& reg, const char* id, const char* name, Category category, Scope scope,
         const Keys& keys, std::function<void()> run = nullptr)
{
    reg.registerCommand({id, QString::fromUtf8(name), category, scope, keys, keys, std::move(run)});
}

void hold(Registry& reg, const char* id, const char* name, Qt::Key key)
{
    reg.registerHoldKey({id, QString::fromUtf8(name), key, key, {}});
}

}  // namespace

void registerAllCommands(Registry& reg)
{
    using namespace jefe::qt;
    constexpr Scope P = Scope::Player;
    constexpr Scope G = Scope::Global;

    // ---- Playback (Player) -----------------------------------------------------------------
    add(reg, "playback.play-pause", "Play / Pause", Category::Playback, P, {k("Space")},
        [] { pausePlayback(); });
    add(reg, "playback.step-back", "Step Back One Frame", Category::Playback, P, {k("Left"), k("X")},
        [] { stepFrame(-1); });
    add(reg, "playback.step-forward", "Step Forward One Frame", Category::Playback, P,
        {k("Right"), k("C")}, [] { stepFrame(+1); });
    add(reg, "playback.go-start", "Go to Start", Category::Playback, P, {k("Home")},
        [] { rewindPlayback(); });
    add(reg, "playback.go-end", "Go to End", Category::Playback, P, {k("End")},
        [] { fastFwdPlayback(); });
    add(reg, "playback.direction-reverse", "Play Direction: Reverse", Category::Playback, P,
        {k(","), QKeySequence(Qt::Key_Less)}, [] { setPlayDirection(-1); });
    add(reg, "playback.direction-forward", "Play Direction: Forward", Category::Playback, P,
        {k("."), QKeySequence(Qt::Key_Greater)}, [] { setPlayDirection(+1); });
    add(reg, "playback.loop-once", "Loop Mode: Once", Category::Playback, P, {k("8")},
        [] { setLoopMode(0); });
    add(reg, "playback.loop", "Loop Mode: Loop", Category::Playback, P, {k("9")},
        [] { setLoopMode(1); });
    add(reg, "playback.bounce", "Loop Mode: Bounce", Category::Playback, P, {k("0")},
        [] { setLoopMode(2); });

    // ---- In / Out (Player) -----------------------------------------------------------------
    add(reg, "inout.set-in", "Set In Point at Playhead", Category::InOut, P, {k("I")},
        [] { setInPoint(getCurrentFrame()); });
    add(reg, "inout.set-out", "Set Out Point at Playhead", Category::InOut, P, {k("O")},
        [] { setOutPoint(getCurrentFrame()); });
    add(reg, "inout.reset-in", "Reset In Point to Start", Category::InOut, P, {k("Shift+I")},
        [] { setInPoint(getFromFrame()); });
    add(reg, "inout.reset-out", "Reset Out Point to End", Category::InOut, P, {k("Shift+O")},
        [] { setOutPoint(getToFrame()); });
    add(reg, "inout.set-in-load", "Set In Point and Load from Here", Category::InOut, P,
        {k("Alt+I")}, [] { setInPointAndLoad(); });

    // ---- Track cycling (Player) ------------------------------------------------------------
    add(reg, "plate.track-prev", "Previous Track on Active Plate", Category::Plate, P, {k("Up")},
        [] { cycleTrackOnActivePlate(-1); });
    add(reg, "plate.track-next", "Next Track on Active Plate", Category::Plate, P, {k("Down")},
        [] { cycleTrackOnActivePlate(+1); });

    // ---- Layout (Global) -------------------------------------------------------------------
    add(reg, "layout.single", "Layout: Single", Category::Layout, G, {k("Ctrl+1")},
        [] { setFramingMode(FRAMINGSINGLE_ID); });
    add(reg, "layout.side", "Layout: Side by Side", Category::Layout, G, {k("Ctrl+2")},
        [] { setFramingMode(FRAMINGDOUBLE_ID); });
    add(reg, "layout.top-bottom", "Layout: Top / Bottom", Category::Layout, G, {k("Ctrl+3")},
        [] { setFramingMode(FRAMINGDOUBLEVERT_ID); });
    add(reg, "layout.quad", "Layout: 2 x 2", Category::Layout, G, {k("Ctrl+4")},
        [] { setFramingMode(FRAMINGQUAD_ID); });

    // ---- Plate transforms (Global) ---------------------------------------------------------
    add(reg, "plate.fit", "Fit to Screen", Category::Plate, G, {k("F")}, [] { fitActivePlate(); });
    add(reg, "plate.fit-all", "Fit All Plates", Category::Plate, G, {k("Shift+F")},
        [] { fitAllPlates(); });
    add(reg, "plate.flip", "Flip Vertical", Category::Plate, G, {k("V")}, [] { toggleFlipActive(); });
    add(reg, "plate.flip-all", "Flip All Plates", Category::Plate, G, {k("Shift+V")},
        [] { toggleFlipAll(); });
    add(reg, "plate.flop", "Flop Horizontal", Category::Plate, G, {k("M")}, [] { toggleFlopActive(); });
    add(reg, "plate.flop-all", "Flop All Plates", Category::Plate, G, {k("Shift+M")},
        [] { toggleFlopAll(); });
    add(reg, "plate.text-mode", "Text Overlay Mode", Category::Plate, G, {k("T")},
        [] { toggleTextModeActive(); });
    add(reg, "plate.text-mode-all", "Text Overlay Mode (All Plates)", Category::Plate, G,
        {k("Alt+T")}, [] { toggleTextModeAll(); });
    add(reg, "plate.reset", "Reset Plate", Category::Plate, G, {k("Ctrl+R")},
        [] { resetActivePlate(); });
    add(reg, "plate.reset-all", "Reset All Plates", Category::Plate, G, {k("Ctrl+Alt+R")},
        [] { resetAllPlates(); });
    add(reg, "view.toggle-histogram", "Show Histogram", Category::Plate, G, {k("Ctrl+H")},
        [] { toggleHistogramActiveQuad(); });
    add(reg, "view.toggle-histogram-all", "Show Histogram (All Plates)", Category::Plate, G,
        {k("Ctrl+Alt+H")}, [] { toggleHistogramAll(); });
    add(reg, "view.toggle-note-overlay", "Show Notes", Category::Plate, G, {k("N")});

    // ---- Colour (Global) -------------------------------------------------------------------
    add(reg, "color.reset", "Reset Color Correction", Category::Color, G, {k("Shift+R")},
        [] { resetActiveColorCorrection(); });
    add(reg, "color.reset-all", "Reset All Color Corrections", Category::Color, G,
        {k("Shift+Alt+R")}, [] { resetAllColorCorrections(); });

    // ---- File (Global, window-level) -------------------------------------------------------
    add(reg, "file.quick-load", "Quick Load…", Category::File, G, {k("Ctrl+O")});
    add(reg, "file.load-manager", "Load Sequence Manager…", Category::File, G, {k("Ctrl+L")});
    add(reg, "file.save-session", "Save Session", Category::File, G, standard(QKeySequence::Save));
    add(reg, "file.open-session", "Open Session…", Category::File, G, {k("Ctrl+Shift+O")});
    add(reg, "file.preferences", "Preferences…", Category::File, G, {k("Ctrl+P"), k("Ctrl+,")});
    add(reg, "file.quit", "Quit", Category::File, G, standard(QKeySequence::Quit));

    // ---- Panels and window (Global, window-level) ------------------------------------------
    add(reg, "view.toggle-fullscreen", "Fullscreen", Category::Panels, G,
        {k("F11"), QKeySequence(Qt::CTRL | Qt::META | Qt::Key_F)});
    add(reg, "panels.toggle-plate-manager", "Plate Manager", Category::Panels, G, {k("F2")});
    add(reg, "panels.toggle-fx", "FX", Category::Panels, G, {k("F3")});
    add(reg, "panels.toggle-luts", "LUTs", Category::Panels, G, {k("F4")});
    add(reg, "panels.open-remote", "Remote Session…", Category::Panels, G, {k("F5")});
    add(reg, "panels.open-render", "Render…", Category::Panels, G, {k("F6")});
    add(reg, "panels.toggle-notes", "Notes", Category::Panels, G, {k("F7")});
    add(reg, "panels.toggle-timeline", "Timeline", Category::Panels, G, {k("F8")});
    add(reg, "panels.toggle-playlist", "Playlist", Category::Panels, G, {k("F9")});
    add(reg, "panels.hide-controls", "Hide Controls", Category::Panels, G, {k("Ctrl+Alt+F")});
#ifdef Q_OS_MACOS
    add(reg, "panels.minimize", "Minimize", Category::Panels, G, {k("Ctrl+M")});
#endif

    // ---- Help (Global) ---------------------------------------------------------------------
    add(reg, "help.manual", "User Manual", Category::Help, G, {k("F1")});
    add(reg, "help.toggle-overlay", "Toggle On-Screen Help", Category::Help, G, {k("H")},
        [] { toggleOnScreenHelp(); });

    // ---- Hold and drag (viewport) ----------------------------------------------------------
    hold(reg, "color.drag-gamma", "Drag Gamma", Qt::Key_W);
    hold(reg, "color.drag-exposure", "Drag Exposure", Qt::Key_E);
    hold(reg, "color.drag-brightness", "Drag Brightness", Qt::Key_Q);
    hold(reg, "color.drag-contrast", "Drag Contrast", Qt::Key_D);
    hold(reg, "color.drag-saturation", "Drag Saturation", Qt::Key_S);
}

}  // namespace jefe::shortcuts
