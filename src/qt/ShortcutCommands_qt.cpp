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

QKeySequence k(const QByteArray& portable) { return k(portable.constData()); }

// The key a QKeySequence::StandardKey maps to on this platform: only the first binding, as
// QKeySequence(StandardKey) gives the menus (later bindings are media keys on macOS).
Keys standard(QKeySequence::StandardKey key)
{
    return QKeySequence::keyBindings(key).mid(0, 1);
}

// A JefeCheck 1.7 key written "Ctrl+...": FLTK's FL_CTRL is the physical Control key, which is
// Qt::META on macOS (Qt::CTRL is Cmd there) and Qt::CTRL elsewhere.
QKeySequence ctl(const char* portable)
{
    QKeySequence key = k(portable);
#ifdef Q_OS_MACOS
    const QKeyCombination c = key[0];
    if (c.keyboardModifiers() & Qt::ControlModifier)
        key = QKeySequence(QKeyCombination((c.keyboardModifiers() & ~Qt::ControlModifier) | Qt::MetaModifier, c.key()));
#endif
    return key;
}

QKeySequence ctl(const QByteArray& portable) { return ctl(portable.constData()); }

// Up to two distinct keys, in order (a 1.7 physical-Control key and its 2.0 Ctrl key are the
// same key everywhere but macOS).
Keys keys(std::initializer_list<QKeySequence> in)
{
    Keys out;
    for (const QKeySequence& key : in)
        if (!key.isEmpty() && !out.contains(key) && out.size() < 2) out << key;
    return out;
}

void add(Registry& reg, const char* id, const char* name, Category category, Scope scope,
         const Keys& editing, const Keys& classic, std::function<void()> run = nullptr)
{
    reg.registerCommand({id, QString::fromUtf8(name), category, scope, editing, classic, std::move(run)});
}

// Same keys in both presets.
void both(Registry& reg, const char* id, const char* name, Category category, Scope scope,
          const Keys& keys, std::function<void()> run = nullptr)
{
    add(reg, id, name, category, scope, keys, keys, std::move(run));
}

void hold(Registry& reg, const char* id, const char* name, Qt::Key key)
{
    reg.registerHoldKey({id, QString::fromUtf8(name), key, key, {}});
}

}  // namespace

Transport bridgeTransport()
{
    return {[] { return jefe::qt::isPlaying(); }, [](int d) { jefe::qt::setPlayDirection(d); },
            [] { jefe::qt::pausePlayback(); }};
}

// Keys per preset follow docs/superpowers/specs/2026-11-25-keyboard-shortcuts-design.md
// ("Presets"). Editing (default) = the 2.0 keys with the NLE conventions; classic = shipped
// JefeCheck 1.7 (`main`: UICallbacks.cpp globalCB, GlViewport.cpp FL_KEYDOWN, mainWindow.fl
// menus) plus a 2.0 key wherever 1.7 left it free. ctl() is 1.7's physical Control key.
void registerAllCommands(Registry& reg)
{
    using namespace jefe::qt;
    constexpr Scope P = Scope::Player;
    constexpr Scope G = Scope::Global;
    reg.setDefaultPreset(QStringLiteral("editing"));

    // ---- Playback (Player) -----------------------------------------------------------------
    both(reg, "playback.play-pause", "Play / Pause", Category::Playback, P, {k("Space")},
         [] { pausePlayback(); });
    // J / K / L: Editing only (L is the LUT hold key in 1.7).
    add(reg, "playback.jkl-reverse", "Play Backward", Category::Playback, P, {k("J")}, {},
        [] { runJKL(-1, bridgeTransport()); });
    add(reg, "playback.jkl-pause", "Pause", Category::Playback, P, {k("K")}, {},
        [] { runJKL(0, bridgeTransport()); });
    add(reg, "playback.jkl-forward", "Play Forward", Category::Playback, P, {k("L")}, {},
        [] { runJKL(+1, bridgeTransport()); });
    // 1.7's transport buttons: '<' / '>' toggle playing that way (mainWindow.fl playRevButton /
    // playFwdButton). Editing gives '<' / '>' to the play direction instead.
    add(reg, "playback.play-reverse", "Play Reverse (toggle)", Category::Playback, P, {},
        {QKeySequence(Qt::Key_Less)}, [] { playReverseToggle(); });
    add(reg, "playback.play-forward", "Play Forward (toggle)", Category::Playback, P, {},
        {QKeySequence(Qt::Key_Greater)}, [] { playForwardToggle(); });
    add(reg, "playback.step-back", "Step Back One Frame", Category::Playback, P, {k("Left")},
        {k("Left"), k("X")}, [] { stepFrame(-1); });
    add(reg, "playback.step-forward", "Step Forward One Frame", Category::Playback, P, {k("Right")},
        {k("Right"), k("C")}, [] { stepFrame(+1); });
    add(reg, "playback.go-start", "Go to Start", Category::Playback, P, {k("Home")}, {k("Z"), k("Home")},
        [] { rewindPlayback(); });
    add(reg, "playback.go-end", "Go to End", Category::Playback, P, {k("End")}, {k("V"), k("End")},
        [] { fastFwdPlayback(); });
    add(reg, "playback.direction-reverse", "Play Direction: Reverse", Category::Playback, P,
        {k(","), QKeySequence(Qt::Key_Less)}, {k(",")}, [] { setPlayDirection(-1); });
    add(reg, "playback.direction-forward", "Play Direction: Forward", Category::Playback, P,
        {k("."), QKeySequence(Qt::Key_Greater)}, {k(".")}, [] { setPlayDirection(+1); });
    both(reg, "playback.loop-once", "Loop Mode: Once", Category::Playback, P, {k("8")},
         [] { setLoopMode(0); });
    both(reg, "playback.loop", "Loop Mode: Loop", Category::Playback, P, {k("9")},
         [] { setLoopMode(1); });
    both(reg, "playback.bounce", "Loop Mode: Bounce", Category::Playback, P, {k("0")},
         [] { setLoopMode(2); });

    // ---- In / Out (Player) -----------------------------------------------------------------
    both(reg, "inout.set-in", "Set In Point at Playhead", Category::InOut, P, {k("I")},
         [] { setInPoint(getCurrentFrame()); });
    both(reg, "inout.set-out", "Set Out Point at Playhead", Category::InOut, P, {k("O")},
         [] { setOutPoint(getCurrentFrame()); });
    add(reg, "inout.go-in", "Go to In Point", Category::InOut, P, {k("Shift+I")}, {},
        [] { seekToFrame(getInPoint()); });
    add(reg, "inout.go-out", "Go to Out Point", Category::InOut, P, {k("Shift+O")}, {},
        [] { seekToFrame(getOutPoint()); });
    add(reg, "inout.reset-in", "Reset In Point to Start", Category::InOut, P, {k("Alt+I")},
        {k("Shift+I")}, [] { setInPoint(getFromFrame()); });
    add(reg, "inout.reset-out", "Reset Out Point to End", Category::InOut, P, {k("Alt+O")},
        {k("Shift+O")}, [] { setOutPoint(getToFrame()); });
    add(reg, "inout.clear-both", "Clear In and Out", Category::InOut, P, {k("Alt+X")}, {}, [] {
        setInPoint(getFromFrame());
        setOutPoint(getToFrame());
    });
    add(reg, "inout.set-in-load", "Set In Point and Load from Here", Category::InOut, P,
        {k("Shift+Alt+I")}, {k("Alt+I")}, [] { setInPointAndLoad(); });

    // ---- Tracks, channels, LUTs and favourites on the active plate (Player) ----------------
    both(reg, "plate.track-prev", "Previous Track on Active Plate", Category::Plate, P, {k("Up")},
         [] { cycleTrackOnActivePlate(-1); });
    both(reg, "plate.track-next", "Next Track on Active Plate", Category::Plate, P, {k("Down")},
         [] { cycleTrackOnActivePlate(+1); });
    static const char* const kShowTrack[] = {"plate.show-track-1", "plate.show-track-2",
                                             "plate.show-track-3", "plate.show-track-4"};
    static const char* const kShowTrackName[] = {"Show Track A", "Show Track B", "Show Track C",
                                                 "Show Track D"};
    for (int i = 0; i < 4; ++i) {
        const QKeySequence digit(Qt::Key(Qt::Key_1 + i));
        both(reg, kShowTrack[i], kShowTrackName[i], Category::Plate, P, {digit}, [i] { showTrackOnActive(i); });
    }
    both(reg, "color.channel-r", "Toggle Red Channel", Category::Color, P, {k("R")},
         [] { toggleChannelOnActive(0); });
    both(reg, "color.channel-g", "Toggle Green Channel", Category::Color, P, {k("G")},
         [] { toggleChannelOnActive(1); });
    both(reg, "color.channel-b", "Toggle Blue Channel", Category::Color, P, {k("B")},
         [] { toggleChannelOnActive(2); });
    both(reg, "color.channel-a", "Toggle Alpha Channel", Category::Color, P, {k("A")},
         [] { toggleChannelOnActive(3); });
    // Previous / next LUT: unbound commands in both presets; 1.7 had them as hold L + Up / Down
    // (plate.lut-hold below).
    both(reg, "plate.lut-prev", "Previous LUT on Active Plate", Category::Color, P, {},
         [] { scrollLUTOnActive(-1); });
    both(reg, "plate.lut-next", "Next LUT on Active Plate", Category::Color, P, {},
         [] { scrollLUTOnActive(+1); });
    static const char* const kCCApply[] = {"color.cc-fav-apply-1", "color.cc-fav-apply-2", "color.cc-fav-apply-3",
                                           "color.cc-fav-apply-4", "color.cc-fav-apply-5"};
    static const char* const kCCSave[] = {"color.cc-fav-save-1", "color.cc-fav-save-2", "color.cc-fav-save-3",
                                          "color.cc-fav-save-4", "color.cc-fav-save-5"};
    static const char* const kFXLoad[] = {"fx.fav-load-1", "fx.fav-load-2", "fx.fav-load-3", "fx.fav-load-4",
                                          "fx.fav-load-5"};
    static const char* const kFXAppend[] = {"fx.fav-append-1", "fx.fav-append-2", "fx.fav-append-3",
                                            "fx.fav-append-4", "fx.fav-append-5"};
    static const char* const kFXSave[] = {"fx.fav-save-1", "fx.fav-save-2", "fx.fav-save-3", "fx.fav-save-4",
                                          "fx.fav-save-5"};
    static const QByteArray kNames[5][5] = {
        {"Apply Color Favorite 1", "Apply Color Favorite 2", "Apply Color Favorite 3", "Apply Color Favorite 4", "Apply Color Favorite 5"},
        {"Save Color Favorite 1", "Save Color Favorite 2", "Save Color Favorite 3", "Save Color Favorite 4", "Save Color Favorite 5"},
        {"Load FX Favorite 1", "Load FX Favorite 2", "Load FX Favorite 3", "Load FX Favorite 4", "Load FX Favorite 5"},
        {"Append FX Favorite 1", "Append FX Favorite 2", "Append FX Favorite 3", "Append FX Favorite 4", "Append FX Favorite 5"},
        {"Save FX Favorite 1", "Save FX Favorite 2", "Save FX Favorite 3", "Save FX Favorite 4", "Save FX Favorite 5"},
    };
    for (int i = 0; i < 5; ++i) {
        const QByteArray n = QByteArray::number(i + 1);
        const QByteArray f = "F" + n;
        // Shift+digit and Ctrl+Shift+digit: CC favourites (1.7 globalCB and the View menu).
        // Shift / Ctrl / Ctrl+Shift + F1-F5: FX-stack favourites (1.7 globalCB).
#ifdef Q_OS_MACOS
        // macOS keeps Cmd+Shift+3/4/5 (screenshots) and Cmd+F5 (VoiceOver) for itself, so Editing
        // saves a colour favourite on Control+Shift+digit (1.7's physical keys) and appends an
        // FX favourite on Option+Fn (Control+Fn is macOS keyboard navigation). Classic keeps
        // 1.7's Control keys and gets no Cmd key for the saves: 1.7 ignored Cmd, so Cmd+Shift+1
        // applied a favourite there and must not overwrite one here.
        const Keys ccSaveEditing{ctl("Ctrl+Shift+" + n)}, ccSaveClassic{ctl("Ctrl+Shift+" + n)};
        const Keys fxAppendEditing{k("Alt+" + f)}, fxAppendClassic = keys({ctl("Ctrl+" + f), k("Alt+" + f)});
        const Keys fxSaveClassic{ctl("Ctrl+Shift+" + f)};
#else
        const Keys ccSaveEditing{k("Ctrl+Shift+" + n)}, ccSaveClassic{k("Ctrl+Shift+" + n)};
        const Keys fxAppendEditing{k("Ctrl+" + f)}, fxAppendClassic{k("Ctrl+" + f)};
        const Keys fxSaveClassic{k("Ctrl+Shift+" + f)};
#endif
        both(reg, kCCApply[i], kNames[0][i].constData(), Category::Color, P, {k("Shift+" + n)},
             [i] { applyCCFavoriteToActive(i); });
        add(reg, kCCSave[i], kNames[1][i].constData(), Category::Color, P, ccSaveEditing, ccSaveClassic,
            [i] {
                saveCCFavoriteFromActive(i);
                saveCCFavoritesFile(getFavoritesFilePath());   // colour favourites persist across launches
            });
        both(reg, kFXLoad[i], kNames[2][i].constData(), Category::Plate, P, {k("Shift+" + f)},
             [i] { loadFXFavoriteToActive(i); });
        add(reg, kFXAppend[i], kNames[3][i].constData(), Category::Plate, P, fxAppendEditing, fxAppendClassic,
            [i] { appendFXFavoriteToActive(i); });
        add(reg, kFXSave[i], kNames[4][i].constData(), Category::Plate, P, {k("Ctrl+Shift+" + f)}, fxSaveClassic,
            [i] { saveFXFavoriteFromActive(i); });
    }

    // ---- Timeline (Player): reserved for the timeline project, which binds the runs --------
    both(reg, "timeline.zoom-in", "Timeline: Zoom In", Category::Timeline, P, {k("=")});
    both(reg, "timeline.zoom-out", "Timeline: Zoom Out", Category::Timeline, P, {k("-")});
    both(reg, "timeline.zoom-fit", "Timeline: Zoom to Fit", Category::Timeline, P, {k("\\")});
    both(reg, "timeline.zoom-inout", "Timeline: Zoom to In / Out", Category::Timeline, P, {k("Shift+\\")});

    // ---- Remote chat (Player) --------------------------------------------------------------
    // Return (1.7: Y) starts typing a chat message while connected; Return sends it again
    // (GlViewport_Qt claims typed keys while composing). 1.7's Ctrl+Y shows / fades the chat.
    add(reg, "remote.chat-type", "Chat: Start Typing", Category::Panels, P, {k("Return"), k("Enter")},
        {k("Y")}, [] {
            if (!isRemoteConnected()) return;
            remoteChatShowNow();   // 1.7's Y also brought a faded chat back
            remoteChatBegin();
        });
    add(reg, "remote.chat-toggle", "Chat: Show / Hide", Category::Panels, P, {}, {ctl("Ctrl+Y")},
        [] { remoteChatToggleShown(); });

    // ---- Layout (Global) -------------------------------------------------------------------
    add(reg, "layout.single", "Layout: Single", Category::Layout, G, {k("Ctrl+1")},
        keys({ctl("Ctrl+1"), k("Ctrl+1")}), [] { setFramingMode(FRAMINGSINGLE_ID); });
    add(reg, "layout.side", "Layout: Side by Side", Category::Layout, G, {k("Ctrl+2")},
        keys({ctl("Ctrl+2"), k("Ctrl+2")}), [] { setFramingMode(FRAMINGDOUBLE_ID); });
    add(reg, "layout.top-bottom", "Layout: Top / Bottom", Category::Layout, G, {k("Ctrl+3")},
        keys({ctl("Ctrl+3"), k("Ctrl+3")}), [] { setFramingMode(FRAMINGDOUBLEVERT_ID); });
    add(reg, "layout.quad", "Layout: 2 x 2", Category::Layout, G, {k("Ctrl+4")},
        keys({ctl("Ctrl+4"), k("Ctrl+4")}), [] { setFramingMode(FRAMINGQUAD_ID); });

    // ---- Plate transforms (Global) ---------------------------------------------------------
    both(reg, "plate.fit", "Fit to Screen", Category::Plate, G, {k("F")}, [] { fitActivePlate(); });
    add(reg, "plate.fit-all", "Fit All Plates", Category::Plate, G, {k("Shift+F")},
        {k("Alt+F"), k("Shift+F")}, [] { fitAllPlates(); });
    // V is Go to End in 1.7: flip goes to 1.7's Ctrl+8.
    add(reg, "plate.flip", "Flip Vertical", Category::Plate, G, {k("V")}, {ctl("Ctrl+8")},
        [] { toggleFlipActive(); });
    add(reg, "plate.flip-all", "Flip All Plates", Category::Plate, G, {k("Shift+V")},
        {ctl("Ctrl+Alt+8")}, [] { toggleFlipAll(); });
    add(reg, "plate.flop", "Flop Horizontal", Category::Plate, G, {k("M")}, {k("M"), ctl("Ctrl+9")},
        [] { toggleFlopActive(); });
    add(reg, "plate.flop-all", "Flop All Plates", Category::Plate, G, {k("Shift+M")},
        {k("Shift+M"), ctl("Ctrl+Alt+9")}, [] { toggleFlopAll(); });
    both(reg, "plate.text-mode", "Text Overlay Mode", Category::Plate, G, {k("T")},
         [] { toggleTextModeActive(); });
    both(reg, "plate.text-mode-all", "Text Overlay Mode (All Plates)", Category::Plate, G,
         {k("Alt+T")}, [] { toggleTextModeAll(); });
    add(reg, "plate.reset", "Reset Plate", Category::Plate, G, {k("Ctrl+R")},
        keys({ctl("Ctrl+R"), k("Ctrl+R")}), [] { resetActivePlate(); });
    add(reg, "plate.reset-all", "Reset All Plates", Category::Plate, G, {k("Ctrl+Alt+R")},
        keys({ctl("Ctrl+Alt+R"), k("Ctrl+Alt+R")}), [] { resetAllPlates(); });
    add(reg, "view.toggle-histogram", "Show Histogram", Category::Plate, G, {k("Ctrl+H")},
        keys({ctl("Ctrl+H"), k("Ctrl+H")}), [] { toggleHistogramActiveQuad(); });
    add(reg, "view.toggle-histogram-all", "Show Histogram (All Plates)", Category::Plate, G,
        {k("Ctrl+Alt+H")}, keys({ctl("Ctrl+Alt+H"), k("Ctrl+Alt+H")}), [] { toggleHistogramAll(); });
    both(reg, "view.toggle-note-overlay", "Show Notes", Category::Plate, G, {k("N")});

    // ---- Colour (Global) -------------------------------------------------------------------
    both(reg, "color.reset", "Reset Color Correction", Category::Color, G, {k("Shift+R")},
         [] { resetActiveColorCorrection(); });
    both(reg, "color.reset-all", "Reset All Color Corrections", Category::Color, G,
         {k("Shift+Alt+R")}, [] { resetAllColorCorrections(); });

    // ---- File (Global, window-level) -------------------------------------------------------
    // 1.7's Ctrl+O opened a session; on macOS that is Control+O, so Cmd+O stays Quick Load.
    add(reg, "file.quick-load", "Quick Load…", Category::File, G, {k("Ctrl+O")},
        ctl("Ctrl+O") == k("Ctrl+O") ? Keys{} : Keys{k("Ctrl+O")});
    add(reg, "file.load-manager", "Load Sequence Manager…", Category::File, G, {k("Ctrl+L")},
        keys({ctl("Ctrl+L"), k("Ctrl+L")}));
    add(reg, "file.save-session", "Save Session", Category::File, G, standard(QKeySequence::Save),
        keys({ctl("Ctrl+S"), standard(QKeySequence::Save).value(0)}));
    add(reg, "file.open-session", "Open Session…", Category::File, G, {k("Ctrl+Shift+O")},
        keys({ctl("Ctrl+O"), k("Ctrl+Shift+O")}));
    // 1.7's Ctrl+P opened the Playlist Manager; Preferences keeps the 2.0 keys 1.7 left free.
    add(reg, "file.preferences", "Preferences…", Category::File, G, {k("Ctrl+P"), k("Ctrl+,")},
        ctl("Ctrl+P") == k("Ctrl+P") ? Keys{k("Ctrl+,")} : Keys{k("Ctrl+P"), k("Ctrl+,")});
    add(reg, "file.quit", "Quit", Category::File, G, standard(QKeySequence::Quit),
        keys({standard(QKeySequence::Quit).value(0), ctl("Ctrl+Q")}));

    // ---- Panels and window (Global, window-level) ------------------------------------------
    add(reg, "view.toggle-fullscreen", "Fullscreen", Category::Panels, G,
        {k("F11"), QKeySequence(Qt::CTRL | Qt::META | Qt::Key_F)}, {ctl("Ctrl+F"), k("F11")});
    // 1.7's F2-F6 opened windows 2.0 merged or renamed; the 2.0 panel keys are kept.
    both(reg, "panels.toggle-plate-manager", "Plate Manager", Category::Panels, G, {k("F2")});
    both(reg, "panels.toggle-fx", "FX", Category::Panels, G, {k("F3")});
    both(reg, "panels.toggle-luts", "LUTs", Category::Panels, G, {k("F4")});
    both(reg, "panels.open-remote", "Remote Session…", Category::Panels, G, {k("F5")});
    both(reg, "panels.open-render", "Render…", Category::Panels, G, {k("F6")});
    both(reg, "panels.toggle-notes", "Notes", Category::Panels, G, {k("F7")});
    both(reg, "panels.toggle-timeline", "Timeline", Category::Panels, G, {k("F8")});
    add(reg, "panels.toggle-playlist", "Playlist", Category::Panels, G, {k("F9")},
        {ctl("Ctrl+P"), k("F9")});
    add(reg, "panels.hide-controls", "Hide Controls", Category::Panels, G, {k("Ctrl+Alt+F")},
        keys({ctl("Ctrl+Alt+F"), k("Ctrl+Alt+F")}));
#ifdef Q_OS_MACOS
    both(reg, "panels.minimize", "Minimize", Category::Panels, G, {k("Ctrl+M")});
#endif

    // ---- Help (Global) ---------------------------------------------------------------------
    both(reg, "help.manual", "User Manual", Category::Help, G, {k("F1")});
    both(reg, "help.toggle-overlay", "Toggle On-Screen Help", Category::Help, G, {k("H")},
         [] { toggleOnScreenHelp(); });

    // ---- Hold keys (viewport) --------------------------------------------------------------
    hold(reg, "color.drag-gamma", "Drag Gamma", Qt::Key_W);
    hold(reg, "color.drag-exposure", "Drag Exposure", Qt::Key_E);
    hold(reg, "color.drag-brightness", "Drag Brightness", Qt::Key_Q);
    hold(reg, "color.drag-contrast", "Drag Contrast", Qt::Key_D);
    hold(reg, "color.drag-saturation", "Drag Saturation", Qt::Key_S);
    // 1.7: hold L + Up / Down = previous / next LUT (GlViewport.cpp FL_Up / FL_Down). Not in
    // Editing, where L is Play Forward.
    reg.registerHoldKey({"plate.lut-hold", QStringLiteral("Hold for Previous / Next LUT (+ Up / Down)"),
                         Qt::Key_unknown, Qt::Key_L,
                         {{Qt::Key_Up, [] { scrollLUTOnActive(-1); }},
                          {Qt::Key_Down, [] { scrollLUTOnActive(+1); }}}});
}

}  // namespace jefe::shortcuts
