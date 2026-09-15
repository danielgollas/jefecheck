#include "MainWindow_qt.h"

#include "FXLutPanel_qt.h"
#include "FXParamPanel_qt.h"
#include "GlViewport_qt.h"
#include "NotesPanel_qt.h"
#include "PlaylistPanel_qt.h"
#include "RemotePanel_qt.h"
#include "ImageLoadBridge_qt.h"
#include "LoadWindowDialog_qt.h"
#include "PlateManager_qt.h"
#include "MinSpecsDialog_qt.h"
#include "PreferencesWindow_qt.h"
#include "qt_prefs_persist.h"
#include "RenderBridge_qt.h"
#include "RenderDialog_qt.h"
#include "ReviewPackageDialog_qt.h"
#include "ReviewSummaryPdf_qt.h"
#include "VideoEncoder_qt.h"

#include <QEventLoop>
#include "SequenceLoadBridge_qt.h"
#include "TimelinePanel_qt.h"

#include "../UIConstants.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QDockWidget>
#include <QUrl>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>

#include "../gfcReviewSummary.h"
#include "../gfcNoteStore.h"
#include "../gfcreview.h"
#include "../gfcrevision.h"
#include "../gfcnotestroke.h"
#include "../gfcnotetext.h"
#include "../gfcMediaFingerprint.h"
#include "../gfcSessionPaths.h"
#include "../gfcTarArchive.h"
#include "../xmlParser.h"

#include <ctime>

namespace {
constexpr const char* kSettingsGeometry = "MainWindow/geometry";
// Bumped to _v2 to discard layouts saved while the timeline briefly forced
// an over-tall bottom dock row (which could collapse/hide the Plate Manager).
// Old "MainWindow/state" is ignored, so first-launch defaults reapply once.
constexpr const char* kSettingsState    = "MainWindow/state_v2";

// Sets *flag true for the scope and false again on every return path
// (including early returns), so a guarded function can't be mistaken for
// still running if an exception or an early `return` skips a manual reset.
struct ScopedFlag {
    bool* flag;
    explicit ScopedFlag(bool* f) : flag(f) { *flag = true; }
    ~ScopedFlag() { *flag = false; }
};
}

MainWindow_Qt::MainWindow_Qt(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("JefeCheck");
    resize(1280, 800);

    viewport_ = new GlViewport_Qt(this);
    setCentralWidget(viewport_);

    // Initialize the rendering pipeline's GUI bridges before the bridge
    // starts driving paintGL. Routed through a Qt-free TU because the
    // plateManager / trackManager headers pull glad, and Qt's
    // QOpenGLWidget can't share a TU with glad on macOS.
    jefe::qt::initializeRenderingChain();

    // Wire the JefeCheck rendering chain into the Qt viewport. The bridge
    // forwards onDraw/onResize to plateManager.draw().
    renderBridge_ = std::make_unique<jefe::qt::RenderBridge_Qt>();
    viewport_->setListener(renderBridge_.get());

    statusBar()->showMessage("Drop an image onto the viewport to load it.");

    // Restore the engine load defaults persisted in QSettings — the default
    // texture bit depth and the OIIO decode filter. Both are now edited in
    // Preferences → Engine; here we just apply the saved values at startup
    // (without opening Preferences). Centralized in qt_prefs_persist.cpp,
    // which stays the only TU responsible for the sett <-> QSettings mapping;
    // this call itself is glad-free (gfcStructures.h drags glad, which can't
    // share a TU with QOpenGLWidget on macOS — see developer_notes.md §1).
    jefe::qt::loadPreferences();

    // Permanent right-aligned label that always reflects the current
    // framing mode. Status-bar text exposes via NSAccessibility (the
    // QOpenGLWidget viewport doesn't, so we can't hang the hint there),
    // and a permanent widget never gets clobbered by transient messages
    // like the file-drop status updates.
    layoutStatusLabel_ = new QLabel(this);
    layoutStatusLabel_->setObjectName("statusbar.layout.label");
    statusBar()->addPermanentWidget(layoutStatusLabel_);

    // Mirrors the layout label, but for the active plate's currently-bound
    // track. Refreshed each tick so combo edits, keyboard cycle, and
    // active-plate clicks all flow into the visible label without each
    // path having to remember to update it. Test surface for the plate-
    // card track combo: combo title shows the GUI selection, this label
    // shows what gfcPlate::track is actually rendering — when the two
    // disagree, the bridge has regressed.
    trackStatusLabel_ = new QLabel(this);
    trackStatusLabel_->setObjectName("statusbar.track.label");
    statusBar()->addPermanentWidget(trackStatusLabel_);

    // "Loaded: <basename>" or "Loaded: -" for the active plate's
    // sequence. Refreshed each tick (cheap — two field reads off
    // gfcSequence). Permanent so the transient status-bar message
    // ("Drop an image…") doesn't clobber it. Real user value (no more
    // "what's loaded into the active plate?" guessing) plus an
    // AX-stable surface for behavioral tests asserting on load state.
    loadedStatusLabel_ = new QLabel(this);
    loadedStatusLabel_->setObjectName("statusbar.loaded.label");
    statusBar()->addPermanentWidget(loadedStatusLabel_);

    // "Startup:" label tracks the LUT/FX autoload phase. Goes through
    // 'Loading…' → 'Ready (<N> FX, <M> LUT)' → 'Errors (<details>)'
    // so the user sees a real health signal at launch and tests have
    // an AX-stable surface to poll for autoload completion before
    // asserting on panel contents.
    startupStatusLabel_ = new QLabel(this);
    startupStatusLabel_->setObjectName("statusbar.startup.label");
    startupStatusLabel_->setText("Startup: Loading…");
    statusBar()->addPermanentWidget(startupStatusLabel_);

    // Drag-drop reports a load-time scale (Shift = 0.5, Shift+Cmd = 0.25);
    // wire to the scale-aware slot. The legacy fileDropped signal is
    // still emitted by the viewport but we don't connect it — the
    // scale-aware handler covers all drag cases.
    connect(viewport_, &GlViewport_Qt::fileDroppedWithScale,
            this, &MainWindow_Qt::onFileDropped);

    setDockOptions(QMainWindow::AnimatedDocks
                   | QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks);

    buildMenuBar();
    buildDocks();
    restoreLayout();

    // Seed session state from QSettings + capture last-run clean-exit flag.
    {
        QSettings s;
        std::vector<std::string> recents;
        for (const QString& p : s.value("Session/recent").toStringList())
            recents.push_back(p.toStdString());
        jefe::qt::setRecentSessions(recents);
        std::vector<std::string> recentPlaylists;   // JEF-18
        for (const QString& p : s.value("Playlist/recent").toStringList())
            recentPlaylists.push_back(p.toStdString());
        jefe::qt::setRecentPlaylists(recentPlaylists);
        jefe::qt::setStartupSessionBehavior(
            s.value("Session/startupBehavior", 2).toInt());
        lastExitWasClean_ = s.value("Session/cleanExit", true).toBool();
        s.setValue("Session/cleanExit", false);   // unclean until proven otherwise
    }
    // App-global color-correction favorites (persist across launches).
    jefe::qt::loadCCFavoritesFile(jefe::qt::getFavoritesFilePath());

    // Window-scoped layout shortcuts. Qt's QShortcut delivers regardless
    // of whether the viewport, a dock widget, or the menu bar has focus —
    // GlViewport_Qt's keyPressEvent only fires when the viewport itself
    // has keyboard focus, which it usually doesn't after the user clicks
    // on a plate card or spinbox.
    auto layoutName = [](int framingMode) -> const char* {
        switch (framingMode) {
            case FRAMINGSINGLE_ID:     return "single";
            case FRAMINGDOUBLE_ID:     return "double-horizontal";
            case FRAMINGDOUBLEVERT_ID: return "double-vertical";
            case FRAMINGQUAD_ID:       return "quad";
            default:                   return "unknown";
        }
    };
    auto announceLayout = [this, layoutName](int framingMode) {
        if (layoutStatusLabel_) {
            layoutStatusLabel_->setText(
                QStringLiteral("Layout: %1").arg(layoutName(framingMode)));
        }
    };
    auto bindLayout = [this, announceLayout](QKeySequence seq, int framingMode) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this,
                [this, framingMode, announceLayout]() {
            jefe::qt::setFramingMode(framingMode);
            if (viewport_) viewport_->update();
            if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();
            announceLayout(framingMode);
        });
    };
    bindLayout(QKeySequence(Qt::CTRL | Qt::Key_1), FRAMINGSINGLE_ID);
    bindLayout(QKeySequence(Qt::CTRL | Qt::Key_2), FRAMINGDOUBLE_ID);
    bindLayout(QKeySequence(Qt::CTRL | Qt::Key_3), FRAMINGDOUBLEVERT_ID);
    bindLayout(QKeySequence(Qt::CTRL | Qt::Key_4), FRAMINGQUAD_ID);
    announceLayout(FRAMINGSINGLE_ID);

    // Two shortcuts open Preferences: Cmd+P (legacy from the FLTK
    // build) and Cmd+, (the macOS-standard convention). Both route
    // through the same lambda. macOS's system Print handler intercepts
    // synthesized Cmd+P delivery in some contexts (Appium / Mac2),
    // so tests prefer Cmd+, — real users get either.
    auto openPrefs = [this]() {
        PreferencesWindow_Qt dlg(this);
        // Live preview: repaint the viewport as viewport-affecting settings
        // (text style, background, aspect bars) change while the dialog is open.
        connect(&dlg, &PreferencesWindow_Qt::viewportRepaintRequested,
                this, [this]() { if (viewport_) viewport_->update(); });
        dlg.exec();
    };
    auto bindPrefsShortcut = [this, openPrefs](QKeySequence seq) {
        auto* sc = new QShortcut(seq, this);
        // ApplicationShortcut so the binding fires regardless of
        // whether the main window or a dock widget has keyboard focus —
        // synthesized keystrokes (Mac2 driver / AppleScript) don't
        // always land on the focused QMainWindow's event filter.
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, openPrefs);
    };
    bindPrefsShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    bindPrefsShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));

    // Plate-control shortcuts. Promoted from GlViewport_Qt's keyPressEvent
    // to QShortcut at ApplicationShortcut context so they fire regardless
    // of which widget has focus — clicking a plate-card spinbox or the
    // timeline shouldn't disable Fit / Flip / Flop / Text-mode the way
    // viewport-scoped handling did. Qt automatically suppresses these
    // when the focused widget consumes the key (text editors emit
    // ShortcutOverride for printable chars they're about to insert), so
    // typing 'f' into the aspect combo still works.
    //
    // Arrow keys, Space, and Left/Right step are deliberately left in
    // the viewport handler — those compete with widget-level meanings
    // (spinbox value adjust, button-press activation, text-caret motion)
    // where promoting would break expected widget behavior.
    auto bindPlateAction = [this](QKeySequence seq, std::function<void()> action) {
        auto* sc = new QShortcut(seq, this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, [this, action]() {
            action();
            if (viewport_) viewport_->update();
            if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();
        });
    };
    // JEF-17: the ACTIVE-plate transforms (Fit F, Flip V, Flop M, Text T,
    // Reset Ctrl+R, Reset-CC Shift+R) now live as View-menu QActions that own
    // their shortcut (see buildMenuBar) — a single source of truth, and the
    // menu makes them discoverable. Only the "all-plates" variants remain as
    // standalone ApplicationShortcut QShortcuts here (no menu entry). Flop is
    // on M/Shift+M (moved off H so bare H toggles the on-screen help overlay).
    // The bare `r` key FLTK uses for "toggle red channel" is intentionally NOT
    // promoted — a printable letter at app scope would block typing 'r'.
    bindPlateAction(QKeySequence(Qt::SHIFT | Qt::Key_F),
                    []() { jefe::qt::fitAllPlates(); });
    bindPlateAction(QKeySequence(Qt::SHIFT | Qt::Key_V),
                    []() { jefe::qt::toggleFlipAll(); });
    bindPlateAction(QKeySequence(Qt::SHIFT | Qt::Key_M),
                    []() { jefe::qt::toggleFlopAll(); });
    bindPlateAction(QKeySequence(Qt::ALT | Qt::Key_T),
                    []() { jefe::qt::toggleTextModeAll(); });
    bindPlateAction(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_R),
                    []() { jefe::qt::resetAllPlates(); });
    bindPlateAction(QKeySequence(Qt::SHIFT | Qt::ALT | Qt::Key_R),
                    []() { jefe::qt::resetAllColorCorrections(); });

    // LUT + FX autoload runs after the window is shown and the AX
    // system has had a chance to register it. The 250ms initial delay
    // matters: with a 0ms QTimer the load lambda fires before AppKit
    // finishes registering the window, and the WDA driver's launch
    // handshake (find element by predicate `title == 'JefeCheck'`)
    // returns 404 because the window isn't yet in the AX tree. Each
    // step then loads ONE file and re-posts via QTimer::singleShot(0)
    // so the event loop fully iterates between shader compiles —
    // paint events fire, AX queries get answered, and the user
    // sees the "Startup: Loading FXs (12/35)…" label tick forward.
    QTimer::singleShot(250, this, [this]() { startAutoload(); });

    // Session recovery runs after the GL context is alive (loadSession uploads
    // preview textures) — deferred past the autoload kick.
    QTimer::singleShot(400, this, [this]() { maybeRestoreSessionAtStartup(); });

    // General prefs (JEF-16 Task 1): apply "start in fullscreen" / "open
    // Load window at startup" now that loadPreferences() (above) has
    // populated `sett`. Deferred via singleShot(0) like the other startup
    // hooks above, so this runs once the event loop is pumping — main_qt.cpp
    // has already called show() by the time this fires — rather than
    // fighting the window manager mid-construction. Bridged through
    // SequenceLoadBridge_qt so this TU stays glad-free (see developer_notes.md §1).
    QTimer::singleShot(0, this, [this]() {
        if (jefe::qt::getStartFullscreen()) showFullScreen();
        if (jefe::qt::getOpenLoadWindowAtStartup()) openLoadWindow();
    });

    // Fast playback tick (~250Hz) for tight FPS pacing. The frame-advance
    // logic in gfcPlaybackManager accumulates real wall-clock time and only
    // advances when it crosses the target frame interval, so ticking finely
    // lets a frame advance land within a few ms of its true time instead of
    // being quantized to a coarse interval (a 16Hz/60Hz tick made a 24fps
    // target wobble ±0.1 because 41.67ms can't sit on a 16ms grid). The
    // FLTK build got the same effect from a near-continuous idle loop.
    //
    // Affording the fine tick requires keeping the per-tick cost low: the
    // expensive makeCurrent/doneCurrent pair (each flushes the CGL command
    // buffer + flips the context TLS slot on macOS) only runs when a decoded
    // frame is actually waiting to upload, not on every tick.
    playbackTimer_ = new QTimer(this);
    playbackTimer_->setTimerType(Qt::PreciseTimer);
    playbackTimer_->setInterval(4);
    connect(playbackTimer_, &QTimer::timeout, this, [this]() {
        if (!viewport_) return;
        // The 4ms timer fires at 250Hz for playback-pacing precision, but a
        // QOpenGLWidget::update() on *every* tick starves the macOS paint
        // event loop — paintGL never wins and the viewport freezes (gray
        // frame after load, pointers/trails invisible until playback shifts
        // the cadence). So collect the repaint *intent* here and flush it
        // through a ~60Hz coalescing throttle at the end of the tick. A
        // skipped repaint stays pending and flushes on a later tick (the
        // timer is always on), so the trailing frame is never dropped.
        bool wantRepaint = false;

        // Service the RakNet sockets on every tick — inbound messages must
        // be received even while playback is idle. Cheap (non-blocking).
        // Returns true when connection/chat state changed OR an inbound packet
        // applied mirrored state: refresh the panel and repaint the viewport so
        // remote changes show without needing a local interaction on this side.
        if (jefe::qt::pumpNetwork()) {
            if (remoteDialog_) remoteDialog_->refreshConnectionState();
            // JEF-39: pumpNetwork() also drains inbound note add/remove/lock
            // events into the in-memory review store, folded into the same
            // "changed" signal as chat/participants -- refresh the dock so a
            // remote peer's markup shows up without a local interaction.
            if (notesPanelWidget_) notesPanelWidget_->refresh();
            wantRepaint = true;
        }
        // Skip everything when nothing is playing and no raw frames are
        // pending. needsPlaybackTick is an isPlaying check + 4 O(1)
        // queue::empty() probes.
        const bool needsTick = jefe::qt::needsPlaybackTick();
        // A time-based animation (fading pointer trail, flip/flop settle, status
        // overlay fade) driving the repaint on its own — no mouse, no playback,
        // no inbound packet this tick. macOS does NOT service an async
        // QOpenGLWidget::update() posted from a timer while the app is otherwise
        // idle (mouse-drag and playback supply the OS events that flush paints),
        // so such animations freeze until the next real event. Flushing those
        // with a synchronous repaint() instead forces the paint immediately.
        const bool animActive = jefe::qt::hasActiveViewportAnimation();
        bool dirty = false;
        if (needsTick) {
            // No-GL timing step — advances currentFrame at the target FPS
            // and updates animations. Cheap enough to run at the full tick
            // rate, which is what keeps pacing tight.
            dirty = jefe::qt::tickPlaybackTiming();
            // Only enter the GL context when there's a decoded frame queued
            // for upload — gates the costly makeCurrent/doneCurrent pair so
            // the fast tick doesn't thrash the context.
            if (jefe::qt::hasPendingTextureUploads()) {
                viewport_->makeCurrent();
                jefe::qt::uploadPendingTextures();
                viewport_->doneCurrent();
                dirty = true;
            }
            // Repaint on a new frame (dirty) OR while any time-based animation
            // is settling: updateAnimations() advances flip/flop, pointer-trail
            // fade, and the overlay status fade without always flipping the
            // changed flag, so those would otherwise only animate during
            // playback. hasActiveViewportAnimation() keeps them repainting while
            // stopped.
            if (dirty || animActive) {
                wantRepaint = true;
            }
        } else if (jefe::qt::consumePlateChanged()) {
            // Stopped and nothing animating, but a bare setChanged() landed
            // (any state edit whose call site didn't force its own repaint —
            // e.g. a mirrored remote change that isn't an animation). Honor the
            // dirty flag so the viewport still refreshes without needing a
            // local interaction. Drained here exactly once (tickPlaybackTiming
            // drains it in the needsTick branch instead).
            wantRepaint = true;
        }

        // ~60Hz coalescing repaint flush. Keeps at most one update() in
        // flight per display refresh so paintGL is never starved, while a
        // pending intent always flushes within ~16ms.
        static QElapsedTimer repaintThrottle;
        static bool repaintPending = false;
        if (!repaintThrottle.isValid()) repaintThrottle.start();
        if (wantRepaint) repaintPending = true;
        if (repaintPending && repaintThrottle.elapsed() >= 16) {
            // Synchronous repaint for idle animations (macOS drops async timer
            // updates when idle); async update() everywhere else so normal
            // playback stays vsync-friendly and coalesced.
            if (animActive)
                viewport_->repaint();
            else
                viewport_->update();
            repaintPending = false;
            repaintThrottle.restart();
        }
        // The timeline/status read-back only needs ~60Hz, so throttle it to
        // every 4th tick (≈16ms) rather than running it at the full 250Hz
        // playback rate. refreshFromPlayback animates the playhead; 60Hz is
        // plenty smooth and avoids 4× the signal-blocked widget churn.
        if (++uiRefreshCounter_ < 4) return;
        uiRefreshCounter_ = 0;
        // Playlist auto-advance: the bridge latches a one-shot when forward
        // ONCE-mode playback hits the end; the panel decides whether it's
        // armed and what "next" is. Cheap: a bool read on most ticks.
        if (jefe::qt::consumePlaylistAdvanceSignal() && playlistPanelWidget_) {
            playlistPanelWidget_->advanceToNext();
        }
        // Pull playback state into the timeline widgets.
        // Cheap (a handful of getters + signal-blocked setValues), and
        // it's the only path that animates the playhead during play.
        if (timelinePanelWidget_) {
            timelinePanelWidget_->refreshFromPlayback();
        }
        // Active-plate track readout. Reading through the bridge so the
        // value reflects gfcPlate::track (post-`updateValuesFromGUI`),
        // not the GUI's parallel `trackChoice_`. Doing this in the tick
        // sidesteps wiring change-signals from every path that mutates
        // the active plate or its track.
        if (trackStatusLabel_) {
            const int active = jefe::qt::getActivePlate();
            const int track = active >= 0
                ? jefe::qt::getTrackOnPlate(active) : -1;
            QString label;
            if (active < 0 || track < 0 || track > 3) {
                label = QStringLiteral("Track: -");
            } else {
                const QChar letter = QChar('A' + track);
                label = QStringLiteral("Track: %1").arg(letter);
            }
            if (trackStatusLabel_->text() != label) {
                trackStatusLabel_->setText(label);
            }
        }
        // Active-plate "Loaded:" readout. Reading through the bridge
        // so the value reflects gfcSequence's actual loaded state, not
        // a parallel mirror — bug regressions in the load path will
        // show up as the label staying on "Loaded: -" after a
        // successful load. Doing this in the tick sidesteps wiring
        // change-signals from every load path.
        if (loadedStatusLabel_) {
            const int active = jefe::qt::getActivePlate();
            const std::string name = active >= 0
                ? jefe::qt::getLoadedSequenceName(active)
                : std::string{};
            const QString label = name.empty()
                ? QStringLiteral("Loaded: -")
                : QStringLiteral("Loaded: %1")
                      .arg(QString::fromStdString(name));
            if (loadedStatusLabel_->text() != label) {
                loadedStatusLabel_->setText(label);
            }
        }
    });
    playbackTimer_->start();
}

// Out-of-line destructor: lets the unique_ptr<RenderBridge_Qt> see the
// full RenderBridge_Qt definition (included above) when generating the
// deleter, instead of forcing the header to include RenderBridge_qt.h.
MainWindow_Qt::~MainWindow_Qt() = default;

void MainWindow_Qt::buildMenuBar() {
    auto* mb = menuBar();
    mb->setObjectName("menubar");

    auto* fileMenu = mb->addMenu("&File");
    fileMenu->setObjectName("menu.file");
    // Pick one file via QFileDialog and route it into the active
    // plate using the same path drag-drop uses (loadFileIntoPlate →
    // loadPreview → optional async sequence load). The dialog opens
    // at the directory of the most recently loaded file (persisted in
    // QSettings) so the user can step through a folder of takes
    // without re-navigating each time. A fully-featured Load Manager
    // (per-track frame range, scale, gamma, channel picker à la the
    // FLTK loadWindow) is intentionally deferred — UX revision needed
    // first, per the migration plan's PR-LAST note.
    auto* loadAction = fileMenu->addAction("&Quick Load…",
                        QKeySequence(Qt::CTRL | Qt::Key_O),
                        this, [this]() {
        QSettings settings;
        // Prefer the most-recently-used load directory; fall back to the
        // General-prefs default browse path (JEF-16 Task 1), then to the
        // platform Pictures folder if neither is set.
        QString lastDir = settings.value("MainWindow/lastLoadDir", QString()).toString();
        if (lastDir.isEmpty()) {
            lastDir = QString::fromStdString(jefe::qt::getDefaultBrowsePath());
        }
        if (lastDir.isEmpty()) {
            lastDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        }
        const QString filter = tr(
            "Image files (*.exr *.dpx *.png *.jpg *.jpeg *.tif *.tiff "
            "*.tga *.bmp);;All files (*)");
        const QString chosen = QFileDialog::getOpenFileName(
            this, tr("Load Sequence"), lastDir, filter);
        if (chosen.isEmpty()) return;
        settings.setValue("MainWindow/lastLoadDir",
                          QFileInfo(chosen).absolutePath());
        const int plate = jefe::qt::getActivePlate();
        // Active plate is 0-based and getActivePlate clamps to a valid
        // index (default 0) — no out-of-range path to guard.
        loadFileIntoPlate(plate, chosen);
    });
    loadAction->setObjectName("menu.file.load");

    auto* loadMgrAction = fileMenu->addAction(tr("Load Sequence Manager…"));
    loadMgrAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    loadMgrAction->setObjectName("menu.file.loadmgr");
    connect(loadMgrAction, &QAction::triggered, this, &MainWindow_Qt::openLoadWindow);

    fileMenu->addSeparator();
    auto* saveSessAction = fileMenu->addAction(tr("&Save Session"),
        QKeySequence(QKeySequence::Save), this, [this]() { doSaveSession(false); });
    saveSessAction->setObjectName("menu.file.savesession");
    auto* saveAsAction = fileMenu->addAction(tr("Save Session &As…"),
        this, [this]() { doSaveSession(true); });
    saveAsAction->setObjectName("menu.file.savesessionas");
    auto* openSessAction = fileMenu->addAction(tr("&Open Session…"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O),
        this, [this]() { doOpenSession(); });
    openSessAction->setObjectName("menu.file.opensession");
    recentMenu_ = fileMenu->addMenu(tr("Recent Sessions"));
    recentMenu_->setObjectName("menu.file.recent");

    // JEF-18: playlist open + recent list, parallel to the Session pair above.
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Open &Playlist…"), this, [this]() { doOpenPlaylist(); })
        ->setObjectName("menu.file.openplaylist");
    recentPlaylistMenu_ = fileMenu->addMenu(tr("Recent Playlists"));
    recentPlaylistMenu_->setObjectName("menu.file.recentplaylists");

    // JEF-41: attach the active frame's notes to a copy of its EXR.
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Stamp Notes into EXR…"), this, [this]() {
        const QString out = QFileDialog::getSaveFileName(
            this, tr("Stamp Notes into EXR"), QString(), tr("OpenEXR (*.exr)"));
        if (out.isEmpty()) return;
        QString message;
        stampActiveFrameNotes(out, &message);
        statusBar()->showMessage(message, 8000);
    })->setObjectName("menu.file.stampnotes");

    fileMenu->addAction(tr("Export Review Summary…"), this, [this]() {
        QString filter;
        QString out = QFileDialog::getSaveFileName(
            this, tr("Export Review Summary"), QString(),
            tr("PDF (*.pdf);;Text (*.txt);;CSV (*.csv)"), &filter);
        if (out.isEmpty()) return;
        if (QFileInfo(out).suffix().isEmpty()) {
            out += filter.startsWith("Text") ? ".txt" : filter.startsWith("CSV") ? ".csv" : ".pdf";
        }
        ReviewSummaryStats stats;
        QString message;
        if (exportReviewSummary(out, &stats, &message)) {
            statusBar()->showMessage(message, 8000);
        } else {
            QMessageBox::warning(this, tr("Export Review Summary"), message);
        }
    })->setObjectName("menu.file.exportsummary");

    fileMenu->addAction(tr("Export Review Package…"), this, [this]() {
        ReviewPackageDialog_Qt dialog(
            [this](const QString& out, bool includeMedia, jefe::qt::package::ExportInput& input, QString* message) {
                return gatherPackageInput(out, includeMedia, input, message);
            },
            packageMediaBytes(), this);
        dialog.exec();
        if (!dialog.lastMessage().isEmpty()) statusBar()->showMessage(dialog.lastMessage(), 8000);
    })->setObjectName("menu.file.exportpackage");

    fileMenu->addAction(tr("Open Review Package…"), this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open Review Package"), QString(),
                                                          tr("JefeCheck Review Package (*.jcreview)"));
        if (path.isEmpty()) return;
        PackageStats stats;
        QString message;
        if (!openReviewPackage(path, true, &stats, &message)) {
            QMessageBox::warning(this, tr("Open Review Package"), message);
            return;
        }
        statusBar()->showMessage(message, 8000);
        QStringList problems;
        if (!stats.missingMedia.isEmpty()) problems << tr("Media not found: %1").arg(stats.missingMedia.join(", "));
        if (!stats.missingFx.isEmpty()) problems << tr("FX not installed: %1").arg(stats.missingFx.join(", "));
        if (!stats.notesProblems.isEmpty()) problems << tr("Notes not merged:\n%1").arg(stats.notesProblems.join("\n"));
        if (!stats.lutsNotLoaded.isEmpty()) problems << tr("LUTs not loaded: %1").arg(stats.lutsNotLoaded.join(", "));
        if (!problems.isEmpty()) {
            QMessageBox::information(this, tr("Open Review Package"), problems.join("\n"));
        }
    })->setObjectName("menu.file.openpackage");

    // Rebuild both recent submenus each time the File menu opens.
    connect(fileMenu, &QMenu::aboutToShow, this, [this]() {
        rebuildRecentSessionsMenu();
        rebuildRecentPlaylistsMenu();
    });

    // JEF-17: Render… and Remote Session… used to be duplicated here and in
    // the old "Dialogs" menu. They now live only in the Window menu (dialog
    // launchers alongside the panel toggles), so File no longer carries them.
    fileMenu->addSeparator();
    auto* prefsAction = fileMenu->addAction("&Preferences…",
                        QKeySequence(Qt::CTRL | Qt::Key_P),
                        this, [this]() {
                            // Modal — settings persist on Done via
                            // jefe::qt::writePreferences() inside the dialog.
                            PreferencesWindow_Qt dlg(this);
                            connect(&dlg, &PreferencesWindow_Qt::viewportRepaintRequested,
                                    this, [this]() { if (viewport_) viewport_->update(); });
                            dlg.exec();
                        });
    prefsAction->setObjectName("menu.file.preferences");
    // Suppress Qt's auto-detection of "Preferences..." titles. By
    // default Qt moves such actions into the macOS Application menu
    // and steals Cmd+, as the bound shortcut, which then races our
    // window-level QShortcut for Cmd+, and intermittently no-ops on
    // synthesized keystrokes (UI tests).
    prefsAction->setMenuRole(QAction::NoRole);
    fileMenu->addSeparator();
    fileMenu->addAction("&Quit",
                        QKeySequence::Quit,
                        []() { QApplication::quit(); })
            ->setObjectName("menu.file.quit");

    auto* viewMenu = mb->addMenu("&View");
    viewMenu->setObjectName("menu.view");
    // Fullscreen toggle: F11 (cross-platform) + Cmd+Ctrl+F (macOS
    // native standard). FLTK uses Cmd+F (0x40066), but Cmd+F is
    // already conventionally Find on macOS — the F11 / Cmd+Ctrl+F
    // pairing is what every modern macOS app uses, and we have no
    // Find functionality to conflict with anyway.
    auto* fullscreenAction = viewMenu->addAction("&Fullscreen",
        this, [this]() {
            if (isFullScreen()) {
                showNormal();
            } else {
                showFullScreen();
            }
        });
    fullscreenAction->setObjectName("menu.view.fullscreen");
    fullscreenAction->setShortcuts({
        QKeySequence(Qt::Key_F11),
        QKeySequence(Qt::CTRL | Qt::META | Qt::Key_F),
    });
    fullscreenAction->setCheckable(true);
    // Keep the menu checkmark in sync with the actual window state —
    // user can also toggle via the macOS green window-zoom button or
    // the system menu's Enter Full Screen, and the action's check
    // mark would otherwise drift.
    connect(fullscreenAction, &QAction::triggered, this, [fullscreenAction, this]() {
        fullscreenAction->setChecked(isFullScreen());
    });
    viewMenu->addSeparator();
    // Histogram overlay (gfcPlate's in-viewport draggable sub-window).
    // Ctrl+H toggles it on the active quad, Ctrl+Alt+H on all plates —
    // matches FLTK. Routed through the bridge; the render path already
    // draws the window when visible.
    viewMenu->addAction(tr("Show &Histogram"), QKeySequence(Qt::CTRL | Qt::Key_H),
                        this, []() {
        jefe::qt::toggleHistogramActiveQuad();
    })->setObjectName("menu.view.histogram");
    viewMenu->addAction(tr("Show Histogram (All Plates)"),
                        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_H),
                        this, []() {
        jefe::qt::toggleHistogramAll();
    })->setObjectName("menu.view.histogramall");
    // JEF-39: bare N toggles note-overlay visibility (verified free --
    // F H L M O P R T V are taken). ApplicationShortcut context matches the
    // bare-H help-overlay toggle below so it fires regardless of which
    // dock/widget has focus.
    auto* notesVisibleAction = viewMenu->addAction(
        tr("Show &Notes"), QKeySequence(Qt::Key_N), this, [this]() {
            jefe::qt::toggleNotesVisible();
            if (viewport_) viewport_->update();
        });
    notesVisibleAction->setShortcutContext(Qt::ApplicationShortcut);
    notesVisibleAction->setCheckable(true);
    notesVisibleAction->setChecked(jefe::qt::notesVisible());
    notesVisibleAction->setObjectName("menu.view.notesvisible");
    viewMenu->addSeparator();
    // Toggle actions for each dock. createDockWidget() exposes a built-in
    // toggleViewAction() that flips visibility and tracks state for us.
    auto rememberDockToggle = [viewMenu](QDockWidget* d) {
        if (!d) return;
        viewMenu->addAction(d->toggleViewAction());
    };
    // Filled in after buildDocks() runs, see below.
    (void)rememberDockToggle;

    // Status bar show/hide. Checkable, persisted in QSettings; the saved
    // state is applied to the status bar at startup just below.
    {
        QSettings settings;
        const bool visible =
            settings.value("UI/statusBarVisible", true).toBool();
        statusBar()->setVisible(visible);
        auto* sbAction = viewMenu->addAction(tr("Show Status &Bar"));
        sbAction->setObjectName("menu.view.statusbar");
        sbAction->setCheckable(true);
        sbAction->setChecked(visible);
        connect(sbAction, &QAction::toggled, this, [this](bool on) {
            statusBar()->setVisible(on);
            QSettings s;
            s.setValue("UI/statusBarVisible", on);
        });
    }

    // View → Color Correction Favorites: 5 save/load slots on the active plate.
    // Menu-only (no Ctrl+1-5 shortcuts — they'd collide with the layout ones).
    viewMenu->addSeparator();
    auto* ccFavMenu = viewMenu->addMenu(tr("Color Correction Favorites"));
    ccFavMenu->setObjectName("menu.view.ccfavorites");
    for (int i = 0; i < 5; ++i) {
        QAction* save = ccFavMenu->addAction(tr("Save to Slot %1").arg(i + 1));
        save->setObjectName(QString("menu.view.ccfav.save.%1").arg(i));
        connect(save, &QAction::triggered, this, [this, i]() {
            jefe::qt::saveCCFavoriteFromActive(i);
            jefe::qt::saveCCFavoritesFile(jefe::qt::getFavoritesFilePath());
            statusBar()->showMessage(
                tr("Saved color correction to favorite %1").arg(i + 1), 3000);
        });
    }
    ccFavMenu->addSeparator();
    for (int i = 0; i < 5; ++i) {
        QAction* load = ccFavMenu->addAction(tr("Load Slot %1").arg(i + 1));
        load->setObjectName(QString("menu.view.ccfav.load.%1").arg(i));
        connect(load, &QAction::triggered, this, [this, i]() {
            jefe::qt::applyCCFavoriteToActive(i);
            if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();
            if (viewport_) viewport_->update();
            statusBar()->showMessage(
                tr("Loaded color correction favorite %1").arg(i + 1), 3000);
        });
    }

    // JEF-17: the active-plate transforms, folded into the View menu so they
    // are discoverable (previously keyboard-only). Each QAction owns its
    // shortcut at ApplicationShortcut context (fires regardless of focus,
    // including a floating dock) — the single source of truth for these keys.
    // The "all-plates" variants stay as bare QShortcuts (ctor) and are listed
    // in the on-screen help overlay. Flip/Flop also have per-Plate-Manager-
    // card toggle buttons.
    auto addPlateMenuAction = [this](QMenu* menu, const QString& text,
                                     const QKeySequence& seq,
                                     std::function<void()> fn,
                                     const QString& objName) {
        QAction* a = menu->addAction(text, this, [this, fn]() {
            fn();
            if (viewport_) viewport_->update();
            if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();
        });
        if (!seq.isEmpty()) {
            a->setShortcut(seq);
            a->setShortcutContext(Qt::ApplicationShortcut);
        }
        a->setObjectName(objName);
        return a;
    };
    viewMenu->addSeparator();
    addPlateMenuAction(viewMenu, tr("Fit to Screen"), QKeySequence(Qt::Key_F),
                       []() { jefe::qt::fitActivePlate(); }, "menu.view.fit");
    addPlateMenuAction(viewMenu, tr("Flip Vertical"), QKeySequence(Qt::Key_V),
                       []() { jefe::qt::toggleFlipActive(); }, "menu.view.flip");
    addPlateMenuAction(viewMenu, tr("Flop Horizontal"), QKeySequence(Qt::Key_M),
                       []() { jefe::qt::toggleFlopActive(); }, "menu.view.flop");
    addPlateMenuAction(viewMenu, tr("Text Overlay Mode"), QKeySequence(Qt::Key_T),
                       []() { jefe::qt::toggleTextModeActive(); },
                       "menu.view.textmode");
    viewMenu->addSeparator();
    addPlateMenuAction(viewMenu, tr("Reset Plate"),
                       QKeySequence(Qt::CTRL | Qt::Key_R),
                       []() { jefe::qt::resetActivePlate(); },
                       "menu.view.resetplate");
    addPlateMenuAction(viewMenu, tr("Reset Color Correction"),
                       QKeySequence(Qt::SHIFT | Qt::Key_R),
                       []() { jefe::qt::resetActiveColorCorrection(); },
                       "menu.view.resetcc");

    // JEF-17: the consolidated "Panels" menu replaces the legacy "Dialogs"
    // menu. Named "Panels" (not "Window") so it doesn't imply the macOS-
    // standard Window-menu commands (Minimize/Zoom/window-list) we don't ship.
    // Created empty here (so it sits between View and Help in the menu bar) and
    // fully populated in buildDocks() once the docks exist — every panel toggle
    // plus the Remote/Render launchers and Hide Controls.
    panelsMenu_ = mb->addMenu(tr("&Panels"));
    panelsMenu_->setObjectName("menu.panels");

    auto* helpMenu = mb->addMenu("&Help");
    helpMenu->setObjectName("menu.help");
    helpMenu->addAction(tr("User &Manual"), QKeySequence(Qt::Key_F1), this, []() {
        QDesktopServices::openUrl(QUrl(
            "https://github.com/danielgollas/jefecheck/blob/main/docs/manual.md"));
    })->setObjectName("menu.help.manual");
    helpMenu->addAction(tr("&Quick Start Guide"), this, []() {
        QDesktopServices::openUrl(QUrl(
            "https://github.com/danielgollas/jefecheck/blob/main/docs/quick-start.md"));
    })->setObjectName("menu.help.quickstart");
    // FLTK's "Online Support" / "Video Tutorials" pointed at the dead
    // jefecorp.com domain. The open-source release routes support to the
    // GitHub issue tracker instead; there are no video tutorials to link.
    helpMenu->addAction(tr("&Report an Issue…"), this, []() {
        QDesktopServices::openUrl(QUrl(
            "https://github.com/danielgollas/jefecheck/issues"));
    })->setObjectName("menu.help.issues");
    helpMenu->addSeparator();
    // On-screen help overlay (FLTK's bare 'h' toggleHelp). Bare H is bound
    // here (JEF-17): ApplicationShortcut context so it fires regardless of
    // which widget/floating dock has focus, matching Fit/Flip/Flop/Text.
    // Flop moved to M to free this key.
    auto* onScreenHelpAction = helpMenu->addAction(
        tr("Toggle On-Screen &Help"), QKeySequence(Qt::Key_H), this, []() {
            jefe::qt::toggleOnScreenHelp();
        });
    onScreenHelpAction->setShortcutContext(Qt::ApplicationShortcut);
    onScreenHelpAction->setObjectName("menu.help.onscreen");
    helpMenu->addSeparator();
    auto* aboutAction = helpMenu->addAction("&About JefeCheck",
        this, [this]() {
            QMessageBox::about(this, tr("About JefeCheck"),
                tr("<h3>JefeCheck %1</h3>"
                   "<p>Professional video frame review and color correction.</p>"
                   "<p>By Daniel Gollas &lt;gollas@jefecorp.com&gt;<br>"
                   "Originally written 2006-2014, modernized 2026 for "
                   "open-source release under GPL v2.</p>"
                   "<p><a href='https://github.com/danielgollas/jefecheck'>"
                   "github.com/danielgollas/jefecheck</a></p>"
                   "<p style='font-size:small;color:gray'>Built %2 %3</p>")
                // Hardcoded copy of gfcStructures.h's JEFE_VERSION —
                // can't include the header here because it pulls glad,
                // which doesn't share a TU with Qt's QtGui on macOS.
                // Bumped together with the source-of-truth define and
                // CMakeLists.txt's project() VERSION, per CLAUDE.md.
                .arg(QStringLiteral("1.7.0"))
                .arg(QStringLiteral(__DATE__))
                .arg(QStringLiteral(__TIME__)));
        });
    aboutAction->setObjectName("menu.help.about");
    // Suppress Qt's macOS auto-promotion of "About …" actions into
    // the application menu — that would steal the action and the
    // bundled copy in Help would silently disappear.
    aboutAction->setMenuRole(QAction::NoRole);

    auto* specsAction = helpMenu->addAction("&System Specs…",
        this, [this]() {
            // Modal — read-only snapshot. The capture happens once on
            // first paintGL (RenderBridge_Qt::onGLInit), so opening
            // the dialog before the viewport renders shows a "not yet
            // captured" warning instead of empty fields.
            MinSpecsDialog_Qt dlg(this);
            dlg.exec();
        });
    specsAction->setObjectName("menu.help.specs");
    specsAction->setMenuRole(QAction::NoRole);
}

void MainWindow_Qt::buildDocks() {
    // Plate Manager — bottom-left of the bottom dock area.
    plateDock_ = new QDockWidget("Plate Manager", this);
    plateDock_->setObjectName("dock.platemanager");
    plateDock_->setAccessibleName("Plate Manager dock");
    plateManagerWidget_ = new PlateManager_Qt(plateDock_);
    plateDock_->setWidget(plateManagerWidget_);
    plateDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::BottomDockWidgetArea, plateDock_);

    // Mirror viewport-driven plate edits (drag pan, wheel zoom, keyboard
    // shortcuts) back into the plate cards so the spinboxes stay in sync.
    // QueuedConnection so the slot runs asynchronously in the event
    // loop rather than synchronously inside mouseMoveEvent — keeps the
    // viewport's drag-event handler returning fast and lets Qt
    // coalesce repeated posts when emit-rate exceeds the event-loop
    // service rate. mouseReleaseEvent fires this so the inactive-plate
    // cards and FX panel get their one-shot sync at the end of drag.
    connect(viewport_, &GlViewport_Qt::plateStateChanged,
            plateManagerWidget_, &PlateManager_Qt::refreshAllCards,
            Qt::QueuedConnection);

    // Lightweight per-frame drag signal — only refreshes the four
    // transform spinboxes on the dragged plate, no FX panel cascade.
    // QueuedConnection again so the slot doesn't block mouseMoveEvent.
    connect(viewport_, &GlViewport_Qt::plateTransformChanged,
            plateManagerWidget_, &PlateManager_Qt::refreshPlateTransform,
            Qt::QueuedConnection);

    // Sibling of plateTransformChanged for the W/E/Q/D/S color-
    // correction drag interactions — refreshes only the BCS/gamma/
    // exposure spinboxes on the affected plate. Same queued, gated
    // pattern so the per-frame cost stays bounded.
    connect(viewport_, &GlViewport_Qt::plateColorChanged,
            plateManagerWidget_, &PlateManager_Qt::refreshPlateColor,
            Qt::QueuedConnection);

    // The Plate Manager fixes its own size to the packed card grid (2×2 when
    // horizontal, a single narrow column when vertical). When docked it shares
    // a row/column with the Timeline, and QMainWindow otherwise leaves the
    // shared extent at the (taller) neighbor's size, padding the Plate Manager
    // with empty space. pinPlateDock pulls the shared extent down to the
    // panel's own size hint; the Timeline — which can shrink — follows.
    // Deferred a tick so it runs after the panel has re-laid-out for the new
    // orientation (its sizeHint is only correct post-arrange).
    auto pinPlateDock = [this]() {
        if (!plateDock_ || plateDock_->isFloating()) return;
        QTimer::singleShot(0, this, [this]() {
            if (!plateDock_ || plateDock_->isFloating()) return;
            const QSize hint = plateManagerWidget_->sizeHint();
            const Qt::DockWidgetArea area = dockWidgetArea(plateDock_);
            const bool side = (area == Qt::LeftDockWidgetArea ||
                               area == Qt::RightDockWidgetArea);
            if (side) {
                resizeDocks({plateDock_}, {hint.width()}, Qt::Horizontal);
            } else {
                resizeDocks({plateDock_}, {hint.height()}, Qt::Vertical);
            }
        });
    };

    // Orientation follows the dock edge: a left/right edge gives the
    // narrow-tall column form, every other edge (and floating) the
    // wide-short row form. The Plate Manager pins its own cross-axis extent
    // once it knows the orientation.
    connect(plateDock_, &QDockWidget::dockLocationChanged,
            plateManagerWidget_, [this, pinPlateDock](Qt::DockWidgetArea area) {
                plateManagerWidget_->setOrientation(
                    area == Qt::LeftDockWidgetArea ||
                    area == Qt::RightDockWidgetArea);
                pinPlateDock();
            });
    // Floating reads as horizontal (the dock has no edge to key off); when it
    // re-docks, pin the row/column down to the panel again.
    connect(plateDock_, &QDockWidget::topLevelChanged,
            plateManagerWidget_, [this, pinPlateDock](bool floating) {
                if (floating) plateManagerWidget_->setOrientation(false);
                else pinPlateDock();
            });
    // Initial edge is Bottom ⇒ horizontal.
    plateManagerWidget_->setOrientation(false);
    pinPlateDock();

    // Timeline + Transport — bottom-right; split alongside the plate dock.
    timelineDock_ = new QDockWidget("Timeline", this);
    timelineDock_->setObjectName("dock.timeline");
    timelineDock_->setAccessibleName("Timeline dock");
    timelinePanelWidget_ = new TimelinePanel_Qt(timelineDock_);
    timelineDock_->setWidget(timelinePanelWidget_);
    timelineDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::BottomDockWidgetArea, timelineDock_);

    // Place the timeline to the right of the plate manager so they share the
    // bottom strip side-by-side.
    splitDockWidget(plateDock_, timelineDock_, Qt::Horizontal);

    // LUTs — right side.
    lutDock_ = new QDockWidget("LUTs", this);
    lutDock_->setObjectName("dock.luts");
    lutDock_->setAccessibleName("LUT browser dock");
    lutPanelWidget_ = new LUTPanel_Qt(lutDock_);
    lutDock_->setWidget(lutPanelWidget_);
    lutDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::RightDockWidgetArea, lutDock_);

    // FX — the combined effect-controls panel for the active plate
    // (+ Add FX menu, per-FX cards with active/remove + inline params,
    // drag-to-reorder). Lives on the left side of the window so it
    // doesn't compete with the LUT dock for vertical real estate, and so
    // the value-text propagates to AX (Mac's AX bridge can elide AXValue
    // for 0-sized labels in tab-overflowed docks). See developer_notes §23.
    fxParamsDock_ = new QDockWidget("FX", this);
    fxParamsDock_->setObjectName("dock.fxparams");
    fxParamsDock_->setAccessibleName("FX parameters dock");
    fxParamPanelWidget_ = new FXParamPanel_Qt(fxParamsDock_);
    fxParamsDock_->setWidget(fxParamPanelWidget_);
    fxParamsDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    // Both dimensions are needed: left-area docks default to zero
    // height when no other dock claims that area, which collapses the
    // scroll viewport and prunes the editor widgets from the Mac AX
    // tree (the status label survives because it's outside the
    // scroll area).
    // Floor wide enough that the per-FX header (drag handle + name + active
    // checkbox + remove button) is always fully visible; the cards never
    // scroll horizontally, so the buttons can't slide off-view.
    fxParamPanelWidget_->setMinimumWidth(150);
    fxParamPanelWidget_->setMinimumHeight(240);
    addDockWidget(Qt::LeftDockWidgetArea, fxParamsDock_);

    // Playlist — left side, vertically split below FX Params.
    // Tabifying with FX Params destabilized the AX bridge's view of
    // the param-panel's editor widgets under sweep load (Mac2
    // occasionally couldn't resolve fxparams.fx0.param.*.spin even
    // though the panel had built them); splitting keeps both panels
    // rendered and AX-visible without overlapping.
    playlistDock_ = new QDockWidget("Playlist", this);
    playlistDock_->setObjectName("dock.playlist");
    playlistDock_->setAccessibleName("Playlist dock");
    playlistPanelWidget_ = new PlaylistPanel_Qt(playlistDock_);
    playlistDock_->setWidget(playlistPanelWidget_);
    playlistDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    playlistPanelWidget_->setMinimumWidth(220);
    playlistPanelWidget_->setMinimumHeight(140);
    addDockWidget(Qt::LeftDockWidgetArea, playlistDock_);
    splitDockWidget(fxParamsDock_, playlistDock_, Qt::Vertical);

    // Remote Session — a dockable panel like the others (host/join forms,
    // live status + participants + errors, collapsible chat & connection
    // logs, chat input). Tabified with the LUTs dock on the right so it
    // doesn't crowd the left stack. remoteDialog_ is the panel widget the
    // network pump refreshes; menu actions raise the dock.
    remoteDock_ = new QDockWidget("Remote Session", this);
    remoteDock_->setObjectName("dock.remote");
    remoteDock_->setAccessibleName("Remote session dock");
    remoteDialog_ = new RemoteDialog_Qt(remoteDock_);
    remoteDock_->setWidget(remoteDialog_);
    remoteDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    remoteDialog_->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, remoteDock_);
    if (lutDock_) tabifyDockWidget(lutDock_, remoteDock_);
    remoteDock_->hide();   // hidden until the user opens it from a menu

    // Notes — right side (JEF-39 Task 6). Qt tabifies it with the LUT
    // (and hidden Remote) group rather than honoring a plain vertical split
    // once that group already exists -- same tabbed-group behavior Remote
    // already gets below -- so it's requested explicitly here instead of
    // pretending it will stay split. toggleViewAction()'s raise() (wired
    // below, addDockToggle) brings its tab to front on F7.
    notesDock_ = new QDockWidget("Notes", this);
    notesDock_->setObjectName("dock.notes");
    notesDock_->setAccessibleName("Notes dock");
    notesPanelWidget_ = new NotesPanel_Qt(notesDock_);
    notesDock_->setWidget(notesPanelWidget_);
    notesDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    notesPanelWidget_->setMinimumWidth(220);
    notesPanelWidget_->setMinimumHeight(160);
    addDockWidget(Qt::RightDockWidgetArea, notesDock_);
    if (lutDock_) tabifyDockWidget(lutDock_, notesDock_);

    // Refresh the FX param panel whenever viewport-driven plate edits
    // fire (this also catches active-plate changes — clicking a plate
    // card emits plateStateChanged via PlateManager_Qt's wiring).
    // QueuedConnection for the same reason as the plate-card connect
    // above: keeps the slot off the mouseMove hot path.
    connect(viewport_, &GlViewport_Qt::plateStateChanged,
            fxParamPanelWidget_, &FXParamPanel_Qt::refresh,
            Qt::QueuedConnection);
    // Repaint the viewport when the combined FX panel mutates the active
    // plate's stack (add / remove / reorder / active-toggle / param edit).
    // The idle playback tick skips repaints when nothing's playing, so a
    // stack change otherwise wouldn't show until the next viewport move.
    connect(fxParamPanelWidget_, &FXParamPanel_Qt::viewportRepaintRequested,
            this, [this]() { if (viewport_) viewport_->update(); });

    // JEF-39: same two triggers as the FX panel above -- active-plate switch
    // refreshes the Notes dock's revision/note list, and its own actions
    // (jump/lock/unlock/remove) ask for a repaint the same way FX edits do.
    connect(viewport_, &GlViewport_Qt::plateStateChanged,
            notesPanelWidget_, &NotesPanel_Qt::refresh,
            Qt::QueuedConnection);
    connect(notesPanelWidget_, &NotesPanel_Qt::viewportRepaintRequested,
            this, [this]() { if (viewport_) viewport_->update(); });

    // JEF-17: populate the consolidated Panels menu now that the docks exist.
    // Each dock contributes its own checkable toggleViewAction() (the checkmark
    // tracks visibility); the FLTK F-keys are assigned here, and "show" also
    // raises the dock to the front of its tab group. This replaces the old
    // find-View-menu-by-title-string hack — panelsMenu_ is a stored pointer.
    if (panelsMenu_) {
        auto addDockToggle = [this](QDockWidget* dock, const QKeySequence& seq,
                                    const QString& objName) {
            if (!dock) return;
            QAction* a = dock->toggleViewAction();
            if (!seq.isEmpty()) {
                a->setShortcut(seq);
                a->setShortcutContext(Qt::ApplicationShortcut);
            }
            a->setObjectName(objName);
            // Bringing a hidden/tabbed dock back should also raise it forward.
            connect(a, &QAction::toggled, this,
                    [dock](bool on) { if (on) dock->raise(); });
            panelsMenu_->addAction(a);
        };
        addDockToggle(plateDock_,    QKeySequence(Qt::Key_F2), "menu.panels.platemanager");
        addDockToggle(timelineDock_, QKeySequence(),           "menu.panels.timeline");
        addDockToggle(fxParamsDock_, QKeySequence(Qt::Key_F3), "menu.panels.fxparams");
        addDockToggle(lutDock_,      QKeySequence(Qt::Key_F4), "menu.panels.lut");
        addDockToggle(playlistDock_, QKeySequence(),           "menu.panels.playlist");
        addDockToggle(notesDock_,    QKeySequence(Qt::Key_F7), "menu.panels.notes");

        panelsMenu_->addSeparator();
        // Remote Session… (F5): the modeless persistent dock (JEF-4). Lazy-
        // created; show()/raise() brings it forward without a new instance.
        panelsMenu_->addAction(tr("Remote Session…"), QKeySequence(Qt::Key_F5),
                               this, [this]() {
            if (remoteDock_) { remoteDock_->show(); remoteDock_->raise(); }
            if (remoteDialog_) remoteDialog_->refreshConnectionState();
        })->setObjectName("menu.panels.remote");
        // Render… (F6): modal RenderDialog_Qt.
        panelsMenu_->addAction(tr("Render…"), QKeySequence(Qt::Key_F6),
                               this, [this]() {
            RenderDialog_Qt dlg(this);
            dlg.exec();
        })->setObjectName("menu.panels.render");
        panelsMenu_->addSeparator();
        panelsMenu_->addAction(tr("Hide Controls"),
                               QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F),
                               this, [this]() { toggleHideControls(); })
            ->setObjectName("menu.panels.hidecontrols");
    }
}

void MainWindow_Qt::restoreLayout() {
    QSettings s;
    bool restored = false;
    if (s.contains(kSettingsGeometry)) {
        restoreGeometry(s.value(kSettingsGeometry).toByteArray());
    }
    if (s.contains(kSettingsState)) {
        restored = restoreState(s.value(kSettingsState).toByteArray());
    }
    if (!restored) {
        // First-launch defaults: shrink the bottom dock strip to its
        // minimum dimensions so the central viewport gets the rest of the
        // window. Without this, Qt distributes vertical space ~half/half.
        // Run after the event loop starts so the docks have real geometry.
        QMetaObject::invokeMethod(this, [this]() {
            resizeDocks({plateDock_, timelineDock_},
                        {plateDock_->minimumWidth(), 9999},
                        Qt::Horizontal);
            resizeDocks({plateDock_}, {plateDock_->minimumHeight()}, Qt::Vertical);
        }, Qt::QueuedConnection);
    }
}

void MainWindow_Qt::doSaveSession(bool forceDialog) {
    QString path = currentSessionPath_;
    if (forceDialog || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("Save Session"),
                   path.isEmpty() ? QString() : path,
                   tr("JefeCheck Session (*.jcs)"));
        if (path.isEmpty()) return;
    }
    if (jefe::qt::saveSession(path.toStdString())) {
        currentSessionPath_ = path;
        updateSessionTitle();
        statusBar()->showMessage(
            tr("Saved session: %1").arg(QFileInfo(path).fileName()), 4000);
    } else {
        QMessageBox::warning(this, tr("Save Session"),
                             tr("Could not save the session."));
    }
}

void MainWindow_Qt::doOpenSession() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Session"),
                             QString(), tr("JefeCheck Session (*.jcs)"));
    if (path.isEmpty()) return;
    openSessionPath(path);
}

void MainWindow_Qt::openSessionPath(const QString& path) {
    if (!viewport_) return;
    viewport_->makeCurrent();          // loadSession uploads preview textures
    const bool ok = jefe::qt::loadSession(path.toStdString());
    if (ok) jefe::qt::startLoadingAllTracks();   // loadSession restores params +
                                                 // a preview but doesn't kick the
                                                 // full decode — do it (= "Load All")
    viewport_->doneCurrent();
    if (ok) {
        currentSessionPath_ = path;
        packageTitle_.clear();
        updateSessionTitle();
        refreshAfterSessionLoad();
        statusBar()->showMessage(
            tr("Opened session: %1").arg(QFileInfo(path).fileName()), 4000);
    } else {
        QMessageBox::warning(this, tr("Open Session"),
                             tr("Could not open the session."));
    }
}

void MainWindow_Qt::refreshAfterSessionLoad() {
    // loadSession sets plate/track params synchronously but the sequences
    // re-decode asynchronously (loader thread → frames arrive over the next
    // ticks). Refresh the not-per-tick widgets now AND again shortly after,
    // so the loaded-state-dependent UI (plate cards, LUT/timeline, viewport)
    // catches up once the async load has progressed. (Status labels + the
    // timeline already refresh every tick.)
    auto refresh = [this]() {
        if (plateManagerWidget_)   plateManagerWidget_->refreshAllCards();
        if (lutPanelWidget_)       lutPanelWidget_->refreshList();
        if (timelinePanelWidget_)  timelinePanelWidget_->refreshFromPlayback();
        // Deliberately inside the deferred lambda: this path decodes
        // asynchronously, so the first call can run before the preview frame
        // exists and find no media. The 250/750ms repeats below are what
        // actually catch it.
        refreshNotesForLoadedMedia();
        if (viewport_)             viewport_->update();
    };
    refresh();
    QTimer::singleShot(250, this, refresh);
    QTimer::singleShot(750, this, refresh);
}

void MainWindow_Qt::rebuildRecentSessionsMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    const auto recents = jefe::qt::getRecentSessions();
    bool any = false;
    for (auto it = recents.rbegin(); it != recents.rend(); ++it) {  // newest first
        const QString p = QString::fromStdString(*it);
        if (!QFileInfo::exists(p)) continue;                        // prune missing
        any = true;
        QAction* a = recentMenu_->addAction(QFileInfo(p).fileName());
        a->setToolTip(p);
        connect(a, &QAction::triggered, this, [this, p]() { openSessionPath(p); });
    }
    if (!any) recentMenu_->addAction(tr("(none)"))->setEnabled(false);
}

void MainWindow_Qt::rebuildRecentPlaylistsMenu() {
    if (!recentPlaylistMenu_) return;
    recentPlaylistMenu_->clear();
    const auto recents = jefe::qt::getRecentPlaylists();
    bool any = false;
    for (auto it = recents.rbegin(); it != recents.rend(); ++it) {  // newest first
        const QString p = QString::fromStdString(*it);
        if (!QFileInfo::exists(p)) continue;                        // prune missing
        any = true;
        QAction* a = recentPlaylistMenu_->addAction(QFileInfo(p).fileName());
        a->setToolTip(p);
        connect(a, &QAction::triggered, this, [this, p]() { openPlaylistPath(p); });
    }
    if (!any) recentPlaylistMenu_->addAction(tr("(none)"))->setEnabled(false);
}

void MainWindow_Qt::openPlaylistPath(const QString& path) {
    // loadPlaylistFile clears + reloads the playlist and pushes the path onto
    // the recent-playlists list (bridge). Refresh the panel and surface it.
    jefe::qt::loadPlaylistFile(path.toStdString());
    if (playlistPanelWidget_) playlistPanelWidget_->refreshList();
    if (playlistDock_) { playlistDock_->show(); playlistDock_->raise(); }
    statusBar()->showMessage(
        tr("Opened playlist: %1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow_Qt::doOpenPlaylist() {
    QSettings s;
    // Reuse the Playlist panel's last-directory key so menu + panel agree.
    const QString seed = s.value("Playlist/lastAddDir").toString();
    const QString chosen = QFileDialog::getOpenFileName(
        this, tr("Open Playlist"), seed,
        tr("JefeCheck playlist (*.jpl);;All files (*)"));
    if (chosen.isEmpty()) return;
    s.setValue("Playlist/lastAddDir", QFileInfo(chosen).absolutePath());
    openPlaylistPath(chosen);
}

int MainWindow_Qt::runHeadlessVideoTest(const QString& dir) {
    if (!viewport_) return 0;
    const int from = jefe::qt::getFromFrame();
    const int to   = jefe::qt::getToFrame();
    const QString tmp = QDir(QDir::tempPath()).filePath("jefecheck_vidtest_frames");
    QDir(tmp).removeRecursively();
    QDir().mkpath(tmp);

    viewport_->makeCurrent();
    for (int f = from; f <= to; ++f) {
        jefe::qt::RenderParams p;
        p.quadrant = 0; p.format = 5; p.formatString = "png";
        p.from = f; p.to = f; p.padding = 4; p.scale = 1.0f;
        p.path = tmp.toStdString(); p.prefix = "f_";
        jefe::qt::triggerSyncRender(p);
    }
    viewport_->doneCurrent();

    VideoEncoder_Qt enc;
    VideoEncoder_Qt::Params ep;
    ep.framePattern = tmp + "/f_%04d.png";
    ep.startNumber  = from;
    ep.frameCount   = to - from + 1;
    ep.fps          = 24;
    ep.codec        = VideoEncoder_Qt::Codec::H264;
    ep.quality      = 80;
    ep.outFile      = QDir(dir).filePath("videotest.mp4");

    QEventLoop loop;
    int result = 0;
    QObject::connect(&enc, &VideoEncoder_Qt::finished, &loop,
                     [&](bool ok, const QString& msg) {
        result = ok ? 1 : 0;
        printf("VIDEO-TEST: %s%s\n", ok ? "OK " : "FAILED: ",
               ok ? ep.outFile.toLocal8Bit().constData()
                  : msg.toLocal8Bit().constData());
        fflush(stdout);
        loop.quit();
    });
    enc.start(ep);
    loop.exec();
    QDir(tmp).removeRecursively();
    return result;
}

void MainWindow_Qt::toggleHideControls() {
    // Hide/show the menu bar, status bar and all docks so only the viewport
    // remains. The Ctrl+Alt+F QShortcut still fires while hidden, so the menu
    // bar can be brought back.
    controlsHidden_ = !controlsHidden_;
    const bool vis = !controlsHidden_;
    menuBar()->setVisible(vis);
    statusBar()->setVisible(vis);
    const QList<QDockWidget*> docks = findChildren<QDockWidget*>();
    for (QDockWidget* d : docks) d->setVisible(vis);
}

int MainWindow_Qt::runHeadlessRenderTest(const QString& dir) {
    if (!viewport_) return 0;
    // Exercise every output format so the smoke test covers both the
    // 8-bit (JPEG/PNG/TIFF/TGA/BMP) and float/half (EXR) saver paths.
    struct Fmt { int format; const char* ext; };
    static const Fmt kFmts[] = {
        {0, "jpg"}, {1, "exr"}, {2, "tif"},
        {3, "tga"}, {4, "bmp"}, {5, "png"},
    };
    const int frame = jefe::qt::getCurrentFrame();
    int total = 0;
    viewport_->makeCurrent();
    for (const auto& f : kFmts) {
        jefe::qt::RenderParams p;
        p.quadrant     = 0;
        p.format       = f.format;
        p.formatString = f.ext;
        p.from         = frame;
        p.to           = frame;
        p.padding      = 4;
        p.scale        = 1.0f;
        p.path         = dir.toStdString();
        p.prefix       = QString("rendertest_%1_").arg(f.ext).toStdString();
        total += jefe::qt::triggerSyncRender(p);
    }
    // Extra low-quality JPEG so the quality plumbing is observable: this
    // file should be markedly smaller than rendertest_jpg_*.jpg above.
    {
        jefe::qt::RenderParams p;
        p.quadrant     = 0;
        p.format       = 0;      // JPEG
        p.formatString = "jpg";
        p.from         = frame;
        p.to           = frame;
        p.padding      = 4;
        p.scale        = 1.0f;
        p.path         = dir.toStdString();
        p.prefix       = "rendertest_jpglowq_";
        p.jpegQuality  = 5;
        total += jefe::qt::triggerSyncRender(p);
    }
    // Full in/out sequence as a numbered PNG run (seq_0001.png …) so the
    // multi-frame render path is exercised, not just one frame in N formats.
    {
        jefe::qt::RenderParams p;
        p.quadrant     = 0;
        p.format       = 5;      // PNG
        p.formatString = "png";
        p.from         = jefe::qt::getFromFrame();
        p.to           = jefe::qt::getToFrame();
        p.padding      = 4;
        p.scale        = 1.0f;
        p.path         = dir.toStdString();
        p.prefix       = "seq_";
        const int n = jefe::qt::triggerSyncRender(p);
        printf("RENDER-TEST seq: %d frame(s) [%d..%d]\n", n, p.from, p.to);
        fflush(stdout);
        total += n;
    }
    viewport_->doneCurrent();
    return total;
}

int MainWindow_Qt::runHeadlessFXTest(const QString& imagePath) {
    if (!viewport_) {
        printf("FX-TEST FAIL: no viewport\n");
        fflush(stdout);
        return 2;
    }

    // 1. Load the image into plate 0 (this makes the GL context current
    // internally and uploads the texture). Make plate 0 the active plate so
    // addFXToActivePlate() (which targets getActiveQuad()) hits the same
    // plate the renderer reads from.
    loadFileIntoPlate(0, imagePath);
    jefe::qt::setActivePlate(0);

    // A single still only populates each track's *preview* frame, not the
    // numbered sequence frames the renderer reads by default. Flip every
    // plate into showPreview mode so getFrameAndSequence() serves the loaded
    // preview frame (theFrame.loaded == true) — otherwise the render path
    // sees an unloaded frame and writes nothing.
    jefe::qt::setAllPlatesShowPreview(true);

    const QString outDir = QDir::tempPath() + "/jefecheck_fxtest";
    QDir().mkpath(outDir);

    // Render one PNG of plate 0's current frame with the given prefix, via
    // the same triggerSyncRender path the Render dialog and --render-test
    // use. Returns the absolute path of the file written.
    auto renderOne = [&](const char* prefix) -> QString {
        jefe::qt::RenderParams p;
        p.quadrant     = 0;
        p.format       = 5;        // PNG (8-bit FBO)
        p.formatString = "png";
        const int frame = jefe::qt::getCurrentFrame();
        p.from = frame;
        p.to   = frame;
        p.padding = 4;
        p.scale   = 1.0f;
        p.path    = outDir.toStdString();
        p.prefix  = prefix;
        const QString fname =
            QString::fromStdString(jefe::qt::previewRenderFilename(p));
        // triggerSyncRender → renderPlate → gfcPlate::draw issues GL calls,
        // so the viewport context must be current (we're outside paintGL).
        viewport_->makeCurrent();
        jefe::qt::triggerSyncRender(p);
        viewport_->doneCurrent();
        return fname;
    };

    // On-screen capture: grabFramebuffer() forces paintGL() — i.e. the real
    // on-screen draw() path (forRender=false, FXPASS_LAST + startSuperShader),
    // which is DIFFERENT from the forRender FBO-readback path renderOne() uses.
    // Reports mean channel value so a black screen (≈0) is obvious, and any
    // glPrintError spew during the paint pinpoints where GL state breaks.
    auto screenStats = [&](const char* tag) {
        QImage img = viewport_->grabFramebuffer();
        if (img.isNull()) { printf("FX-SCREEN %s: grab null\n", tag); fflush(stdout); return; }
        img = img.convertToFormat(QImage::Format_RGBA8888);
        double sum = 0.0; long long n = 0;
        for (int y = 0; y < img.height(); ++y) {
            const uchar* r = img.constScanLine(y);
            for (int x = 0; x < img.width() * 4; ++x) { sum += r[x]; ++n; }
        }
        printf("FX-SCREEN %s: meanChannel=%.3f (0-255) over %dx%d\n",
               tag, n ? sum / double(n) : 0.0, img.width(), img.height());
        fflush(stdout);
    };

    // 2. Baseline render (empty FX stack; forRender still routes through
    // draw3DrectWithFX as a pass-through).
    const QString beforePath = renderOne("fxtest_before_");
    printf("FX-SCREEN: --- grab BEFORE adding FX ---\n"); fflush(stdout);
    screenStats("before-fx");

    // 3. Add a visually-obvious FX through the SAME bridge call the UI uses.
    // Flip Horizontal is geometric with no params — its default output is an
    // unmistakable mirror of the input.
    const std::vector<std::string> names = jefe::qt::getAvailableFXNames();
    int fxIndex = -1;
    for (int i = 0; i < (int)names.size(); ++i) {
        if (names[i].find("Flip Horizontal") != std::string::npos) {
            fxIndex = i;
            break;
        }
    }
    if (fxIndex < 0) {
        printf("FX-TEST FAIL: 'Flip Horizontal' FX not found among %zu loaded FX\n",
               names.size());
        fflush(stdout);
        return 3;
    }
    jefe::qt::addFXToActivePlate(fxIndex);
    const int stackN = (int)jefe::qt::getFXStackOnPlate(0).size();
    printf("FX-TEST: added FX index %d (\"%s\"); plate 0 stack size now %d\n",
           fxIndex, names[fxIndex].c_str(), stackN);
    fflush(stdout);

    printf("FX-SCREEN: --- grab AFTER adding FX ---\n"); fflush(stdout);
    screenStats("after-fx");

    // 4. Render again with the FX in the stack.
    const QString afterPath = renderOne("fxtest_after_");

    // 5. Compute mean absolute pixel difference between the two PNGs.
    QImage before(beforePath);
    QImage after(afterPath);
    if (before.isNull() || after.isNull()) {
        printf("FX-TEST FAIL: could not read rendered PNGs\n  before=%s (%s)\n  after=%s (%s)\n",
               beforePath.toLocal8Bit().constData(),
               before.isNull() ? "null" : "ok",
               afterPath.toLocal8Bit().constData(),
               after.isNull() ? "null" : "ok");
        fflush(stdout);
        return 4;
    }
    before = before.convertToFormat(QImage::Format_RGBA8888);
    after  = after.convertToFormat(QImage::Format_RGBA8888);

    const int w = std::min(before.width(),  after.width());
    const int h = std::min(before.height(), after.height());
    double sum = 0.0;
    long long count = 0;
    for (int y = 0; y < h; ++y) {
        const uchar* ra = before.constScanLine(y);
        const uchar* rb = after.constScanLine(y);
        for (int x = 0; x < w * 4; ++x) {
            sum += std::abs(int(ra[x]) - int(rb[x]));
            ++count;
        }
    }
    const double meanAbsDiff = count ? (sum / double(count)) : 0.0;

    const bool pass = meanAbsDiff > 1.0;
    printf("FX-TEST %s: meanAbsPixelDiff=%.4f (0-255 scale) over %dx%d\n",
           pass ? "PASS" : "FAIL", meanAbsDiff, w, h);
    printf("  before=%s\n  after =%s\n",
           beforePath.toLocal8Bit().constData(),
           afterPath.toLocal8Bit().constData());
    fflush(stdout);
    return pass ? 0 : 1;
}

int MainWindow_Qt::runHeadlessCCTest(const QString& imagePath) {
    // Proves that rendered frames carry the super-shader colour pipeline
    // (gamma/exposure/BCS + LUT). This would FAIL before the fix that applies
    // the super-shader before the forRender read-back — the FX-stack test
    // can't catch it because FX are baked into the FBO earlier.
    if (!viewport_) { printf("CC-TEST FAIL: no viewport\n"); fflush(stdout); return 2; }

    loadFileIntoPlate(0, imagePath);
    jefe::qt::setActivePlate(0);
    jefe::qt::setAllPlatesShowPreview(true);

    const QString outDir = QDir::tempPath() + "/jefecheck_cctest";
    QDir().mkpath(outDir);

    auto renderOne = [&](const char* prefix, bool bakeCropBars = false) -> QString {
        jefe::qt::RenderParams p;
        p.quadrant = 0;
        p.format = 5;               // PNG (8-bit FBO)
        p.formatString = "png";
        const int frame = jefe::qt::getCurrentFrame();
        p.from = frame; p.to = frame;
        p.padding = 4; p.scale = 1.0f;
        p.path = outDir.toStdString();
        p.prefix = prefix;
        p.bakeCropBars = bakeCropBars;
        const QString fname =
            QString::fromStdString(jefe::qt::previewRenderFilename(p));
        viewport_->makeCurrent();
        jefe::qt::triggerSyncRender(p);
        viewport_->doneCurrent();
        return fname;
    };
    auto loadRGBA = [](const QString& path) {
        return QImage(path).convertToFormat(QImage::Format_RGBA8888);
    };
    // Mean abs per-channel diff between two images over rows [y0,y1).
    auto bandDiff = [](const QImage& a, const QImage& b, double f0, double f1) {
        const int h = std::min(a.height(), b.height());
        const int w = std::min(a.width(),  b.width());
        const int y0 = int(f0 * h), y1 = int(f1 * h);
        double sum = 0; long long n = 0;
        for (int y = y0; y < y1; ++y) {
            const uchar* ra = a.constScanLine(y);
            const uchar* rb = b.constScanLine(y);
            for (int x = 0; x < w * 4; ++x) { sum += std::abs(int(ra[x]) - int(rb[x])); ++n; }
        }
        return n ? sum / double(n) : 0.0;
    };

    // Baseline render (no colour correction).
    const QString beforePath = renderOne("cctest_before_");

    // Apply a strong, unmistakable colour correction through the super-shader
    // path — exactly what was dropped from renders before the fix.
    jefe::qt::adjustPlateExposure(0, 3.0f);
    jefe::qt::adjustPlateGamma(0, 1.5f);
    if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();

    const QString afterPath = renderOne("cctest_after_");

    QImage before(beforePath), after(afterPath);
    if (before.isNull() || after.isNull()) {
        printf("CC-TEST FAIL: could not read rendered PNGs\n  before=%s (%s)\n  after=%s (%s)\n",
               beforePath.toLocal8Bit().constData(), before.isNull() ? "null" : "ok",
               afterPath.toLocal8Bit().constData(),  after.isNull()  ? "null" : "ok");
        fflush(stdout);
        return 4;
    }
    before = before.convertToFormat(QImage::Format_RGBA8888);
    after  = after.convertToFormat(QImage::Format_RGBA8888);
    const int w = std::min(before.width(),  after.width());
    const int h = std::min(before.height(), after.height());
    double sum = 0.0; long long count = 0;
    for (int y = 0; y < h; ++y) {
        const uchar* ra = before.constScanLine(y);
        const uchar* rb = after.constScanLine(y);
        for (int x = 0; x < w * 4; ++x) { sum += std::abs(int(ra[x]) - int(rb[x])); ++count; }
    }
    const double meanAbsDiff = count ? (sum / double(count)) : 0.0;
    const bool ccPass = meanAbsDiff > 1.0;
    printf("CC-TEST %s: meanAbsPixelDiff=%.4f (0-255) over %dx%d — render %s colour correction\n",
           ccPass ? "PASS" : "FAIL", meanAbsDiff, w, h, ccPass ? "reflects" : "IGNORES");
    printf("  before=%s\n  after =%s\n",
           beforePath.toLocal8Bit().constData(), afterPath.toLocal8Bit().constData());
    fflush(stdout);

    // --- Crop-bar bake verification ---------------------------------------
    // Set a wide aspect + crop, then render the same frame with the "Bake
    // aspect/crop bars" option OFF and ON. The letterbox should change the
    // top/bottom bands (black bars) while leaving the centre content alone.
    // aspect = 0.5 → content height = 0.5·width, i.e. a 2:1 letterbox on the
    // square source, giving ~25%-tall black bars top and bottom.
    jefe::qt::setPlateAspect(0, 0.5f);
    jefe::qt::setPlateCrop(0, true);
    if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();
    const QString cropOffPath = renderOne("cctest_cropoff_", /*bake*/ false);
    const QString cropOnPath  = renderOne("cctest_cropon_",  /*bake*/ true);
    const QImage cOff = loadRGBA(cropOffPath), cOn = loadRGBA(cropOnPath);
    bool cropPass = false;
    if (!cOff.isNull() && !cOn.isNull()) {
        const double borderDiff = 0.5 * (bandDiff(cOff, cOn, 0.00, 0.12) +
                                         bandDiff(cOff, cOn, 0.88, 1.00));
        const double centreDiff = bandDiff(cOff, cOn, 0.35, 0.65);
        // Bars must land in the top/bottom letterbox, not the centre.
        cropPass = borderDiff > 5.0 && borderDiff > centreDiff * 3.0;
        printf("CROP-TEST %s: borderDiff=%.4f centreDiff=%.4f (bake-off vs bake-on)\n",
               cropPass ? "PASS" : "FAIL", borderDiff, centreDiff);
        printf("  cropOff=%s\n  cropOn =%s\n",
               cropOffPath.toLocal8Bit().constData(), cropOnPath.toLocal8Bit().constData());
    } else {
        printf("CROP-TEST FAIL: could not read crop PNGs\n");
    }
    fflush(stdout);

    return (ccPass && cropPass) ? 0 : 1;
}

int MainWindow_Qt::runHeadlessFXMultiTest(const QString& imagePath) {
    if (!viewport_) { printf("FX-MULTI FAIL: no viewport\n"); fflush(stdout); return 2; }

    loadFileIntoPlate(0, imagePath);
    loadFileIntoPlate(1, imagePath);
    jefe::qt::setAllPlatesShowPreview(true);
    jefe::qt::setFramingMode(FRAMINGDOUBLE_ID);   // side-by-side: plate 0 = left, plate 1 = right

    auto grab = [&]() -> QImage {
        QImage img = viewport_->grabFramebuffer();
        return img.isNull() ? img : img.convertToFormat(QImage::Format_RGBA8888);
    };
    // Mean per-channel value of a half ("brightness"): ~0 means a black plate.
    auto halfMean = [](const QImage& img, bool leftHalf) -> double {
        if (img.isNull()) return -1.0;
        const int W = img.width(), H = img.height(), midx = W / 2;
        double sum = 0; long long n = 0;
        for (int y = 0; y < H; ++y) {
            const uchar* r = img.constScanLine(y);
            const int x0 = leftHalf ? 0 : midx, x1 = leftHalf ? midx : W;
            for (int x = x0; x < x1; ++x) { sum += (r[x*4]+r[x*4+1]+r[x*4+2])/3.0; ++n; }
        }
        return n ? sum/double(n) : 0.0;
    };
    // Mean abs per-channel diff of a half between two grabs.
    auto halfDiff = [](const QImage& a, const QImage& b, bool leftHalf) -> double {
        if (a.isNull() || b.isNull() || a.size() != b.size()) return -1.0;
        const int W = a.width(), H = a.height(), midx = W / 2;
        double sum = 0; long long n = 0;
        for (int y = 0; y < H; ++y) {
            const uchar* ra = a.constScanLine(y); const uchar* rb = b.constScanLine(y);
            const int x0 = leftHalf ? 0 : midx, x1 = leftHalf ? midx : W;
            for (int x = x0*4; x < x1*4; ++x) { sum += std::abs(int(ra[x])-int(rb[x])); ++n; }
        }
        return n ? sum/double(n) : 0.0;
    };

    const QImage baseline = grab();
    printf("FX-MULTI baseline: leftHalf(plate0)=%.2f  rightHalf(plate1)=%.2f\n",
           halfMean(baseline, true), halfMean(baseline, false)); fflush(stdout);

    // Add an FX to plate 0 (left) ONLY.
    jefe::qt::setActivePlate(0);
    const std::vector<std::string> names = jefe::qt::getAvailableFXNames();
    int fxIndex = -1;
    for (int i = 0; i < (int)names.size(); ++i)
        if (names[i].find("Flip Horizontal") != std::string::npos) { fxIndex = i; break; }
    if (fxIndex < 0) { printf("FX-MULTI FAIL: 'Flip Horizontal' not found\n"); fflush(stdout); return 3; }
    jefe::qt::addFXToActivePlate(fxIndex);

    const QImage afterFx = grab();
    const double rightMean = halfMean(afterFx, false);
    const double leftDiff  = halfDiff(baseline, afterFx, true);   // plate 0: flip → should change
    const double rightDiff = halfDiff(baseline, afterFx, false);  // plate 1: no FX → should NOT change
    printf("FX-MULTI after-fx: leftHalf(plate0)=%.2f  rightHalf(plate1)=%.2f\n",
           halfMean(afterFx, true), rightMean); fflush(stdout);
    printf("FX-MULTI diff vs baseline: leftHalf(flip)=%.3f  rightHalf(sibling)=%.3f\n",
           leftDiff, rightDiff); fflush(stdout);

    // PASS: sibling plate 1 is NOT black (no FBO-leak) AND barely changed,
    // while plate 0 visibly changed (the flip actually applied on screen).
    const bool siblingOk   = rightMean > 1.0 && rightDiff < 1.0;
    const bool fxApplied   = leftDiff   > 1.0;
    const bool pass = siblingOk && fxApplied;
    printf("FX-MULTI %s: siblingNotBlack&Stable=%d  fxAppliedOnScreen=%d\n",
           pass ? "PASS" : "FAIL", (int)siblingOk, (int)fxApplied); fflush(stdout);
    return pass ? 0 : 1;
}

void MainWindow_Qt::updateSessionTitle() {
    if (!currentSessionPath_.isEmpty()) {
        setWindowTitle(QString("JefeCheck — %1").arg(QFileInfo(currentSessionPath_).fileName()));
    } else if (!packageTitle_.isEmpty()) {
        setWindowTitle(QString("JefeCheck — %1").arg(packageTitle_));
    } else {
        setWindowTitle("JefeCheck");
    }
}

void MainWindow_Qt::saveLayout() {
    QSettings s;
    s.setValue(kSettingsGeometry, saveGeometry());
    s.setValue(kSettingsState, saveState());
}

void MainWindow_Qt::closeEvent(QCloseEvent* e) {
    saveLayout();
    // Write the recovery session and mark a clean exit so the next launch can
    // distinguish a crash from a normal close. Persist recent sessions.
    jefe::qt::writeRecoverySession();
    {
        QSettings s;
        s.setValue("Session/cleanExit", true);
        QStringList rs;
        for (const auto& p : jefe::qt::getRecentSessions())
            rs << QString::fromStdString(p);
        s.setValue("Session/recent", rs);
        QStringList rp;                              // JEF-18: recent playlists
        for (const auto& p : jefe::qt::getRecentPlaylists())
            rp << QString::fromStdString(p);
        s.setValue("Playlist/recent", rp);
    }
    QMainWindow::closeEvent(e);
}

void MainWindow_Qt::maybeRestoreSessionAtStartup() {
    if (!viewport_) return;
    if (!jefe::qt::getHasRecoverableSession()) return;
    const int mode = jefe::qt::getStartupSessionBehavior();  // 0 empty,1 reopen,2 ask
    auto doLoad = [this]() {
        viewport_->makeCurrent();
        if (jefe::qt::loadRecoverySession())
            jefe::qt::startLoadingAllTracks();   // kick the full decode (= Load All)
        viewport_->doneCurrent();
        refreshAfterSessionLoad();
    };
    if (mode == 1) { doLoad(); return; }                     // Reopen
    if (mode == 0 && lastExitWasClean_) return;              // Empty + clean → nothing
    // Ask (mode 2), or Empty after an unclean exit → prompt.
    const QString msg = lastExitWasClean_
        ? tr("Reopen your last session?")
        : tr("JefeCheck didn't close normally last time. "
             "Recover the previous session?");
    if (QMessageBox::question(this, tr("Session"), msg) == QMessageBox::Yes)
        doLoad();
}

void MainWindow_Qt::hideControlsForDemo() {
    if (!controlsHidden_) toggleHideControls();
}

void MainWindow_Qt::repaintViewportNow() {
    if (viewport_) viewport_->repaint();
}

bool MainWindow_Qt::stampActiveFrameNotes(const QString& outPath, QString* message) {
    auto say = [&](const QString& m) { if (message) *message = m; };
    if (!viewport_) {
        say(tr("Stamp failed: no viewport"));
        return false;
    }

    const int plate = jefe::qt::getActivePlate();
    jefe::qt::NoteStampResult r;

    // The notes layer is rasterised on the GPU, so the viewport's context must
    // be current -- the same requirement a render has (developer_notes.md §18).
    viewport_->makeCurrent();
    const bool ok = jefe::qt::stampNotesIntoExr(plate < 0 ? 0 : plate,
                                                outPath.toStdString(),
                                                /*writeHeader*/ true,
                                                /*writeLayer*/ true, r);
    viewport_->doneCurrent();

    if (!ok) {
        say(tr("Stamp failed: %1").arg(QString::fromStdString(r.error)));
        return false;
    }
    say(tr("Stamped %1 note(s) from %2 into %3 — %4 marked texels in the notes layer")
            .arg(r.noteCount)
            .arg(QFileInfo(QString::fromStdString(r.sourcePath)).fileName())
            .arg(QFileInfo(outPath).fileName())
            .arg(r.markedTexels));
    return true;
}

bool MainWindow_Qt::exportReviewSummary(const QString& outPath, ReviewSummaryStats* stats, QString* message) {
    auto say = [&](const QString& m) { if (message) *message = m; };
    if (summaryExportInProgress_) {
        say(tr("An export is already running"));
        return false;
    }
    ScopedFlag exportGuard(&summaryExportInProgress_);
    const QString suffix = QFileInfo(outPath).suffix().toLower();
    if (suffix != "txt" && suffix != "csv" && suffix != "pdf") {
        say(tr("Unsupported summary format \"%1\": use .pdf, .txt or .csv").arg(suffix));
        return false;
    }
    const std::vector<jefe::qt::SessionMedia> media = jefe::qt::getSessionMediaSet();
    if (media.empty()) {
        say(tr("Nothing to summarise: the session has no media"));
        return false;
    }
    {
        // Refuse an unwritable destination before any rendering starts.
        QFile probe(outPath + ".partial");
        if (!probe.open(QIODevice::WriteOnly)) {
            say(tr("Cannot write %1").arg(outPath));
            return false;
        }
        probe.close();
        probe.remove();
    }
    const QString title = currentSessionPath_.isEmpty()
                          ? tr("Untitled session")
                          : QFileInfo(currentSessionPath_).completeBaseName();
    gfcReviewSummary::Doc doc = jefe::qt::buildReviewSummary(media, title.toStdString());

    ReviewSummaryStats s;
    s.media = int(doc.media.size());
    s.rounds = gfcReviewSummary::roundCount(doc);
    s.notes = gfcReviewSummary::noteCount(doc);

    if (suffix == "pdf") {
        // Thumbnails, and the session saved to put a playlist item's tracks
        // back, live in a temporary directory that is removed when this block
        // ends -- once the PDF has been written, or has failed.
        QTemporaryDir tempDir(QDir::tempPath() + "/jefecheck_summary_XXXXXX");
        if (!tempDir.isValid()) {
            say(tr("Cannot create a temporary directory for the thumbnails"));
            return false;
        }
        s.tempDir = tempDir.path();
        renderSummaryThumbnails(media, doc, tempDir.path(), &s);
        int pages = 0;
        QString err;
        if (!jefe::qt::writeReviewSummaryPdf(doc, outPath, &pages, &err)) {
            say(err);
            return false;
        }
    } else {
        const std::string contents = (suffix == "txt") ? gfcReviewSummary::toText(doc)
                                                       : gfcReviewSummary::toCsv(doc);
        std::string err;
        if (!gfcReviewSummary::writeFileAtomically(outPath.toStdString(), contents, &err)) {
            say(QString::fromStdString(err));
            return false;
        }
    }

    if (stats) *stats = s;
    say(tr("Summary written: %1 %2 %3 media, %4 rounds, %5 notes")
            .arg(QFileInfo(outPath).fileName(), QString::fromUtf8("\xE2\x80\x94"))
            .arg(s.media).arg(s.rounds).arg(s.notes));
    return true;
}

void MainWindow_Qt::renderSummaryThumbnails(const std::vector<jefe::qt::SessionMedia>& media,
                                            gfcReviewSummary::Doc& doc, const QString& dir,
                                            ReviewSummaryStats* stats) {
    // What the export changes, put back when it is done.
    const int savedFrame = jefe::qt::getCurrentFrame();
    const int savedIn = jefe::qt::getInPoint();
    const int savedOut = jefe::qt::getOutPoint();
    const int savedPlaylistItem = jefe::qt::getSelectedPlaylistItem();
    const bool savedFromPlaylist = jefe::qt::currentContentIsPlaylistItem();
    // Saving or opening a session pushes it onto Recent Sessions; the
    // export's own temporary session must not stay there.
    const std::vector<std::string> savedRecents = jefe::qt::getRecentSessions();
    // Playback would otherwise keep advancing frames (via playbackTimer_,
    // which the excluded-user-input event pump below still services) while
    // notes/frames are swapped out for rendering. Pause for the duration and
    // resume at the end, once everything else is back.
    const bool wasPlaying = jefe::qt::isPlaying();

    // In a live remote session, loads, seeks and play/pause are sent to the
    // peers, and the export must send nothing. So it loads no playlist item
    // (those entries count as thumbfail), starts no track load that announces
    // itself, and mutes notifications around each step that changes shared
    // state. Those steps pump no events, so an inbound message cannot unmute
    // them halfway through.
    const bool remote = jefe::qt::isRemoteConnected();
    auto quietly = [remote](const std::function<void()>& step) {
        if (remote) jefe::qt::setRemoteBroadcastsMuted(true);
        step();
        if (remote) jefe::qt::setRemoteBroadcastsMuted(false);
    };

    if (wasPlaying) quietly([] { jefe::qt::pausePlayback(); });

    // A playlist item replaces every track, so the session is saved first and
    // reopened at the end -- only when an item is going to be loaded.
    bool needsPlaylistItem = false;
    for (const jefe::qt::SessionMedia& m : media) {
        if (m.track < 0 && m.playlistItem >= 0) needsPlaylistItem = true;
    }
    const QString restore = dir + "/restore.jcs";
    const bool saved = needsPlaylistItem && !remote && jefe::qt::saveSession(restore.toStdString());

    // gfcSequence::forceLoad (what a forRender=true frame request falls back
    // to when a frame hasn't decoded yet) only works once the async loader
    // thread has reached that frame at least once -- its load params are
    // recorded then. So the export waits, bounded, draining the GL upload
    // queue: for a track's first frame before using the track, and for each
    // frame it renders.
    //
    // Pumps the Qt event loop between polls (excluding user input, see
    // below) so timers, paints and accessibility keep running instead of
    // stalling the GUI thread for up to the full timeout per wait the way an
    // unyielded shader-compile pass does (see autoloadFXsFromPath()'s comment
    // in SequenceLoadBridge_qt.cpp for the same problem elsewhere).
    // summaryExportInProgress_ (set for the whole of exportReviewSummary)
    // keeps a second export from starting mid-wait; excluding user input
    // events here additionally queues any click/keystroke that arrives
    // during the wait instead of letting it interact with a half-restored
    // session.
    auto waitUntil = [this](const std::function<bool()>& done, int timeoutMs) {
        QElapsedTimer waitTimer;
        waitTimer.start();
        while (!done() && waitTimer.elapsed() < timeoutMs) {
            if (jefe::qt::hasPendingTextureUploads()) {
                viewport_->makeCurrent();
                jefe::qt::uploadPendingTextures();
                viewport_->doneCurrent();
            } else {
                // Nothing to drain yet -- a tiny sleep keeps this from
                // busy-spinning while the loader thread works, short enough
                // to stay well clear of the AX/event-loop stall this whole
                // wait exists to avoid.
                QThread::msleep(2);
            }
            // Exclude user input so a queued click/keystroke can't re-enter
            // export mid-wait; timers, paints and accessibility still run.
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        return done();
    };
    auto waitForTrackFrame = [&](int track) {
        return waitUntil([track] { return jefe::qt::getTrackTimelineState(track).loadedCount > 0; }, 5000);
    };
    // Whether @a frame of @a track can be rendered. A frame the loader has not
    // reached gets the track's load restarted at it, and a wait for it --
    // except where that restart would be announced to remote peers.
    auto frameReady = [&](int track, int frame) {
        if (jefe::qt::isTrackFrameReady(track, frame)) return true;
        bool started = false;
        viewport_->makeCurrent();   // restarting deletes the track's frame textures
        quietly([&] { started = jefe::qt::restartTrackLoadAtFrame(track, frame, !remote); });
        viewport_->doneCurrent();
        return started &&
               waitUntil([track, frame] { return jefe::qt::isTrackFrameReady(track, frame); }, 5000);
    };

    int loadedPlaylistItem = -1;
    for (size_t mi = 0; mi < media.size() && mi < doc.media.size(); ++mi) {
        const jefe::qt::SessionMedia& m = media[mi];
        gfcReviewSummary::Media& entry = doc.media[mi];
        int track = m.track;
        bool available = true;
        if (track < 0 && m.playlistItem >= 0) {
            track = m.playlistTrack;
            if (!saved) {
                // A remote session, or no saved session to put the tracks
                // back from: leave the tracks alone.
                available = false;
            } else if (loadedPlaylistItem != m.playlistItem) {
                // Loads the item's tracks, FX stacks and program state: its
                // reviewed look. Replacing the tracks deletes their textures,
                // so the GL context must be current.
                viewport_->makeCurrent();
                jefe::qt::loadPlaylistItem(m.playlistItem);
                viewport_->doneCurrent();
                loadedPlaylistItem = m.playlistItem;
            }
        }
        const int plate = available ? jefe::qt::plateShowingTrack(track) : -1;
        bool ready = false;
        if (plate >= 0) {
            if (remote) {
                // A track's first load is announced to the peers: use only
                // a track that already has its frame list.
                ready = jefe::qt::getTrackTimelineState(track).numFrames > 0;
            } else {
                viewport_->makeCurrent();   // starting a load clears the track's textures
                ready = jefe::qt::prepareTrackForRender(track);
                viewport_->doneCurrent();
            }
        }
        if (ready) ready = waitForTrackFrame(track);
        const int firstFrame = ready ? jefe::qt::getTrackTimelineState(track).rangeStart : 1;

        for (size_t ri = 0; ri < entry.rounds.size(); ++ri) {
            for (size_t fi = 0; fi < entry.rounds[ri].frames.size(); ++fi) {
                gfcReviewSummary::Frame& f = entry.rounds[ri].frames[fi];
                const int frame = (f.frame == gfcReviewSummary::kAllFrames) ? firstFrame : f.frame;
                // frameReady() may pump events, so point the plate at this
                // round's notes only after it, right before the render.
                if (!ready || !frameReady(track, frame) ||
                    !jefe::qt::setPlateNotesToRound(plate, m.mediaPath, int(ri))) {
                    ++stats->thumbFail;
                    continue;
                }
                jefe::qt::RenderParams p;
                p.quadrant = plate;
                p.format = 5;               // PNG
                p.formatString = "png";
                p.from = p.to = frame;
                p.padding = 4;
                p.scale = 1.0f;
                p.path = dir.toStdString();
                p.prefix = QString("thumb_m%1_r%2_f%3_").arg(mi).arg(ri).arg(fi).toStdString();
                int sw = 0, sh = 0;
                jefe::qt::getRenderSourceSize(plate, sw, sh);
                if (sw > 0 && sh > 0) {
                    p.outWidth = 960;
                    p.outHeight = std::max(1, int(960.0 * sh / sw + 0.5));
                }
                p.burnInNotes = true;
                const QString file = QString::fromStdString(jefe::qt::previewRenderFilename(p));
                int rendered = 0;
                viewport_->makeCurrent();
                quietly([&] { rendered = jefe::qt::triggerSyncRender(p); });   // renders seek the playhead
                viewport_->doneCurrent();
                const QImage thumb(file);
                if (rendered == 1 && !thumb.isNull()) {
                    f.thumbnailPath = file.toStdString();
                    ++stats->thumbs;
                    if (stats->firstThumbnail.isNull()) stats->firstThumbnail = thumb;
                } else {
                    ++stats->thumbFail;
                }
            }
        }
    }

    // Put back what the export changed: the plates' real notes, then -- only
    // if a playlist item replaced the tracks -- the saved session, through the
    // same path as File -> Open Session.
    jefe::qt::syncPlateNotes();
    if (loadedPlaylistItem >= 0) {
        viewport_->makeCurrent();
        if (jefe::qt::loadSession(restore.toStdString())) jefe::qt::startLoadingAllTracks();
        viewport_->doneCurrent();
        refreshAfterSessionLoad();
        // startLoadingAllTracks() is async -- wait for each track that has
        // media to land its first frame so a caller that renders right after
        // this returns (e.g. a burn-in comparison) doesn't see a mid-reload
        // blank plate.
        for (int t = 0; t < 4; ++t) {
            if (!jefe::qt::getTrackParams(t).filename.empty()) waitForTrackFrame(t);
        }
    }
    // A session reopen does not restore the playlist selection, and every
    // track load resets the in/out points, so those go back explicitly, with
    // Recent Sessions, the frame and playback.
    jefe::qt::setRecentSessions(savedRecents);
    quietly([&] {
        jefe::qt::restorePlaylistSelection(savedPlaylistItem, savedFromPlaylist);
        // Out first: setOutPoint pulls a later in point down to it, and the
        // saved in point is never after the saved out point.
        jefe::qt::setOutPoint(savedOut);
        jefe::qt::setInPoint(savedIn);
        jefe::qt::seekToFrame(savedFrame);
        // togglePlayFwd() starts forward playback from the paused state
        // pausePlayback() left at the start.
        if (wasPlaying) jefe::qt::togglePlayFwd();
    });
}

int MainWindow_Qt::runHeadlessSummaryTest(const QString& imagePath) {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        printf("SUMMARY-TEST %s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    auto readBytes = [](const QString& path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    if (!viewport_) { printf("SUMMARY-TEST FAIL no viewport\n"); fflush(stdout); return 2; }

    // A private copy of the image, so the sidecar next to it is this test's alone.
    const QString work = QDir::tempPath() + "/jefecheck_summarytest_" +
                         QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(work);
    const QString media = work + "/" + QFileInfo(imagePath).fileName();
    if (!QFile::copy(imagePath, media)) {
        printf("SUMMARY-TEST FAIL cannot copy %s\n", qPrintable(imagePath));
        fflush(stdout);
        return 2;
    }

    // One locked round with a stroke on frame 1 and a text note on every
    // frame, then an open round with no notes.
    {
        gfcReview review;
        review.mediaPath = gfcNoteStore::normalisePath(media.toStdString());
        gfcRevision& r1 = review.beginRevision("Supervisor");
        auto stroke = std::make_unique<gfcNoteStroke>();
        stroke->author = "Supervisor";
        stroke->quadID = 0;
        stroke->from = 1;
        stroke->to = 1;
        stroke->colorR = 1.0f; stroke->colorG = 0.0f; stroke->colorB = 0.0f;
        stroke->size = 12;
        stroke->pts = { gfcNotePoint{0.1f, 0.1f}, gfcNotePoint{0.9f, 0.9f}, gfcNotePoint{0.1f, 0.9f} };
        r1.addNote(std::move(stroke));
        auto text = std::make_unique<gfcNoteText>();
        text->author = "Supervisor";
        text->quadID = 0;
        text->always = true;
        text->anchor = gfcNotePoint{0.5f, 0.5f};
        text->text = "too warm, \"here\"";
        r1.addNote(std::move(text));
        r1.locked = true;
        review.beginRevision("Artist");
        check(gfcNoteStore::save(review), "fixture sidecar saved");
    }

    loadFileIntoPlate(0, media);
    jefe::qt::setActivePlate(0);

    ReviewSummaryStats stats;
    QString msg;
    const QString txt = work + "/summary.txt";
    check(exportReviewSummary(txt, &stats, &msg), "text summary exported");
    check(stats.media == 1 && stats.rounds == 2 && stats.notes == 2,
          "counts: one media, two rounds, two notes");
    const QString text = QString::fromUtf8(readBytes(txt));
    check(text.contains("== " + QFileInfo(media).fileName() + " =="), "text names the media");
    check(text.contains(QString::fromUtf8("Round 1 \xE2\x80\x94 Supervisor")) && text.contains("locked"),
          "text has the locked Supervisor round");
    check(text.contains(QString::fromUtf8("Round 2 \xE2\x80\x94 Artist")) && text.contains("  No notes"),
          "text keeps the empty Artist round");
    check(text.contains("\"too warm, \"here\"\""), "text quotes the text note");

    const QString csv = work + "/summary.csv";
    check(exportReviewSummary(csv, &stats, &msg), "CSV summary exported");
    const QByteArray csvBytes = readBytes(csv);
    check(csvBytes.startsWith("media,round_id,round_author,"), "CSV starts with the header");
    check(csvBytes.count("\r\n") == 3, "CSV has the header and one row per note");
    check(csvBytes.contains("\"too warm, \"\"here\"\"\""), "CSV quotes the text note");
    check(!QFile::exists(txt + ".partial") && !QFile::exists(csv + ".partial"), "no partial files left");
    check(!exportReviewSummary(work + "/summary.doc", &stats, &msg), "an unknown extension is refused");

    // A second media whose sidecar exists but is not a notes document (valid
    // XML, wrong shape) -- buildReviewSummary() should report it as
    // "Notes unreadable" rather than silently showing "No notes". Track 1,
    // not track 0: the next task's PDF checks assume track 0 is still the
    // first media, so it must stay exactly as the checks above left it.
    const QString media2 = work + "/unreadable_" + QFileInfo(media).fileName();
    if (!QFile::copy(imagePath, media2)) {
        printf("SUMMARY-TEST FAIL cannot copy %s\n", qPrintable(imagePath));
        fflush(stdout);
        return 2;
    }
    const std::string sidecar2 = gfcNoteStore::sidecarPathFor(
        gfcNoteStore::normalisePath(media2.toStdString()));
    {
        QFile f(QString::fromStdString(sidecar2));
        check(f.open(QIODevice::WriteOnly) && f.write("<other/>") > 0,
              "unreadable-notes sidecar written");
    }

    loadFileIntoPlate(1, media2);

    const QString txt2 = work + "/summary2.txt";
    check(exportReviewSummary(txt2, &stats, &msg), "third text summary exported");
    const QString text2 = QString::fromUtf8(readBytes(txt2));
    check(text2.contains("== " + QFileInfo(media2).fileName() + " ==\n  Notes unreadable\n"),
          "unreadable notes are reported for the second media");
    check(text2.contains(QString::fromUtf8("Round 1 \xE2\x80\x94 Supervisor")),
          "first media's round is still in the summary");

    // PDF: thumbnails through the plate pipeline with the round's notes burned
    // in, and what the export touched put back afterwards.

    // Pumps events (excluding user input) and drains texture uploads until
    // done() holds, the way the export itself waits for frames.
    auto pumpUntil = [this](const std::function<bool()>& done, int timeoutMs) {
        QElapsedTimer waited;
        waited.start();
        while (!done() && waited.elapsed() < timeoutMs) {
            if (jefe::qt::hasPendingTextureUploads()) {
                viewport_->makeCurrent();
                jefe::qt::uploadPendingTextures();
                viewport_->doneCurrent();
            } else {
                QThread::msleep(2);
            }
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        return done();
    };
    // A numbered PNG sequence of `count` frames in its own directory; returns frame 1.
    auto writeSequence = [](const QString& dir, const QString& stem, int count, int hue) {
        QDir().mkpath(dir);
        for (int i = 1; i <= count; ++i) {
            QImage img(64, 64, QImage::Format_RGB32);
            img.fill(QColor::fromHsv(hue, 200, 60 + 30 * i));
            img.save(dir + "/" + stem + QString(".%1.png").arg(i, 4, 10, QChar('0')));
        }
        return dir + "/" + stem + ".0001.png";
    };
    // One round by `author` with a stroke on each of `frames`.
    auto saveStrokes = [](const QString& framePath, const char* author, std::initializer_list<int> frames) {
        gfcReview review;
        review.mediaPath = gfcNoteStore::normalisePath(framePath.toStdString());
        gfcRevision& r = review.beginRevision(author);
        for (int frame : frames) {
            auto stroke = std::make_unique<gfcNoteStroke>();
            stroke->author = author;
            stroke->quadID = 0;
            stroke->from = frame;
            stroke->to = frame;
            stroke->colorR = 0.0f; stroke->colorG = 1.0f; stroke->colorB = 0.0f;
            stroke->size = 8;
            stroke->pts = { gfcNotePoint{0.2f, 0.2f}, gfcNotePoint{0.8f, 0.8f} };
            r.addNote(std::move(stroke));
        }
        return gfcNoteStore::save(review);
    };

    // A six-frame sequence on track 2, with notes on frames 2 and 5.
    const QString seqFirst = writeSequence(work + "/seq", "shot", 6, 210);
    check(saveStrokes(seqFirst, "Lead", {2, 5}), "sequence sidecar saved");
    loadFileIntoPlate(2, seqFirst);
    check(pumpUntil([] { return jefe::qt::getTrackTimelineState(2).loadedCount >= 6; }, 5000),
          "the sequence loads all six frames");

    // The user's playlist state: the current tracks kept as playlist item 0
    // and loaded from it, so item 0 is selected and armed for auto-advance.
    jefe::qt::addCurrentAsPlaylistItem();
    viewport_->makeCurrent();
    jefe::qt::loadPlaylistItem(0);
    viewport_->doneCurrent();
    check(pumpUntil([] {
              return jefe::qt::getTrackTimelineState(0).loadedCount >= 1 &&
                     jefe::qt::getTrackTimelineState(2).loadedCount >= 6;
          }, 5000),
          "the tracks reload from playlist item 0");

    // As if the sequence's loader had been started at frame 4 (Alt+click on
    // the timeline): frames 1-3 have never been reached, so a forced render of
    // frame 2 has no load parameters to decode it with.
    viewport_->makeCurrent();
    const bool restarted = jefe::qt::restartTrackLoadAtFrame(2, 4, true);
    viewport_->doneCurrent();
    check(restarted, "the sequence's load restarts at frame 4");
    check(pumpUntil([] { return jefe::qt::isTrackFrameReady(2, 6); }, 5000) &&
          jefe::qt::isTrackFrameReady(2, 5) && !jefe::qt::isTrackFrameReady(2, 2),
          "fixture: frame 5 is loaded, frame 2 has not been reached");

    // What the export must put back: a frame other than the first, an in/out
    // range inside the sequence, the playlist selection, Recent Sessions and
    // the tracks.
    auto setUserState = []() {
        jefe::qt::setOutPoint(5);
        jefe::qt::setInPoint(2);
        jefe::qt::seekToFrame(3);
    };
    jefe::qt::setRecentSessions({ (work + "/earlier.jcs").toStdString() });
    const std::vector<std::string> beforeRecents = jefe::qt::getRecentSessions();
    const std::string beforeFiles[3] = { jefe::qt::getTrackParams(0).filename,
                                         jefe::qt::getTrackParams(1).filename,
                                         jefe::qt::getTrackParams(2).filename };
    auto checkRestored = [&](const std::string& when, const ReviewSummaryStats& st) {
        printf("SUMMARY-TEST %s state: frame=%d in=%d out=%d item=%d fromPlaylist=%d recents=%zu tempDirExists=%d\n",
               when.c_str(), jefe::qt::getCurrentFrame(), jefe::qt::getInPoint(), jefe::qt::getOutPoint(),
               jefe::qt::getSelectedPlaylistItem(), jefe::qt::currentContentIsPlaylistItem() ? 1 : 0,
               jefe::qt::getRecentSessions().size(), QFileInfo::exists(st.tempDir) ? 1 : 0);
        auto what = [&](const char* s) { return when + ": " + s; };
        check(jefe::qt::getCurrentFrame() == 3, what("the current frame is restored").c_str());
        check(jefe::qt::getInPoint() == 2 && jefe::qt::getOutPoint() == 5,
              what("the in/out points are restored").c_str());
        check(jefe::qt::getSelectedPlaylistItem() == 0 && jefe::qt::currentContentIsPlaylistItem(),
              what("the playlist selection and its auto-advance arming are restored").c_str());
        check(jefe::qt::getRecentSessions() == beforeRecents, what("Recent Sessions is unchanged").c_str());
        check(!st.tempDir.isEmpty() && !QFileInfo::exists(st.tempDir),
              what("the export's temporary directory is removed").c_str());
        bool sameMedia = true;
        for (int t = 0; t < 3; ++t) {
            if (jefe::qt::getTrackParams(t).filename != beforeFiles[t]) sameMedia = false;
        }
        check(sameMedia, what("the tracks' media is restored").c_str());
    };

    const QString pdf = work + "/summary.pdf";

    // Re-entrancy (JEF-39 fix round 2): the export's frame-decode wait pumps
    // the event loop (excluding user input), so schedule a second export to
    // fire from that pump -- the only way it can actually land inside the
    // first export's call stack -- and confirm it's refused rather than
    // running concurrently with the first.
    bool reentrantFired = false;
    bool reentrantResult = true;
    QString reentrantMsg;
    const QString reentrantPdf = work + "/reentrant.pdf";
    QTimer::singleShot(0, this, [&]() {
        reentrantFired = true;
        ReviewSummaryStats reentrantStats;
        reentrantResult = exportReviewSummary(reentrantPdf, &reentrantStats, &reentrantMsg);
    });

    setUserState();
    check(exportReviewSummary(pdf, &stats, &msg), "PDF summary exported");
    printf("SUMMARY-TEST pdf: %s thumbs=%d thumbfail=%d\n", qPrintable(msg), stats.thumbs, stats.thumbFail);
    // Two entries on the image (all frames, frame 1) and frames 2 and 5 of the
    // sequence -- frame 2 only renders once the export has loaded it.
    check(stats.thumbs == 4 && stats.thumbFail == 0,
          "one thumbnail per frame entry, including a frame the loader had not reached; none failed");
    const QByteArray pdfBytes = readBytes(pdf);
    check(pdfBytes.startsWith("%PDF-") && pdfBytes.trimmed().endsWith("%%EOF"), "PDF file is complete");
    check(!QFile::exists(pdf + ".partial"), "no partial PDF left");
    checkRestored("tracks-only PDF", stats);

    if (!reentrantFired) {
        check(false, "re-entrant export timer never fired during the export (test inconclusive -- the wait pump was not exercised)");
    } else {
        check(!reentrantResult, "a re-entrant export while one is running is refused");
        check(reentrantMsg.contains("already running"), "re-entrant export message says already running");
        check(!QFile::exists(reentrantPdf), "the re-entrant export wrote nothing");
    }

    // Burn-in reached the thumbnail: the same frame rendered with the empty
    // round (no notes) must differ from it.
    const QImage withNotes = stats.firstThumbnail;
    check(!withNotes.isNull(), "first thumbnail readable");
    const std::vector<jefe::qt::SessionMedia> set = jefe::qt::getSessionMediaSet();
    if (!withNotes.isNull() && !set.empty()) {
        check(jefe::qt::setPlateNotesToRound(0, set[0].mediaPath, 1), "plate shows the empty round");
        jefe::qt::RenderParams p;
        p.quadrant = 0;
        p.format = 5;
        p.formatString = "png";
        p.from = p.to = jefe::qt::getTrackTimelineState(0).rangeStart;
        p.padding = 4;
        p.scale = 1.0f;
        p.path = work.toStdString();
        p.prefix = "nonotes_";
        p.outWidth = withNotes.width();
        p.outHeight = withNotes.height();
        p.burnInNotes = true;
        const QString file = QString::fromStdString(jefe::qt::previewRenderFilename(p));
        viewport_->makeCurrent();
        jefe::qt::triggerSyncRender(p);
        viewport_->doneCurrent();
        jefe::qt::syncPlateNotes();
        QImage without(file);
        double diff = 0.0;
        if (!without.isNull()) {
            const QImage a = withNotes.convertToFormat(QImage::Format_RGBA8888);
            const QImage b = without.convertToFormat(QImage::Format_RGBA8888);
            const int w = std::min(a.width(), b.width());
            const int h = std::min(a.height(), b.height());
            double sum = 0.0;
            long long n = 0;
            for (int y = 0; y < h; ++y) {
                const uchar* ra = a.constScanLine(y);
                const uchar* rb = b.constScanLine(y);
                for (int x = 0; x < w * 4; ++x) { sum += std::abs(int(ra[x]) - int(rb[x])); ++n; }
            }
            diff = n ? sum / double(n) : 0.0;
        }
        printf("SUMMARY-TEST burn-in mean abs diff: %.3f\n", diff);
        check(!without.isNull() && diff > 0.0, "notes are burned into the thumbnail");
    }

    // Media that is only in a playlist item: a three-frame clip with a note on
    // frame 2. Rendering it loads the item, which replaces the tracks, so the
    // export reopens its temporary session afterwards.
    const QString clipFirst = writeSequence(work + "/playlist", "clip", 3, 30);
    check(saveStrokes(clipFirst, "Client", {2}), "playlist clip sidecar saved");
    jefe::qt::addPlaylistFiles({ clipFirst.toStdString() });
    {
        bool playlistOnly = false;
        for (const jefe::qt::SessionMedia& m : jefe::qt::getSessionMediaSet()) {
            if (m.track < 0 && m.playlistItem == 1) playlistOnly = true;
        }
        check(playlistOnly, "the clip is playlist-only media (item 1, on no track)");
    }
    // Watches track 0 while an export runs: a loaded playlist item shows up
    // as track 0 holding the clip.
    bool sawPlaylistLoad = false;
    QTimer watch;
    watch.setInterval(1);
    connect(&watch, &QTimer::timeout, this, [&]() {
        if (jefe::qt::getTrackParams(0).filename != beforeFiles[0]) sawPlaylistLoad = true;
    });

    setUserState();
    watch.start();
    ReviewSummaryStats playlistStats;
    const bool playlistOk = exportReviewSummary(work + "/summary_playlist.pdf", &playlistStats, &msg);
    watch.stop();
    printf("SUMMARY-TEST playlist pdf: %s thumbs=%d thumbfail=%d\n", qPrintable(msg),
           playlistStats.thumbs, playlistStats.thumbFail);
    check(playlistOk, "PDF summary with playlist-only media exported");
    check(playlistStats.thumbs == 5 && playlistStats.thumbFail == 0,
          "the playlist-only media's frame entry gets a thumbnail too");
    check(sawPlaylistLoad, "the playlist item was loaded to render it");
    checkRestored("playlist PDF", playlistStats);

    // In a live remote session a playlist load would be sent to the peers, so
    // the export loads none: the clip's entry counts as thumbfail and the PDF
    // is still written.
    jefe::qt::RemoteServerParams server;
    server.serverName = "summary-test";
    server.port = 47913;
    server.password = "";
    jefe::qt::connectAsServer(server);
    check(jefe::qt::isRemoteConnected(), "a remote session is live");
    check(pumpUntil([] {
              return jefe::qt::isTrackFrameReady(0, 1) && jefe::qt::isTrackFrameReady(2, 2) &&
                     jefe::qt::isTrackFrameReady(2, 5);
          }, 5000),
          "the reopened tracks have the frames the remote export renders");
    sawPlaylistLoad = false;
    setUserState();
    watch.start();
    ReviewSummaryStats remoteStats;
    const QString remotePdf = work + "/summary_remote.pdf";
    const bool remoteOk = exportReviewSummary(remotePdf, &remoteStats, &msg);
    watch.stop();
    printf("SUMMARY-TEST remote pdf: %s thumbs=%d thumbfail=%d\n", qPrintable(msg),
           remoteStats.thumbs, remoteStats.thumbFail);
    check(remoteOk && readBytes(remotePdf).startsWith("%PDF-"), "remote: the PDF is still written");
    check(remoteStats.thumbs == 4 && remoteStats.thumbFail == 1,
          "remote: the playlist-only entry counts as thumbfail, the tracks' entries render");
    check(!sawPlaylistLoad, "remote: no playlist item is loaded");
    checkRestored("remote PDF", remoteStats);
    jefe::qt::disconnectRemote();

    printf("SUMMARY-TEST: %s\n", failures == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return failures == 0 ? 0 : 2;
}

bool MainWindow_Qt::gatherPackageInput(const QString& outPath, bool includeMedia,
                                       jefe::qt::package::ExportInput& input, QString* message) {
    auto say = [&](const QString& m) { if (message) *message = m; };
    const std::vector<jefe::qt::SessionMedia> media = jefe::qt::getSessionMediaSet();
    if (media.empty()) {
        say(tr("Nothing to package: the session has no media"));
        return false;
    }

    // A QTemporaryDir removes itself (and the session copy it holds) when it
    // goes out of scope, on every return path -- the session bytes only need
    // to pass through disk because saveSession() writes a file, not a string;
    // once read into input.sessionXml below, the copy on disk serves no
    // further purpose.
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        say(tr("Cannot create a temporary directory"));
        return false;
    }
    const QString sessionFile = tempDir.filePath("session.jcs");
    if (!jefe::qt::saveSession(sessionFile.toStdString())) {
        say(tr("Cannot save the session"));
        return false;
    }
    QFile saved(sessionFile);
    if (!saved.open(QIODevice::ReadOnly)) {
        say(tr("Cannot read the saved session"));
        return false;
    }

    input = jefe::qt::package::ExportInput{};
    input.outPath = outPath.toStdString();
    input.includeMedia = includeMedia;
    input.sessionXml = saved.readAll().toStdString();
    input.appVersion = jefe::qt::appVersion();
    input.createdIso = gfcReviewSummary::isoUtc(time(nullptr));

    for (const jefe::qt::SessionMedia& m : media) {
        jefe::qt::package::ExportMedia em;
        em.mediaPath = m.mediaPath;
        em.frames = jefe::qt::listSequenceFrames(m.anyFramePath);
        if (em.frames.empty()) {
            say(tr("Cannot find the frames of %1").arg(QString::fromStdString(m.anyFramePath)));
            return false;
        }
        std::string err;
        gfcMediaFingerprint::Probe probe;
        if (!gfcMediaFingerprint::probe(em.frames.front(), probe, &err)) {
            say(QString::fromStdString(err));
            return false;
        }
        em.width = probe.width;
        em.height = probe.height;
        em.fingerprint = jefe::qt::reviewFingerprint(m.mediaPath);
        if (em.fingerprint.empty()) {
            em.fingerprint = gfcMediaFingerprint::compute(em.frames, &err);
            if (em.fingerprint.empty()) {
                say(QString::fromStdString(err));
                return false;
            }
            // Best effort: an unwritable sidecar keeps the value in memory, and
            // the packaged notes below carry it either way.
            jefe::qt::setReviewFingerprint(m.mediaPath, em.fingerprint);
        }
        em.notesXml = jefe::qt::reviewXmlForMedia(m.mediaPath);
        input.media.push_back(std::move(em));
    }

    std::vector<std::string> lutNames;
    std::string perr;
    if (!gfcSessionPaths::listLutNames(input.sessionXml, lutNames, &perr)) {
        say(QString::fromStdString(perr));
        return false;
    }
    for (const std::string& name : lutNames) {
        const std::string source = jefe::qt::lutSourcePath(name);
        if (!source.empty() && !jefe::qt::isInstallLutPath(source)) input.luts.emplace_back(name, source);
    }
    return true;
}

qint64 MainWindow_Qt::packageMediaBytes() {
    qint64 total = 0;
    for (const jefe::qt::SessionMedia& m : jefe::qt::getSessionMediaSet()) {
        for (const std::string& frame : jefe::qt::listSequenceFrames(m.anyFramePath)) {
            total += QFileInfo(QString::fromStdString(frame)).size();
        }
    }
    return total;
}

bool MainWindow_Qt::exportReviewPackage(const QString& outPath, bool includeMedia, PackageStats* stats, QString* message) {
    auto say = [&](const QString& m) { if (message) *message = m; };
    jefe::qt::package::ExportInput input;
    if (!gatherPackageInput(outPath, includeMedia, input, message)) return false;

    jefe::qt::package::Exporter exporter;
    QString err;
    if (!exporter.begin(input, &err)) {
        say(err);
        return false;
    }
    using State = jefe::qt::package::Exporter::State;
    State state = State::Running;
    while ((state = exporter.step(&err)) == State::Running) {}
    if (state != State::Done) {
        say(err.isEmpty() ? tr("Export failed") : err);
        return false;
    }

    PackageStats s;
    s.media = int(input.media.size());
    s.mediaIncluded = includeMedia;
    s.bytes = QFileInfo(outPath).size();
    if (stats) *stats = s;
    say(tr("Review package written: %1 (%2 media, %3)")
            .arg(QFileInfo(outPath).fileName())
            .arg(s.media)
            .arg(includeMedia ? tr("media included") : tr("media referenced")));
    return true;
}

QString MainWindow_Qt::makePackageFixture(const QString& imagePath, const QString& work) {
    const QString srcDir = work + "/src";
    QDir().mkpath(srcDir);
    const QString media = srcDir + "/" + QFileInfo(imagePath).fileName();
    if (!QFile::copy(imagePath, media)) return QString();
    gfcReview review;
    review.mediaPath = gfcNoteStore::normalisePath(media.toStdString());
    gfcRevision& round = review.beginRevision("Supervisor");
    auto stroke = std::make_unique<gfcNoteStroke>();
    stroke->author = "Supervisor";
    stroke->quadID = 0;
    stroke->from = 1;
    stroke->to = 1;
    stroke->pts = { gfcNotePoint{0.2f, 0.2f}, gfcNotePoint{0.8f, 0.8f} };
    round.addNote(std::move(stroke));
    round.locked = true;
    return gfcNoteStore::save(review) ? media : QString();
}

int MainWindow_Qt::runHeadlessPackageTest(const QString& imagePath) {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        printf("PACKAGE-TEST %s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    auto readBytes = [](const QString& path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    if (!viewport_) { printf("PACKAGE-TEST FAIL no viewport\n"); fflush(stdout); return 2; }

    const QString work = QDir::tempPath() + "/jefecheck_packagetest_" +
                         QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString media = makePackageFixture(imagePath, work);
    if (media.isEmpty()) { printf("PACKAGE-TEST FAIL fixture\n"); fflush(stdout); return 2; }
    loadFileIntoPlate(0, media);
    jefe::qt::setActivePlate(0);
    jefe::qt::adjustPlateExposure(0, 1.5f);
    const std::string mediaKey = gfcNoteStore::normalisePath(media.toStdString());
    const std::string imageName = QFileInfo(media).fileName().toStdString();

    // A LUT outside the install path, assigned to plate 0, so the with-media
    // export below also proves the LUT-packaging path (listLutNames ->
    // lutSourcePath -> isInstallLutPath -> input.luts). A minimal but valid
    // Truelight Cube v2.0 (the only format loadLUT's ".cube" branch reads):
    // a header, a "# width" line giving the cube's edge length, a "# Cube"
    // marker, then edge^3 whitespace-separated RGB triads.
    const QString lutPath = work + "/review_test.cube";
    {
        QFile f(lutPath);
        check(f.open(QIODevice::WriteOnly) &&
              f.write("# Truelight Cube v2.0\n"
                      "# width 2 2 2\n"
                      "# Cube\n"
                      "0.0 0.0 0.0\n"
                      "1.0 0.0 0.0\n"
                      "0.0 1.0 0.0\n"
                      "1.0 1.0 0.0\n"
                      "0.0 0.0 1.0\n"
                      "1.0 0.0 1.0\n"
                      "0.0 1.0 1.0\n"
                      "1.0 1.0 1.0\n") > 0,
              "test LUT written");
    }
    const std::string lutName = QFileInfo(lutPath).fileName().toStdString();
    // gfcLUTManager::loadLUT() calls CubeLUT::create3DTexture(), which needs
    // a current GL context (same requirement stampNotesIntoExr documents).
    viewport_->makeCurrent();
    jefe::qt::loadLUTFile(lutPath.toStdString());
    viewport_->doneCurrent();
    const std::vector<std::string> lutNames = jefe::qt::getLutNames();
    const auto lutPos = std::find(lutNames.begin(), lutNames.end(), lutName);
    check(lutPos != lutNames.end(), "test LUT loaded");
    if (lutPos != lutNames.end()) {
        // applyLUTToPlate() takes the GUI's row index: 0 = "(No LUT)",
        // row r>=1 = lutManager entry r-1 -- so the position found above
        // (a raw lutManager index) needs +1.
        jefe::qt::applyLUTToPlate(0, int(lutPos - lutNames.begin()) + 1);
    }

    PackageStats stats;
    QString msg;
    const QString withMedia = work + "/with.jcreview";
    check(exportReviewPackage(withMedia, true, &stats, &msg), "package with media exported");
    printf("PACKAGE-TEST export: %s\n", qPrintable(msg));
    check(stats.media == 1 && stats.mediaIncluded && stats.bytes > QFileInfo(media).size(),
          "stats: one media, included, larger than the image");

    gfcTar::Reader reader;
    std::string terr;
    std::string bytes;
    check(reader.open(withMedia.toStdString(), &terr), "the package is a valid archive");
    check(!reader.entries().empty() && reader.entries()[0].name == "manifest.json", "manifest.json is the first entry");
    const gfcTar::Entry* image = reader.find("media/000/" + imageName);
    check(image && reader.readBytes(*image, bytes, &terr) && QByteArray::fromStdString(bytes) == readBytes(media),
          "the image is packaged byte for byte");
    const gfcTar::Entry* session = reader.find("session.jcs");
    check(session && reader.readBytes(*session, bytes, &terr) &&
          bytes.find("filename=\"media/000/" + imageName + "\"") != std::string::npos,
          "the session points at the packaged image");
    gfcReview packagedNotes;
    const gfcTar::Entry* notes = reader.find("notes/000.jnotes");
    check(notes && reader.readBytes(*notes, bytes, &terr) && gfcNoteStore::fromXmlString(bytes, packagedNotes) &&
          packagedNotes.revisions.size() == 1,
          "the notes are packaged");
    jefe::qt::package::Manifest manifest;
    QString merr;
    std::string manifestBytes;
    check(!reader.entries().empty() && reader.readBytes(reader.entries()[0], manifestBytes, &terr) &&
          jefe::qt::package::manifestFromJson(QByteArray::fromStdString(manifestBytes), manifest, &merr) &&
          manifest.mediaIncluded && manifest.media.size() == 1 &&
          manifest.media[0].fingerprint.rfind("fp1:", 0) == 0 &&
          manifest.media[0].frames == std::vector<std::string>{imageName} && manifest.media[0].width > 0,
          "the manifest describes the media");
    gfcReview onDisk;
    check(gfcNoteStore::load(mediaKey, onDisk) && !manifest.media.empty() &&
          onDisk.fingerprint == manifest.media[0].fingerprint,
          "the source sidecar now records the fingerprint");
    const gfcTar::Entry* lut = reader.find("luts/" + lutName);
    check(lut && reader.readBytes(*lut, bytes, &terr) && !bytes.empty(), "the LUT is packaged");
    check(!manifest.luts.empty() && manifest.luts[0].name == lutName &&
          manifest.luts[0].file == "luts/" + lutName,
          "the manifest lists the packaged LUT");

    const QString withoutMedia = work + "/without.jcreview";
    check(exportReviewPackage(withoutMedia, false, &stats, &msg), "package without media exported");
    gfcTar::Reader lean;
    bool anyMedia = false;
    const bool leanOpened = lean.open(withoutMedia.toStdString(), &terr);
    if (leanOpened) {
        for (const gfcTar::Entry& e : lean.entries()) {
            if (e.name.rfind("media/", 0) == 0) anyMedia = true;
        }
    }
    const gfcTar::Entry* leanSession = leanOpened ? lean.find("session.jcs") : nullptr;
    check(leanOpened && !anyMedia && leanSession && lean.readBytes(*leanSession, bytes, &terr) &&
          bytes.find(media.toStdString()) != std::string::npos,
          "the lean package has no media and keeps the absolute path");
    check(!QFile::exists(withMedia + ".partial") && !QFile::exists(withoutMedia + ".partial"), "no partial files left");

    // Round trip: open the package with media.
    const QString before = work + "/before.jcs";
    jefe::qt::saveSession(before.toStdString());
    check(openReviewPackage(withMedia, false, &stats, &msg), "the package with media opens");
    printf("PACKAGE-TEST open: %s\n", qPrintable(msg));
    check(stats.resolved == 1 && stats.missing == 0, "media resolved from the package");
    const QString loaded = QString::fromStdString(jefe::qt::getTrackParams(0).filename);
    check(!stats.extractDir.isEmpty() &&
          QFileInfo(loaded).canonicalFilePath().startsWith(QFileInfo(stats.extractDir).canonicalFilePath()),
          "the track loads media from the extraction directory");
    check(readBytes(loaded) == readBytes(media), "the extracted media is byte-identical");
    gfcReview original;
    gfcReview reopened;
    const bool bothLoaded = gfcNoteStore::load(mediaKey, original) &&
                            gfcNoteStore::load(gfcNoteStore::normalisePath(loaded.toStdString()), reopened);
    original.mediaPath.clear();
    reopened.mediaPath.clear();
    check(bothLoaded && gfcNoteStore::toJsonString(original) == gfcNoteStore::toJsonString(reopened),
          "notes are identical after the round trip");
    const QString after = work + "/after.jcs";
    jefe::qt::saveSession(after.toStdString());
    auto plateAttr = [&readBytes](const QString& jcs, const char* name) {
        const QByteArray xml = readBytes(jcs);
        // A saved .jcs always starts with a UTF-8 BOM (gfcSessionManager
        // writes one); XMLNode::parseString, unlike parseFile, does not skip
        // it on its own -- same 3-byte skip as gfcSessionPaths.cpp's parseRoot.
        const char* text = xml.constData();
        if (xml.size() >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0) text += 3;
        XMLResults results;
        XMLNode top = XMLNode::parseString(text, NULL, &results);
        XMLNode plate = top.getChildNode("root").getChildNode("plates").getChildNode("plate", 0);
        XMLCSTR value = plate.isEmpty() ? nullptr : plate.getAttribute(name);
        return QString(value ? value : "");
    };
    check(!plateAttr(before, "exposure").isEmpty() &&
          plateAttr(before, "exposure") == plateAttr(after, "exposure") &&
          plateAttr(before, "gamma") == plateAttr(after, "gamma") &&
          plateAttr(before, "lut") == plateAttr(after, "lut"),
          "plate colour correction and LUT survive the round trip");
    check(openReviewPackage(withMedia, false, &stats, &msg), "opening the same package again works");

    const QByteArray packageBytes = readBytes(withMedia);
    const QString truncated = work + "/truncated.jcreview";
    {
        QFile t(truncated);
        if (t.open(QIODevice::WriteOnly)) t.write(packageBytes.left(packageBytes.size() / 2));
    }
    const std::string trackBefore = jefe::qt::getTrackParams(0).filename;
    const QString titleBefore = windowTitle();
    const int lutCountBefore = jefe::qt::getLoadedLUTCount();
    check(!openReviewPackage(truncated, false, &stats, &msg) && msg.contains("truncated"),
          "a truncated package is refused");
    check(jefe::qt::getTrackParams(0).filename == trackBefore &&
          windowTitle() == titleBefore &&
          jefe::qt::getLoadedLUTCount() == lutCountBefore,
          "a refused package changes nothing");
    if (!stats.extractDir.isEmpty()) QDir(stats.extractDir).removeRecursively();

    printf("PACKAGE-TEST: %s\n", failures == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return failures == 0 ? 0 : 2;
}

bool MainWindow_Qt::openReviewPackage(const QString& packagePath, bool interactive, PackageStats* stats, QString* message) {
    namespace pkg = jefe::qt::package;
    auto say = [&](const QString& m) { if (message) *message = m; };
    if (!viewport_) {
        say(tr("No viewport"));
        return false;
    }

    pkg::OpenServices services;
    services.loadLut = [this](const std::string& path) {
        viewport_->makeCurrent();   // loading a LUT creates GL textures
        const bool ok = jefe::qt::loadLUTFile(path);
        viewport_->doneCurrent();
        return ok;
    };
    services.reloadReview = [](const std::string& mediaPath) { jefe::qt::reloadReviewFromDisk(mediaPath); };
    services.searchPaths = jefe::qt::getSearchPaths();
    services.searchRecursive = jefe::qt::getSearchPathsRecursive();
    services.interactive = interactive;
    services.locate = [this](const pkg::ManifestMedia& media) {
        const QString name = QString::fromStdString(std::filesystem::path(media.originalPath).filename().string());
        return QFileDialog::getOpenFileName(this, tr("Locate %1").arg(name)).toStdString();
    };
    services.confirmMismatch = [this](const pkg::ManifestMedia&, const std::string& chosen) {
        return QMessageBox::question(
                   this, tr("Media does not match"),
                   tr("%1 does not match the media recorded in the package. Use it anyway?")
                       .arg(QString::fromStdString(chosen))) == QMessageBox::Yes;
    };

    const QString cacheRoot = !packageCacheRoot_.isEmpty()
        ? packageCacheRoot_
        : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/packages";
    QDir().mkpath(cacheRoot);
    pkg::OpenResult result;
    QString err;
    if (!pkg::openPackage(packagePath.toStdString(), cacheRoot.toStdString(), services, result, &err)) {
        say(err);
        return false;
    }

    viewport_->makeCurrent();   // loadSession uploads preview textures
    const bool loaded = jefe::qt::loadSession(result.sessionPath);
    if (loaded) jefe::qt::startLoadingAllTracks();
    viewport_->doneCurrent();
    if (!loaded) {
        // By this point the package's LUTs are loaded and its notes are
        // merged into the local sidecars (openPackage already did both) --
        // only the session itself failed to load, so say so instead of
        // implying nothing happened.
        say(tr("The package's notes and LUTs were applied, but its session could not be loaded: %1")
                .arg(QString::fromStdString(result.sessionPath)));
        return false;
    }

    // The extracted session is not a file the user chose: Save Session asks where.
    currentSessionPath_.clear();
    packageTitle_ = QFileInfo(packagePath).fileName();
    updateSessionTitle();
    refreshAfterSessionLoad();

    PackageStats s;
    s.media = int(result.manifest.media.size());
    s.mediaIncluded = result.manifest.mediaIncluded;
    s.bytes = QFileInfo(packagePath).size();
    s.resolved = result.resolved;
    s.missing = result.missing;
    s.extractDir = QString::fromStdString(result.extractDir);
    for (const std::string& name : result.missingMedia) s.missingMedia << QString::fromStdString(name);
    for (const std::string& fx : result.fxNames) {
        if (!jefe::qt::isFxLoaded(fx)) s.missingFx << QString::fromStdString(fx);
    }
    for (const std::string& problem : result.notesProblems) s.notesProblems << QString::fromStdString(problem);
    for (const std::string& lut : result.lutsNotLoaded) s.lutsNotLoaded << QString::fromStdString(lut);
    if (stats) *stats = s;
    QString msg = tr("Opened review package %1: %2 of %3 media found")
                      .arg(QFileInfo(packagePath).fileName())
                      .arg(s.resolved)
                      .arg(s.media);
    if (!s.notesProblems.isEmpty() || !s.lutsNotLoaded.isEmpty()) {
        QStringList extras;
        if (!s.notesProblems.isEmpty()) {
            extras << (s.notesProblems.size() == 1 ? tr("1 notes problem")
                                                    : tr("%1 notes problems").arg(s.notesProblems.size()));
        }
        if (!s.lutsNotLoaded.isEmpty()) {
            extras << (s.lutsNotLoaded.size() == 1 ? tr("1 LUT not loaded")
                                                    : tr("%1 LUTs not loaded").arg(s.lutsNotLoaded.size()));
        }
        msg += "; " + extras.join("; ");
    }
    say(msg);
    return true;
}

int MainWindow_Qt::runHeadlessRelinkTest(const QString& imagePath) {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        printf("RELINK-TEST %s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    if (!viewport_) { printf("RELINK-TEST FAIL no viewport\n"); fflush(stdout); return 2; }

    const QString work = QDir::tempPath() + "/jefecheck_relinktest_" +
                         QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString media = makePackageFixture(imagePath, work);
    if (media.isEmpty()) { printf("RELINK-TEST FAIL fixture\n"); fflush(stdout); return 2; }
    loadFileIntoPlate(0, media);
    jefe::qt::setActivePlate(0);

    PackageStats stats;
    QString msg;
    const QString lean = work + "/lean.jcreview";
    check(exportReviewPackage(lean, false, &stats, &msg), "package without media exported");

    const QString movedDir = work + "/moved/deep";
    QDir().mkpath(movedDir);
    const QString moved = movedDir + "/" + QFileInfo(media).fileName();
    check(QFile::rename(media, moved), "the media is moved away from its recorded path");
    // "use search paths" off: the fingerprint relink must not depend on it.
    jefe::qt::setSearchPaths({(work + "/moved").toStdString()}, true, false);

    check(openReviewPackage(lean, false, &stats, &msg), "the package opens");
    printf("RELINK-TEST open: %s resolved=%d missing=%d\n", qPrintable(msg), stats.resolved, stats.missing);
    check(stats.resolved == 1 && stats.missing == 0, "the media is resolved by fingerprint");
    check(QFileInfo(QString::fromStdString(jefe::qt::getTrackParams(0).filename)).canonicalFilePath() ==
          QFileInfo(moved).canonicalFilePath(),
          "the track loads the moved media");
    gfcReview relinked;
    check(gfcNoteStore::load(gfcNoteStore::normalisePath(moved.toStdString()), relinked) &&
          relinked.revisions.size() == 1,
          "the notes follow the media");
    if (!stats.extractDir.isEmpty()) QDir(stats.extractDir).removeRecursively();

    printf("RELINK-TEST: %s\n", failures == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return failures == 0 ? 0 : 2;
}

int MainWindow_Qt::runHeadlessPackageDialogTest(const QString& imagePath) {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        printf("PACKAGE-DIALOG-TEST %s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    const QString work = QDir::tempPath() + "/jefecheck_packagedialogtest_" +
                         QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString media = makePackageFixture(imagePath, work);
    if (media.isEmpty()) { printf("PACKAGE-DIALOG-TEST FAIL fixture\n"); fflush(stdout); return 2; }
    loadFileIntoPlate(0, media);
    jefe::qt::setActivePlate(0);

    check(ReviewPackageDialog_Qt::formatBytes(0) == "0 B" &&
          ReviewPackageDialog_Qt::formatBytes(1536) == "1.5 KB" &&
          ReviewPackageDialog_Qt::formatBytes(5LL * 1024 * 1024 * 1024) == "5.0 GB",
          "sizes are formatted for people");

    ReviewPackageDialog_Qt dialog(
        [this](const QString& out, bool includeMedia, jefe::qt::package::ExportInput& input, QString* message) {
            return gatherPackageInput(out, includeMedia, input, message);
        },
        packageMediaBytes(), this);
    auto waitUntilIdle = [&dialog]() {
        QElapsedTimer clock;
        clock.start();
        while (dialog.isRunning() && clock.elapsed() < 60000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
    };

    const QString out = work + "/dialog.jcreview";
    dialog.setOutputPath(out);
    dialog.setIncludeMedia(true);
    dialog.startExport();
    check(dialog.isRunning(), "the export starts");
    waitUntilIdle();
    printf("PACKAGE-DIALOG-TEST message: %s\n", qPrintable(dialog.lastMessage()));
    check(!dialog.isRunning() && QFileInfo::exists(out) && dialog.progressPercent() == 100,
          "the export finishes with progress at 100%");

    const QString cancelled = work + "/cancelled.jcreview";
    dialog.setOutputPath(cancelled);
    dialog.startExport();
    dialog.cancelExport();
    QCoreApplication::processEvents();
    check(!dialog.isRunning() && !QFileInfo::exists(cancelled) && !QFileInfo::exists(cancelled + ".partial"),
          "cancel leaves neither package nor partial file");

    dialog.setOutputPath(QString());
    dialog.startExport();
    check(!dialog.isRunning() && !dialog.lastMessage().isEmpty(), "an empty path is refused with a message");

    printf("PACKAGE-DIALOG-TEST: %s\n", failures == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return failures == 0 ? 0 : 2;
}

void MainWindow_Qt::setPackageCacheRoot(const QString& dir) {
    packageCacheRoot_ = dir;
}

void MainWindow_Qt::refreshNotesForLoadedMedia() {
    // Two halves, and the bug was having neither. syncPlateNotes() lazily
    // loads the sidecar and hands the notes to the plate, so footage that was
    // annotated last week shows its markup the moment it opens. refresh()
    // updates the dock, which otherwise waits for a viewport-driven
    // plateStateChanged -- i.e. until the user happens to click the image --
    // and until then insists no media is loaded while notes are visibly
    // drawn on it.
    jefe::qt::syncPlateNotes();
    if (notesPanelWidget_) notesPanelWidget_->refresh();
}

void MainWindow_Qt::loadFileIntoPlate(int plateIdx, const QString& path) {
    loadFileIntoPlate(plateIdx, path, 1.0f);
}

void MainWindow_Qt::loadFileIntoPlate(int plateIdx, const QString& path,
                                      float scale) {
    if (!viewport_ || path.isEmpty()) return;
    if (plateIdx < 0 || plateIdx > 3) return;

    QString resolved = path;

    // Folder drop → pick the first image-like file inside (alpha-sorted).
    // gfcSequence::findSequenceFiles will then discover the rest of the
    // numbered sequence from that one file. We accept anything OIIO
    // probably handles plus DPX/EXR explicitly; leave actually-loadable
    // checks to the loader so we don't have to keep this list in sync.
    if (QFileInfo(resolved).isDir()) {
        static const QStringList kImageFilters{
            "*.exr", "*.EXR",
            "*.dpx", "*.DPX",
            "*.png", "*.PNG",
            "*.jpg", "*.JPG", "*.jpeg", "*.JPEG",
            "*.tif", "*.TIF", "*.tiff", "*.TIFF",
            "*.tga", "*.TGA",
            "*.bmp", "*.BMP",
        };
        QDir dir(resolved);
        const QStringList entries =
            dir.entryList(kImageFilters, QDir::Files, QDir::Name);
        if (entries.isEmpty()) {
            statusBar()->showMessage(
                QString("No image files in %1").arg(resolved), 5000);
            return;
        }
        resolved = dir.absoluteFilePath(entries.first());
    }

    const QString name = QFileInfo(resolved).fileName();

    // GL texture uploads happen inside loadPreview, so the viewport's
    // context must be current on the calling thread.
    viewport_->makeCurrent();
    const bool ok =
        jefe::qt::loadFileIntoPlate(resolved.toStdString(), plateIdx,
                                    /*kickOffSequenceLoad=*/true,
                                    scale);
    viewport_->doneCurrent();

    if (!ok) {
        statusBar()->showMessage(
            QString("Load failed: %1").arg(resolved), 5000);
        return;
    }

    viewport_->update();
    // The preview frame (and its dimensions, channels, layers, etc.) is now
    // loaded — refresh the plate cards so widgets that read frame-derived
    // state (e.g. the Aspect control's native ratio) update immediately
    // rather than waiting for the next viewport-driven plateStateChanged.
    if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();
    refreshNotesForLoadedMedia();
    static const char kPlateNames[4] = {'A', 'B', 'C', 'D'};
    if (scale < 0.999f) {
        // Flash a 3-second message so the Shift / Shift+Cmd modifier
        // isn't invisible — without this the user shift-drops and has
        // no idea why their image looks different.
        statusBar()->showMessage(
            QString("%1 loaded into Track %2 at %3% scale")
                .arg(name)
                .arg(QChar(kPlateNames[plateIdx]))
                .arg(int(scale * 100.0f + 0.5f)),
            3000);
    } else {
        statusBar()->showMessage(
            QString("%1 loaded into Track %2")
                .arg(name)
                .arg(QChar(kPlateNames[plateIdx])));
    }
}

void MainWindow_Qt::onFileDropped(const QString& path, float scale) {
    // Active-plate target preserved from the pre-scale behavior — drag
    // always goes to plate 0 today; PR-after-this can extend to "the
    // plate under the drop point" once we factor that out.
    loadFileIntoPlate(0, path, scale);
}

void MainWindow_Qt::openLoadWindow() {
    // Non-modal dialog (setModal(false)) — the user needs to keep
    // working with the main window (layouts, docks, viewport metadata)
    // while sequences are being prepped. show() (not exec()) is required
    // both because of non-modality and because the drop-forwarding
    // signal/slot chain needs the main event loop to keep pumping.
    if (!loadWindowDialog_) {
        loadWindowDialog_ = new LoadWindowDialog_Qt(viewport_, this);
        connect(viewport_, &GlViewport_Qt::fileDroppedWhileLoadWindowOpen,
                this, &MainWindow_Qt::onLoadWindowDropForwarded);
        // When the Load Sequence Manager closes (Load All or cancel), the
        // tracks' preview frames are decoded — refresh the plate cards so
        // frame-derived widget state (Aspect native ratio, layers, range)
        // reflects what was just loaded.
        connect(loadWindowDialog_, &QDialog::finished, this, [this](int) {
            if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();
            refreshNotesForLoadedMedia();
        });
    }
    loadWindowDialog_->show();
    loadWindowDialog_->raise();
    loadWindowDialog_->activateWindow();
}

void MainWindow_Qt::onLoadWindowDropForwarded(int plateIdx,
                                              const QString& path) {
    if (loadWindowDialog_) loadWindowDialog_->setTrackFilename(plateIdx, path);
}

void MainWindow_Qt::startAutoload() {
    if (!viewport_) return;

    // Text renderer init runs once before the LUT/FX autoload — it's
    // cheap (FreeType reads ~170KB into memory; no atlas bake yet) and
    // gating it behind makeCurrent matches the LUT-load contract: any
    // path that may touch GL state runs with the viewport's context
    // current. Without this, gfc_gl_draw calls from gfcPlate (plate
    // label, frame number, AOI corner readouts) silently early-return
    // because GfcTextRenderer::fontLoaded stays false.
    viewport_->makeCurrent();
    jefe::qt::initializeTextRenderer(viewport_->devicePixelRatioF());
    viewport_->doneCurrent();

    const std::string dir = jefe::qt::resolveInstallPath();
    if (dir.empty()) {
        if (startupStatusLabel_) {
            startupStatusLabel_->setText(
                "Startup: No FX/LUT directory found");
        }
        autoloadPhase_ = AutoloadPhase::Done;
        return;
    }
    lutPaths_ = jefe::qt::getInstallLUTPaths(dir);
    fxPaths_ = jefe::qt::getInstallFXPaths(dir);
    autoloadIdx_ = 0;
    autoloadPhase_ = AutoloadPhase::LUTs;
    if (startupStatusLabel_) {
        startupStatusLabel_->setText(
            QStringLiteral("Startup: Loading LUTs (0/%1)…")
                .arg(lutPaths_.size()));
    }
    QTimer::singleShot(0, this, [this]() { autoloadStep(); });
}

void MainWindow_Qt::autoloadStep() {
    if (!viewport_) return;

    auto setStatus = [this](const QString& text) {
        if (startupStatusLabel_) startupStatusLabel_->setText(text);
    };

    // GL context goes current per-step (rather than once around the
    // whole autoload) so the viewport's paintGL still runs cleanly
    // between our slot invocations — the per-step makeCurrent is
    // cheap, the viewport's paintGL re-makes its own context.
    viewport_->makeCurrent();

    if (autoloadPhase_ == AutoloadPhase::LUTs) {
        if (autoloadIdx_ < (int)lutPaths_.size()) {
            jefe::qt::loadOneLUTFile(lutPaths_[autoloadIdx_]);
            ++autoloadIdx_;
            setStatus(QStringLiteral("Startup: Loading LUTs (%1/%2)…")
                          .arg(autoloadIdx_).arg(lutPaths_.size()));
            viewport_->doneCurrent();
            QTimer::singleShot(0, this, [this]() { autoloadStep(); });
            return;
        }
        // LUTs done — refresh visible LUT widgets so the panel and
        // plate-card combos pick up everything loaded so far.
        if (lutPanelWidget_) lutPanelWidget_->refreshList();
        if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();
        autoloadPhase_ = AutoloadPhase::FXs;
        autoloadIdx_ = 0;
        setStatus(QStringLiteral("Startup: Loading FXs (0/%1)…")
                      .arg(fxPaths_.size()));
        viewport_->doneCurrent();
        QTimer::singleShot(0, this, [this]() { autoloadStep(); });
        return;
    }

    if (autoloadPhase_ == AutoloadPhase::FXs) {
        if (autoloadIdx_ < (int)fxPaths_.size()) {
            jefe::qt::loadOneFXFile(fxPaths_[autoloadIdx_]);
            ++autoloadIdx_;
            setStatus(QStringLiteral("Startup: Loading FXs (%1/%2)…")
                          .arg(autoloadIdx_).arg(fxPaths_.size()));
            viewport_->doneCurrent();
            QTimer::singleShot(0, this, [this]() { autoloadStep(); });
            return;
        }
        // FX list fully populated — sortFXs + rebuildFXHashMap once,
        // then health-check counts.
        jefe::qt::finalizeFXLoad();
        viewport_->doneCurrent();
        autoloadPhase_ = AutoloadPhase::Done;

        const int wantFX  = jefe::qt::getExpectedFXCount();
        const int gotFX   = jefe::qt::getLoadedFXCount();
        const int wantLUT = jefe::qt::getExpectedLUTCount();
        const int gotLUT  = jefe::qt::getLoadedLUTCount();
        if (gotFX == wantFX && gotLUT == wantLUT) {
            setStatus(QStringLiteral("Startup: Ready (%1 FX, %2 LUT)")
                          .arg(gotFX).arg(gotLUT));
        } else {
            setStatus(QStringLiteral(
                "Startup: Errors (%1/%2 FX, %3/%4 LUT)")
                .arg(gotFX).arg(wantFX)
                .arg(gotLUT).arg(wantLUT));
        }

        // FX autoload just finished — rebuild the combined FX panel so the
        // "+ Add FX" menu reflects the freshly-loaded effects (the menu is
        // also rebuilt lazily on aboutToShow, but refresh keeps the rest of
        // the panel in sync with the active plate).
        if (fxParamPanelWidget_) fxParamPanelWidget_->refresh();
        if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();
    }
}
