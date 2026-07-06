#include "MainWindow_qt.h"

#include "FXLutPanel_qt.h"
#include "FXParamPanel_qt.h"
#include "GlViewport_qt.h"
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
#include "VideoEncoder_qt.h"

#include <QEventLoop>
#include "SequenceLoadBridge_qt.h"
#include "TimelinePanel_qt.h"

#include "../UIConstants.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDesktopServices>
#include <QDockWidget>
#include <QUrl>
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
#include <QElapsedTimer>
#include <QTimer>

namespace {
constexpr const char* kSettingsGeometry = "MainWindow/geometry";
// Bumped to _v2 to discard layouts saved while the timeline briefly forced
// an over-tall bottom dock row (which could collapse/hide the Plate Manager).
// Old "MainWindow/state" is ignored, so first-launch defaults reapply once.
constexpr const char* kSettingsState    = "MainWindow/state_v2";
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
    // Fit-to-viewport: F = active plate, Shift+F = all plates.
    bindPlateAction(QKeySequence(Qt::Key_F),
                    []() { jefe::qt::fitActivePlate(); });
    bindPlateAction(QKeySequence(Qt::SHIFT | Qt::Key_F),
                    []() { jefe::qt::fitAllPlates(); });
    // Mirror flips: H = horizontal (flop), V = vertical (flip).
    bindPlateAction(QKeySequence(Qt::Key_H),
                    []() { jefe::qt::toggleFlopActive(); });
    bindPlateAction(QKeySequence(Qt::SHIFT | Qt::Key_H),
                    []() { jefe::qt::toggleFlopAll(); });
    bindPlateAction(QKeySequence(Qt::Key_V),
                    []() { jefe::qt::toggleFlipActive(); });
    bindPlateAction(QKeySequence(Qt::SHIFT | Qt::Key_V),
                    []() { jefe::qt::toggleFlipAll(); });
    // Text overlay cycle: T = active plate, Alt+T = all plates.
    bindPlateAction(QKeySequence(Qt::Key_T),
                    []() { jefe::qt::toggleTextModeActive(); });
    bindPlateAction(QKeySequence(Qt::ALT | Qt::Key_T),
                    []() { jefe::qt::toggleTextModeAll(); });
    // Plate reset: Ctrl+R clears every per-plate override on the active
    // plate (zoom, pan, rotation, flip/flop, channel masks, color
    // correction); Ctrl+Alt+R does the same across all plates. Shift+R /
    // Shift+Alt+R reset only color correction (gamma, exposure, BCS),
    // mirroring FLTK's MenuCallbacks.cpp shortcuts. The bare `r` key
    // FLTK uses for "toggle red channel" is intentionally NOT promoted
    // — a printable letter at app scope would block typing 'r' into
    // any text input.
    bindPlateAction(QKeySequence(Qt::CTRL | Qt::Key_R),
                    []() { jefe::qt::resetActivePlate(); });
    bindPlateAction(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_R),
                    []() { jefe::qt::resetAllPlates(); });
    bindPlateAction(QKeySequence(Qt::SHIFT | Qt::Key_R),
                    []() { jefe::qt::resetActiveColorCorrection(); });
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
        const QString lastDir = settings.value(
            "MainWindow/lastLoadDir",
            QStandardPaths::writableLocation(
                QStandardPaths::PicturesLocation)).toString();
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
    connect(fileMenu, &QMenu::aboutToShow, this, [this]() { rebuildRecentSessionsMenu(); });

    // File → Render… opens RenderDialog_Qt (PR-39a). Modal exec();
    // synchronous renderPlate freezes the dialog until done — async
    // + a worker thread come in PR-39b. No shortcut wired yet
    // because the FLTK F4 binding would shadow plate-reset / fit on
    // some keyboards.
    fileMenu->addAction("&Render…", this, [this]() {
        RenderDialog_Qt dlg(this);
        dlg.exec();
    })->setObjectName("menu.file.render");

    // File → Remote Session… and Dialogs → Remote Session… share one
    // modeless persistent dialog (Task 6 / JEF-4). Lazy-created on
    // first open; show()/raise() on subsequent opens so the window
    // comes to the front without creating a new instance.
    auto showRemote = [this]() {
        if (remoteDock_) { remoteDock_->show(); remoteDock_->raise(); }
        if (remoteDialog_) remoteDialog_->refreshConnectionState();
    };
    fileMenu->addAction("Remote &Session…", this, showRemote)
        ->setObjectName("menu.file.remote");
    fileMenu->addSeparator();
    auto* prefsAction = fileMenu->addAction("&Preferences…",
                        QKeySequence(Qt::CTRL | Qt::Key_P),
                        this, [this]() {
                            // Modal — settings persist on Done via
                            // jefe::qt::writePreferences() inside the dialog.
                            PreferencesWindow_Qt dlg(this);
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

    // Dialogs menu — F-key access to the panels/dialogs (FLTK F2..F6). The
    // docks are built after buildMenuBar(), so the lambdas reach them at
    // trigger time (by which point buildDocks() has run). "Opening" a dock =
    // show + raise (brings its tab forward in a tab group).
    auto* dialogsMenu = mb->addMenu(tr("&Dialogs"));
    dialogsMenu->setObjectName("menu.dialogs");
    auto raiseDock = [](QDockWidget* d) { if (d) { d->show(); d->raise(); } };
    dialogsMenu->addAction(tr("FX"), QKeySequence(Qt::Key_F3),
                           this, [this, raiseDock]() { raiseDock(fxParamsDock_); })
        ->setObjectName("menu.dialogs.fxparams");
    dialogsMenu->addAction(tr("LUT Manager"), QKeySequence(Qt::Key_F4),
                           this, [this, raiseDock]() { raiseDock(lutDock_); })
        ->setObjectName("menu.dialogs.lut");
    dialogsMenu->addAction(tr("Remote Session…"), QKeySequence(Qt::Key_F5),
                           this, showRemote)
        ->setObjectName("menu.dialogs.remote");
    dialogsMenu->addAction(tr("Render…"), QKeySequence(Qt::Key_F6),
                           this, [this]() { RenderDialog_Qt dlg(this); dlg.exec(); })
        ->setObjectName("menu.dialogs.render");
    dialogsMenu->addSeparator();
    dialogsMenu->addAction(tr("Hide Controls"),
                           QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F),
                           this, [this]() { toggleHideControls(); })
        ->setObjectName("menu.dialogs.hidecontrols");

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
    // On-screen help overlay (FLTK's bare 'h' toggleHelp). Menu item only —
    // bare H is flop in the Qt build.
    helpMenu->addAction(tr("Toggle On-Screen &Help"), this, []() {
        jefe::qt::toggleOnScreenHelp();
    })->setObjectName("menu.help.onscreen");
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

    // Hook each dock's toggle into the View menu now that they exist.
    auto* viewMenu = menuBar()->findChild<QMenu*>(QString(), Qt::FindDirectChildrenOnly);
    // Find the View menu by title (findChild by name doesn't work — menus
    // don't have meaningful object names by default).
    QMenu* found = nullptr;
    for (QAction* a : menuBar()->actions()) {
        if (a->menu() && a->text().contains("View")) {
            found = a->menu();
            break;
        }
    }
    (void)viewMenu;
    if (found) {
        found->addAction(plateDock_->toggleViewAction());
        found->addAction(timelineDock_->toggleViewAction());
        found->addAction(fxParamsDock_->toggleViewAction());
        found->addAction(playlistDock_->toggleViewAction());
        found->addAction(lutDock_->toggleViewAction());
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
    if (currentSessionPath_.isEmpty()) setWindowTitle("JefeCheck");
    else setWindowTitle(QString("JefeCheck — %1")
                            .arg(QFileInfo(currentSessionPath_).fileName()));
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
