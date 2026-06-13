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
#include "RenderBridge_qt.h"
#include "RenderDialog_qt.h"
#include "SequenceLoadBridge_qt.h"
#include "TimelinePanel_qt.h"

#include "../UIConstants.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>

namespace {
constexpr const char* kSettingsGeometry = "MainWindow/geometry";
constexpr const char* kSettingsState    = "MainWindow/state";
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

    // Bit depth combo — selects the texture format used for new loads.
    // Persists in QSettings; existing plates keep their depth until
    // reloaded. Routes through the SequenceLoadBridge accessors so
    // we don't pull gfcStructures.h (which drags glad) into this TU.
    depthCombo_ = new QComboBox(this);
    depthCombo_->setObjectName("statusbar.depth.combo");
    depthCombo_->setAccessibleName("Default bit depth for new loads");
    depthCombo_->setToolTip(tr(
        "Bit depth used when loading new sequences. Existing plates "
        "keep their current depth until reloaded."));
    // Pairs are <display label, GFC_*BPC enum value>. GFC_4BPC is a
    // historical misnomer in UIConstants.h — actually 4 bytes per
    // component = 32-bit float. We label it "32-float" and silently
    // use the misnamed enum. GFC_S3TCDX1 is intentionally omitted
    // (storage optimization, not a quality choice).
    depthCombo_->addItem("8",        QVariant::fromValue<int>(GFC_8BPC));
    depthCombo_->addItem("16",       QVariant::fromValue<int>(GFC_16BPC));
    depthCombo_->addItem("16-half",  QVariant::fromValue<int>(GFC_16HALF));
    depthCombo_->addItem("32-float", QVariant::fromValue<int>(GFC_4BPC));
    {
        QSettings settings;
        const int saved = settings.value("Engine/defaultTextureFormat",
                                         GFC_16HALF).toInt();
        // Fallback: if the persisted value isn't one of our combo
        // entries (e.g. a future enum churn dropped the value the user
        // saved), fall back to the GFC_16HALF item rather than coupling
        // to its addItem position. findData on the default returns the
        // item's index since GFC_16HALF is in the combo.
        int idx = depthCombo_->findData(QVariant::fromValue<int>(saved));
        if (idx < 0) {
            idx = depthCombo_->findData(QVariant::fromValue<int>(GFC_16HALF));
        }
        depthCombo_->setCurrentIndex(idx);
        jefe::qt::setDefaultTextureFormat(
            depthCombo_->currentData().toInt());
    }
    // Restore the OIIO loader's decode filter alongside defaultTextureFormat.
    // The Preferences → Engine combo writes here too; mirroring the restore
    // keeps the field consistent across launches and persists the user's
    // last selection without round-tripping through the FLTK XML. Routed
    // through SequenceLoadBridge so this TU can stay glad-free (same reason
    // defaultTextureFormat goes through the bridge above).
    {
        QSettings settings;
        jefe::qt::setDefaultDecodeFilter(
            settings.value("Engine/defaultDecodeFilter",
                           FILTERLANCZOS_ID).toInt());
    }
    connect(depthCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        const int v = depthCombo_->currentData().toInt();
        jefe::qt::setDefaultTextureFormat(v);
        QSettings settings;
        settings.setValue("Engine/defaultTextureFormat", v);
    });
    statusBar()->addPermanentWidget(depthCombo_);

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

    // ~60Hz tick that drives playbackManager's timestep + frame advance.
    // playbackManager.update() is cheap when nothing's playing, so a
    // steady tick is fine. The interval is the upper bound on playback
    // jitter; the tight FPS targeting happens inside the manager.
    playbackTimer_ = new QTimer(this);
    playbackTimer_->setInterval(16);
    connect(playbackTimer_, &QTimer::timeout, this, [this]() {
        // tickPlayback() drains a frame from each sequence's queue and
        // uploads it via glTexImage2D, so the GL context must be current.
        if (!viewport_) return;
        viewport_->makeCurrent();
        const bool dirty = jefe::qt::tickPlayback();
        viewport_->doneCurrent();
        if (dirty) {
            viewport_->update();
        }
        // Pull playback state into the timeline widgets every tick.
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
    auto* loadAction = fileMenu->addAction("&Load Sequence…",
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

    // File → Render… opens RenderDialog_Qt (PR-39a). Modal exec();
    // synchronous renderPlate freezes the dialog until done — async
    // + a worker thread come in PR-39b. No shortcut wired yet
    // because the FLTK F4 binding would shadow plate-reset / fit on
    // some keyboards.
    fileMenu->addAction("&Render…", this, [this]() {
        RenderDialog_Qt dlg(this);
        dlg.exec();
    })->setObjectName("menu.file.render");

    // File → Remote Session… opens RemoteDialog_Qt (PR-41a). Modal
    // dialog with host/server + join/client form sections, mirroring
    // the FLTK remoteWindow.fl. Chat log + participant list land in
    // PR-41b once gfcNetworkManager exposes a connection-event signal
    // we can subscribe to.
    fileMenu->addAction("Remote &Session…", this, [this]() {
        RemoteDialog_Qt dlg(this);
        dlg.exec();
    })->setObjectName("menu.file.remote");
    fileMenu->addSeparator();
    auto* prefsAction = fileMenu->addAction("&Preferences…",
                        QKeySequence(Qt::CTRL | Qt::Key_P),
                        this, [this]() {
                            // Modal — settings persist on Done via
                            // saveSettings(&sett) inside the dialog.
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
    // Toggle actions for each dock. createDockWidget() exposes a built-in
    // toggleViewAction() that flips visibility and tracks state for us.
    auto rememberDockToggle = [viewMenu](QDockWidget* d) {
        if (!d) return;
        viewMenu->addAction(d->toggleViewAction());
    };
    // Filled in after buildDocks() runs, see below.
    (void)rememberDockToggle;

    auto* helpMenu = mb->addMenu("&Help");
    helpMenu->setObjectName("menu.help");
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
    connect(viewport_, &GlViewport_Qt::plateStateChanged,
            plateManagerWidget_, &PlateManager_Qt::refreshAllCards);

    // The 2x2 minimum (wide + tall) applies only when the dock is on the
    // top or bottom edge. Floating, or docked to a side edge, drops to a
    // single-column minimum so the user can run it as a tall narrow
    // column. Generous chrome budget — Qt's dock framing, grid margins,
    // and inter-card spacing add up to more than back-of-envelope math
    // suggests.
    constexpr int kCardMaxW = 320;
    constexpr int kCardMinH = 84;
    constexpr int kHorizDockMinW = 2 * kCardMaxW + 80;   // 2 cols
    constexpr int kHorizDockMinH = 2 * kCardMinH + 60;   // 2 rows
    constexpr int kSingleColMinW = kCardMaxW + 60;       // 1 col
    constexpr int kSingleColMinH = kCardMinH + 40;       // 1 row

    auto updatePlateMins = [this]() {
        const bool wantsTwoColumns =
            !plateDock_->isFloating() &&
            (dockWidgetArea(plateDock_) == Qt::TopDockWidgetArea ||
             dockWidgetArea(plateDock_) == Qt::BottomDockWidgetArea);
        if (wantsTwoColumns) {
            plateDock_->setMinimumWidth(kHorizDockMinW);
            plateDock_->setMinimumHeight(kHorizDockMinH);
        } else {
            plateDock_->setMinimumWidth(kSingleColMinW);
            plateDock_->setMinimumHeight(kSingleColMinH);
        }
    };
    updatePlateMins();
    connect(plateDock_, &QDockWidget::topLevelChanged,
            plateDock_, [updatePlateMins](bool) { updatePlateMins(); });
    connect(plateDock_, &QDockWidget::dockLocationChanged,
            plateDock_, [updatePlateMins](Qt::DockWidgetArea) { updatePlateMins(); });

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

    // FX Stack and LUTs — right side, stacked as tabs.
    fxDock_ = new QDockWidget("FX Stack", this);
    fxDock_->setObjectName("dock.fxstack");
    fxDock_->setAccessibleName("FX Stack dock");
    fxPanelWidget_ = new FXStackPanel_Qt(fxDock_);
    fxDock_->setWidget(fxPanelWidget_);
    fxDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::RightDockWidgetArea, fxDock_);

    lutDock_ = new QDockWidget("LUTs", this);
    lutDock_->setObjectName("dock.luts");
    lutDock_->setAccessibleName("LUT browser dock");
    lutPanelWidget_ = new LUTPanel_Qt(lutDock_);
    lutDock_->setWidget(lutPanelWidget_);
    lutDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::RightDockWidgetArea, lutDock_);

    // FX Params — read-only snapshot of the active plate's FX stack
    // parameter values. Editing comes in PR-38b. Lives on the left
    // side of the window so it doesn't have to compete with the FX
    // Stack / LUT tab group for vertical real estate, and so the
    // value-text propagates to AX (Mac's AX bridge can elide AXValue
    // for 0-sized labels in tab-overflowed docks).
    fxParamsDock_ = new QDockWidget("FX Params", this);
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
    fxParamPanelWidget_->setMinimumWidth(220);
    fxParamPanelWidget_->setMinimumHeight(240);
    addDockWidget(Qt::LeftDockWidgetArea, fxParamsDock_);

    // Tab FX Stack and LUTs together on the right.
    tabifyDockWidget(fxDock_, lutDock_);
    fxDock_->raise();

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

    // Remote sessions — modal dialog launched from the File menu
    // (mirrors the FLTK `remoteWindow.fl` standalone window). Adding
    // it as a fourth left-side dock destabilized the Mac AX bridge's
    // view of the FX Stack and FX Params children under sweep load,
    // so we kept the dialog model the FLTK side already used.
    // Wired in buildMenuBar.

    // Refresh the FX param panel whenever viewport-driven plate edits
    // fire (this also catches active-plate changes — clicking a plate
    // card emits plateStateChanged via PlateManager_Qt's wiring).
    connect(viewport_, &GlViewport_Qt::plateStateChanged,
            fxParamPanelWidget_, &FXParamPanel_Qt::refresh);
    // Also refresh after FX add/remove via the FX Stack panel — those
    // mutate the stack but don't go through plateStateChanged.
    connect(fxPanelWidget_, &FXStackPanel_Qt::stackChanged,
            fxParamPanelWidget_, &FXParamPanel_Qt::refresh);

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
        found->addAction(fxDock_->toggleViewAction());
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

void MainWindow_Qt::saveLayout() {
    QSettings s;
    s.setValue(kSettingsGeometry, saveGeometry());
    s.setValue(kSettingsState, saveState());
}

void MainWindow_Qt::closeEvent(QCloseEvent* e) {
    saveLayout();
    QMainWindow::closeEvent(e);
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
    if (!loadWindowDialog_) {
        loadWindowDialog_ = new LoadWindowDialog_Qt(viewport_, this);
        connect(viewport_, &GlViewport_Qt::fileDroppedWhileLoadWindowOpen,
                this, &MainWindow_Qt::onLoadWindowDropForwarded);
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

        if (fxPanelWidget_) fxPanelWidget_->refreshLists();
        if (plateManagerWidget_) plateManagerWidget_->refreshAllCards();
    }
}
