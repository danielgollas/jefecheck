// Minimal Qt entry point for the USE_QT build. This is intentionally tiny —
// just enough to prove the toolchain end-to-end (QApplication + a main window
// + GlViewport_Qt + IApplication_Qt). The full feature port lives in later
// phases (2E onward).
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QSettings>
#include <QStringList>
#include <QSurfaceFormat>
#include <QDateTime>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QTimer>

#include <cstdlib>
#include <cstring>
#include <string>

#include <QProcess>

#include "gfcStructures.h"
#include "gfcnote.h"
#include "gfcNoteStore.h"
#include "gfcNoteOverlay.h"
#include "gfcNoteStamp.h"
#include "qt/iapplication_qt.h"
#include "qt/ieventsystem_qt.h"
#include "qt/MainWindow_qt.h"
#include "qt/SequenceLoadBridge_qt.h"

extern gfcSettings sett;

static void applyDarkTheme(QApplication& qapp) {
    // Look for the theme next to the binary (`./theme/jefecheck_dark.qss`)
    // and as a fallback in the source tree (so a dev run from build/ also
    // gets styled). Silent miss if neither exists — the skeleton is still
    // usable without the theme.
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/theme/jefecheck_dark.qss",
        QCoreApplication::applicationDirPath() + "/../src/qt/theme/jefecheck_dark.qss",
    };
    for (const QString& path : candidates) {
        QFile qss(path);
        if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qapp.setStyleSheet(QString::fromUtf8(qss.readAll()));
            return;
        }
    }
}

// Resolve --config-dir <path> from argv, falling back to the
// JEFECHECK_CONFIG_DIR env var. UI tests use this to keep QSettings,
// session XML, and LUT autoload path inside a temp dir per test, so
// they never touch the user's real preferences.
static QString resolveConfigDir(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--config-dir") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    const QByteArray env = qgetenv("JEFECHECK_CONFIG_DIR");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    return QString();
}

// Resolve all --open-file <path> occurrences from argv. Each successive
// occurrence loads into the next plate (plate 0, 1, 2, 3). Used by UI
// tests to seed the viewport with a known image before screenshot diffs.
static QStringList resolveOpenFiles(int argc, char* argv[]) {
    QStringList paths;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--open-file") == 0) {
            paths.append(QString::fromLocal8Bit(argv[i + 1]));
            ++i;
        }
    }
    return paths;
}

// Resolve --render-test <dir>. Headless render smoke test: once the
// --open-file footage has loaded, render one frame of plate 0 into <dir>
// in every output format, then exit (0 = frames written, 2 = nothing
// written). Drives the same triggerSyncRender path the Render dialog uses,
// so it verifies the real GL-readback → OIIO-save pipeline without a UI
// session. Use a multi-frame sequence — a single still only populates the
// preview frame, not the sequence frames the renderer reads.
static QString resolveRenderTestDir(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--render-test") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return QString();
}

// Resolve --video-test <dir>. Headless video-export test: render the in/out
// range to a temp PNG sequence and encode an H.264 mp4 into <dir>, then exit
// (0 = ok, 2 = failed). Exercises the full render → FFmpeg pipeline.
static QString resolveVideoTestDir(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--video-test") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return QString();
}

// Resolve --playlist-test <image>. Headless .jpl round-trip smoke test:
// add <image> to the playlist, save to a temp .jpl, clear, reload, and
// verify the entry count survives. Pure data/XML path — no GL needed.
static QString resolvePlaylistTestFile(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--playlist-test") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return QString();
}

// Resolve --fx-test <image>. Headless FX-stack proof: load <image> into
// plate 0, render a baseline PNG, add a visually-obvious shader FX to the
// active plate via the same bridge call the UI uses, render again, and
// compare. Exits 0 if the pixels differ (FX applied), nonzero otherwise.
static QString resolveFXTestFile(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--fx-test") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return QString();
}

// Resolve --cc-test <image>. Headless colour-correction render proof: render a
// baseline, apply exposure+gamma to the plate, render again, and compare.
static QString resolveCCTestFile(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--cc-test") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return QString();
}

// Resolve --fx-multitest <image>. Headless multiplate FX state-leak probe:
// load the image into plates 0 and 1 side-by-side, grab the framebuffer, add
// an FX to plate 0 only, grab again, and report left/right half brightness.
static QString resolveFXMultiTestFile(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--fx-multitest") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return QString();
}

// --aspect-test : headless JEF-21 regression. Pure conversion check (no GL),
// asserts every aspect preset string maps to the renderer's height/width
// convention. Takes no argument.
static bool hasAspectTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--aspect-test") == 0) return true;
    return false;
}

// --notes-test : headless JEF-39 regression. Runs the note model, sidecar
// store, overlay geometry and EXR stamp self-tests (pure data/IO, no GL or
// window needed). Runs all three even if an earlier one fails, so a
// developer sees every broken area at once. Takes no argument.
static bool hasNotesTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--notes-test") == 0) return true;
    return false;
}

// --remote-test : orchestrator/server role (spawns a peer child).
static bool hasRemoteTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--remote-test") == 0) return true;
    return false;
}
// --remote-test-peer <ip> <port> : child/client role.
static bool resolveRemotePeer(int argc, char* argv[], std::string& ip, int& port) {
    for (int i = 1; i + 2 < argc; ++i) {
        if (std::strcmp(argv[i], "--remote-test-peer") == 0 && i + 2 < argc) {
            ip = argv[i + 1]; port = std::atoi(argv[i + 2]); return true;
        }
    }
    return false;
}

int main(int argc, char* argv[]) {
    // Make Qt's accessibility bridge live before QApplication touches
    // anything. On macOS this routes QAccessible → NSAccessibility,
    // which is what Appium's mac2 driver introspects.
    qputenv("QT_ACCESSIBILITY", "1");

    // gfcPlate's renderer relies on the fixed-function GL pipeline:
    // (Sets the global gMacExecutablePath used by getApplicationDataPath
    // on macOS to find the bundled Resources directory. The FLTK build
    // does this in src/main.cpp; mirror it here so LUT/FX autoload from
    // the install path works in the Qt build.)
    if (argc > 0 && argv[0]) {
        setMacExecutablePath(argv[0]);
    }
    // glPipeline notes:
    // glBegin/glEnd quads, GL_TEXTURE_RECTANGLE_ARB, glColor4f, ARB
    // shader objects. macOS only exposes that pipeline through legacy
    // OpenGL 2.1; Qt defaults to Core 3.2/4.1, where every fixed-
    // function call is removed and the polygon ends up rendering an
    // untextured white quad. NoProfile + 2.1 on macOS gives us the
    // same context FLTK uses (Apple's deprecated compatibility GL).
    QSurfaceFormat fmt;
    fmt.setProfile(QSurfaceFormat::NoProfile);
    fmt.setVersion(2, 1);
    fmt.setDepthBufferSize(24);
    // Explicit vsync (1 = sync to display refresh). Qt's default on macOS
    // is platform-dependent; spelling it out keeps swap behavior
    // deterministic across the QOpenGLWidget FBO + window-server
    // composite pipeline. Without this, drag pan feel was inconsistent —
    // some frames hit vsync, others didn't.
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication qapp(argc, argv);
    qapp.setApplicationName("JefeCheck");
    qapp.setOrganizationName("JefeCheck");

    // Test-mode isolation: redirect QSettings + LUT autoload path into a
    // caller-supplied directory. Must run BEFORE any QSettings is
    // constructed (MainWindow_Qt::restoreLayout creates the first one).
    const QString configDir = resolveConfigDir(argc, argv);
    if (!configDir.isEmpty()) {
        QDir().mkpath(configDir);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           configDir);
        // sett is the global gfcSettings; sett.lutPath drives the
        // install-LUT autoload in SequenceLoadBridge_qt. Point it at
        // the test fixture dir if one's been seeded.
        const QString fxDir = configDir + "/FX/";
        if (QFileInfo(fxDir).isDir()) {
            sett.lutPath = fxDir.toStdString();
        }
    }

    static IApplication_Qt application(&qapp);
    static IEventSystem_Qt events;
    jefe::ui::IApplication::setInstance(&application);
    jefe::ui::IEventSystem::setInstance(&events);

    applyDarkTheme(qapp);

    // Headless .jpl round-trip test (--playlist-test <image>): runs before
    // the window is even shown — playlist ops are pure data, no GL.
    const QString playlistTestFile = resolvePlaylistTestFile(argc, argv);
    if (!playlistTestFile.isEmpty()) {
        // GUI stubs must exist before addCurrentAsPlaylistItem() calls
        // getLoadParamsFromGUI() / getPlateStateInfo(). MainWindow_Qt hasn't
        // been constructed yet, so initialize them here (once, safe in
        // headless mode).
        jefe::qt::initializeRenderingChain();
        jefe::qt::clearPlaylist();
        jefe::qt::addPlaylistFile(playlistTestFile.toStdString());
        jefe::qt::addCurrentAsPlaylistItem();   // snapshot path (empty setup OK)
        const int added = int(jefe::qt::getPlaylistItemNames().size());
        const auto detail0 = jefe::qt::getPlaylistItemDetail(0);
        const std::string jpl =
            (QDir::tempPath() + "/jefecheck_playlist_test.jpl").toStdString();
        jefe::qt::savePlaylistFile(jpl);
        jefe::qt::clearPlaylist();
        const int afterClear = int(jefe::qt::getPlaylistItemNames().size());
        jefe::qt::loadPlaylistFile(jpl);
        const int afterLoad = int(jefe::qt::getPlaylistItemNames().size());
        const auto detailAfter = jefe::qt::getPlaylistItemDetail(0);
        const bool detailOk =
            !detail0.empty() && !detailAfter.empty() &&
            detailAfter[0].path == detail0[0].path &&
            detailAfter[0].fromFrame == detail0[0].fromFrame &&
            detailAfter[0].toFrame == detail0[0].toFrame;
        printf("PLAYLIST-TEST: added=%d afterClear=%d afterLoad=%d detailOk=%d file=%s\n",
               added, afterClear, afterLoad, detailOk ? 1 : 0, jpl.c_str());
        fflush(stdout);
        const bool ok = (added == 2 && afterClear == 0 && afterLoad == 2 && detailOk);
        std::_Exit(ok ? 0 : 2);
    }

    // Headless aspect-conversion regression (--aspect-test, JEF-21): pure
    // string->float check, no window or GL needed. Runs and exits.
    if (hasAspectTest(argc, argv)) {
        const bool ok = jefe::qt::runAspectSelfTest();
        std::_Exit(ok ? 0 : 2);
    }

    // Headless note self-tests (--notes-test, JEF-39): model, sidecar store,
    // and overlay geometry are all pure data/arithmetic, no window or GL
    // needed. Runs all three regardless of earlier failures, so a developer
    // sees every broken area in one pass, then exits non-zero if any failed.
    if (hasNotesTest(argc, argv)) {
        const int modelFail   = noteModelSelfTest();
        const int storeFail   = noteStoreSelfTest();
        const int overlayFail = noteOverlaySelfTest();
        const int stampFail   = noteStampSelfTest();
        // The self-tests print via std::printf but do not flush; std::_Exit
        // skips stdio's normal flush-on-exit, so an unflushed buffer (e.g.
        // stdout not a tty) would silently drop all three lines.
        std::fflush(stdout);
        std::_Exit((modelFail == 0 && storeFail == 0 && overlayFail == 0 &&
                    stampFail == 0) ? 0 : 2);
    }

    // --remote-test-peer <ip> <port>: child client role. Connects, holds,
    // exits. Headless; playback state is pure data (no GL needed).
    {
        std::string peerIp; int peerPort = 0;
        if (resolveRemotePeer(argc, argv, peerIp, peerPort)) {
            const auto tInit = std::chrono::steady_clock::now();
            jefe::qt::initializeRenderingChain();
            printf("REMOTE-PEER: initializeRenderingChain took %lldms\n",
                   (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - tInit).count());
            fflush(stdout);
            jefe::qt::remoteTestPeerConnect(peerIp, peerPort, /*holdMs=*/2000, /*play=*/true);
            std::_Exit(0);
        }
    }
    if (hasRemoteTest(argc, argv)) {
        const auto tInit = std::chrono::steady_clock::now();
        jefe::qt::initializeRenderingChain();
        printf("REMOTE-SERVER: initializeRenderingChain took %lldms\n",
               (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - tInit).count());
        fflush(stdout);
        const int port = 60123;
        QProcess peer;
        peer.setProgram(QCoreApplication::applicationFilePath());
        peer.setArguments({"--remote-test-peer", "127.0.0.1", QString::number(port)});
        // Forward the child's output into this process's. It used to be
        // discarded, which made a failing two-process test impossible to
        // diagnose from its log: only the server's half was ever visible.
        peer.setProcessChannelMode(QProcess::ForwardedChannels);
        peer.start();
        if (!peer.waitForStarted(2000)) { printf("REMOTE-TEST: child failed to start: %s\n", peer.errorString().toUtf8().constData()); fflush(stdout); std::_Exit(3); }
        printf("REMOTE-SERVER: peer process started\n");
        fflush(stdout);
        const bool sawPlay = jefe::qt::remoteTestServerSawPlay(port, /*settleMs=*/4000);
        const int  peak    = (int)jefe::qt::remoteParticipants().size();
        peer.waitForFinished(3000);
        if (peer.state() != QProcess::NotRunning) peer.kill();
        printf("REMOTE-TEST: participants=%d mirrored_play=%d\n", peak, sawPlay ? 1 : 0);
        fflush(stdout);
        std::_Exit((peak >= 1 && sawPlay) ? 0 : 2);
    }

    MainWindow_Qt window;
    window.setObjectName("MainWindow");
    window.show();

    // Load each --open-file into the matching plate after the event
    // loop has spun up the GL context. Deferred via QTimer::singleShot
    // so paintGL has fired (initializing GLAD) before the bridge tries
    // to upload a texture.
    const QStringList openFiles = resolveOpenFiles(argc, argv);
    for (int i = 0; i < openFiles.size() && i < 4; ++i) {
        const int plateIdx = i;
        const QString path = openFiles.at(i);
        QTimer::singleShot(0, &window, [&window, plateIdx, path]() {
            window.loadFileIntoPlate(plateIdx, path);
        });
    }

    // --notes-demo: put one of each note type on plate 0 once the footage
    // has decoded, so the annotation overlay can be seen (and screenshotted)
    // without anyone drawing by hand. Coordinates are fixed, so the resulting
    // frame is comparable run to run rather than merely illustrative.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--notes-demo") == 0) {
            QTimer::singleShot(2500, &window, [&window]() {
                jefe::qt::addDemoNotes(0);
                // Real drawing refreshes the dock via the viewport's
                // plateStateChanged on mouse-up; the demo bypasses the mouse,
                // so it has to say so itself or the shot shows notes on the
                // frame and an empty list beside them.
                window.refreshNotesForLoadedMedia();
            });
            break;
        }
    }

    // --stamp-notes <out.exr>: once the footage (and any --notes-demo notes)
    // are in place, stamp the active plate's current frame into <out.exr> and
    // report. With --notes-demo this checks the layer path end to end, which
    // needs the real GL context only a live window provides.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--stamp-notes") == 0) {
            const QString out = QString::fromUtf8(argv[i + 1]);
            QTimer::singleShot(4500, &window, [&window, out]() {
                QString msg;
                const bool ok = window.stampActiveFrameNotes(out, &msg);
                printf("NOTES-STAMP: %s %s\n", ok ? "ok" : "FAIL",
                       msg.toUtf8().constData());
                fflush(stdout);
            });
            break;
        }
    }

    // --window-rect X Y W H: place the window, in logical pixels, so two
    // instances can sit side by side for a recording.
    for (int i = 1; i + 4 < argc; ++i) {
        if (std::strcmp(argv[i], "--window-rect") == 0) {
            const int x = std::atoi(argv[i + 1]);
            const int y = std::atoi(argv[i + 2]);
            const int w = std::atoi(argv[i + 3]);
            const int h = std::atoi(argv[i + 4]);
            QTimer::singleShot(0, &window, [&window, x, y, w, h]() {
                window.setGeometry(x, y, w, h);
            });
            break;
        }
    }

    // --load-all: do what the Load Sequence Manager's Load All button does --
    // leave preview mode and start loading every track -- so a plate filled by
    // --open-file shows its decoded frame, not the preview and its info dump.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--load-all") == 0) {
            QTimer::singleShot(2000, &window, []() {
                jefe::qt::setAllPlatesShowPreview(false);
                jefe::qt::startLoadingAllTracks();
            });
            break;
        }
    }

    // --hide-controls: hide every dock and the text overlay so the picture
    // fills the window, then fit it once the resize and decode have settled.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hide-controls") == 0) {
            QTimer::singleShot(0, &window, [&window]() { window.hideControlsForDemo(); });
            auto tidy = [&window]() {
                jefe::qt::clearTextModeAll();
                jefe::qt::fitAllPlates();
                window.repaintViewportNow();
                printf("DEMO: controls hidden, plates fitted\n");
                fflush(stdout);
            };
            QTimer::singleShot(2500, &window, tidy);
            QTimer::singleShot(8000, &window, tidy);
            break;
        }
    }

    // --demo-host <port> / --demo-join <ip> <port>: start a LAN review
    // session without the Remote dialog. The two sides use different
    // nicknames on purpose -- the server refuses a duplicate. The host then
    // draws a fixed script through the real pencil draw session, so what the
    // joiner receives is ordinary note sync; only the host's mouse input is
    // synthesised.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--demo-host") != 0) continue;
        const int port = std::atoi(argv[i + 1]);
        QTimer::singleShot(3000, &window, [port]() {
            jefe::qt::RemoteServerParams sp;
            sp.serverName = "Supervisor";
            sp.port = port;
            sp.password = "";
            jefe::qt::connectAsServer(sp);
            printf("DEMO: Supervisor hosting on %d\n", port);
            fflush(stdout);
        });

        auto at = [&window](int ms, auto fn) { QTimer::singleShot(ms, &window, fn); };
        auto drawn = [&window]() { window.repaintViewportNow(); };
        int t = 10000;

        // 1. A freehand ellipse around a blob, drawn point by point.
        at(t, []() {
            jefe::qt::demoNoteBegin(0, jefe::qt::NOTETOOL_FREEHAND, 0.40f, 0.34f,
                                    1.0f, 0.25f, 0.20f, 4);
        });
        for (int k = 1; k <= 60; ++k) {
            t += 35;
            const float a = (float)k / 60.0f * 6.2831853f;
            at(t, [a, drawn]() {
                jefe::qt::demoNoteAppend(0.30f + 0.10f * std::cos(a),
                                         0.34f + 0.13f * std::sin(a));
                drawn();
            });
        }
        t += 500;
        at(t, [drawn]() { jefe::qt::noteDrawEnd(); drawn(); printf("DEMO: ellipse sent\n"); fflush(stdout); });

        // 2. An arrow pointing into it.
        t += 1500;
        at(t, []() {
            jefe::qt::demoNoteBegin(0, jefe::qt::NOTETOOL_ARROW, 0.66f, 0.18f,
                                    1.0f, 0.78f, 0.20f, 4);
        });
        for (int k = 1; k <= 24; ++k) {
            t += 35;
            const float u = (float)k / 24.0f;
            at(t, [u, drawn]() {
                jefe::qt::demoNoteAppend(0.66f + (0.44f - 0.66f) * u,
                                         0.18f + (0.32f - 0.18f) * u);
                drawn();
            });
        }
        t += 500;
        at(t, [drawn]() { jefe::qt::noteDrawEnd(); drawn(); printf("DEMO: arrow sent\n"); fflush(stdout); });

        // 3. A box around a second area.
        t += 1500;
        at(t, []() {
            jefe::qt::demoNoteBegin(0, jefe::qt::NOTETOOL_BOX, 0.55f, 0.56f,
                                    0.35f, 0.85f, 1.0f, 3);
        });
        for (int k = 1; k <= 24; ++k) {
            t += 35;
            const float u = (float)k / 24.0f;
            at(t, [u, drawn]() {
                jefe::qt::demoNoteAppend(0.55f + (0.86f - 0.55f) * u,
                                         0.56f + (0.83f - 0.56f) * u);
                drawn();
            });
        }
        t += 500;
        at(t, [drawn]() { jefe::qt::noteDrawEnd(); drawn(); printf("DEMO: box sent\n"); fflush(stdout); });

        // 4. A text note labelling the box.
        t += 1500;
        at(t, [drawn]() {
            jefe::qt::demoNoteBegin(0, jefe::qt::NOTETOOL_TEXT, 0.55f, 0.52f,
                                    0.35f, 0.85f, 1.0f, 3);
            jefe::qt::noteDrawSetText("too warm here");
            jefe::qt::noteDrawEnd();
            drawn();
            printf("DEMO: text sent\n");
            fflush(stdout);
        });
        at(t + 500, []() { printf("DEMO: script done\n"); fflush(stdout); });
        break;
    }
    for (int i = 1; i + 2 < argc; ++i) {
        if (std::strcmp(argv[i], "--demo-join") != 0) continue;
        const std::string ip = argv[i + 1];
        const int port = std::atoi(argv[i + 2]);
        QTimer::singleShot(5000, &window, [ip, port]() {
            jefe::qt::RemoteClientParams cp;
            cp.clientName = "Artist";
            cp.serverIP = ip;
            cp.port = port;
            cp.password = "";
            jefe::qt::connectAsClient(cp);
            printf("DEMO: Artist joining %s:%d\n", ip.c_str(), port);
            fflush(stdout);
        });
        break;
    }

    // --grab-frames <dir> <caption> <startMs> <endMs>: save the window as a
    // captioned JPEG every 66 ms between the two times. Files are named by
    // wall-clock bucket, so two instances can be stitched side by side frame
    // for frame -- and it works when the screen itself cannot be recorded
    // (locked, asleep, no capture permission).
    for (int i = 1; i + 4 < argc; ++i) {
        if (std::strcmp(argv[i], "--grab-frames") != 0) continue;
        const QString dir = QString::fromUtf8(argv[i + 1]);
        const QString caption = QString::fromUtf8(argv[i + 2]);
        const int startMs = std::atoi(argv[i + 3]);
        const int endMs = std::atoi(argv[i + 4]);
        constexpr int kGrabIntervalMs = 66;
        QDir().mkpath(dir);
        auto* grabTimer = new QTimer(&window);
        grabTimer->setInterval(kGrabIntervalMs);
        QObject::connect(grabTimer, &QTimer::timeout, &window, [&window, dir, caption]() {
            QImage frame = window.grab().toImage().convertToFormat(QImage::Format_RGB32);
            QPainter p(&frame);
            const qreal dpr = frame.devicePixelRatio();
            p.scale(1.0 / dpr, 1.0 / dpr);   // draw in physical pixels
            QFont font = p.font();
            font.setPixelSize(int(15 * dpr));
            font.setBold(true);
            p.setFont(font);
            const QRect band(0, 0, frame.width(), int(30 * dpr));
            p.fillRect(band, QColor(0, 0, 0, 170));
            p.setPen(QColor(235, 235, 235));
            p.drawText(band.adjusted(int(12 * dpr), 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, caption);
            p.end();
            const qint64 bucket = QDateTime::currentMSecsSinceEpoch() / kGrabIntervalMs;
            frame.save(dir + "/" + QString::number(bucket) + ".jpg", "JPG", 92);
        });
        QTimer::singleShot(startMs, grabTimer, [grabTimer]() { grabTimer->start(); });
        QTimer::singleShot(endMs, grabTimer, [grabTimer, dir]() {
            grabTimer->stop();
            printf("GRAB: frames written to %s\n", dir.toUtf8().constData());
            fflush(stdout);
        });
        break;
    }

    // --screenshot <path> [delayMs]: grab the window itself, not the screen,
    // so the image is the application and nothing overlapping it.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") != 0) continue;
        const QString path = QString::fromUtf8(argv[i + 1]);
        int delayMs = 4000;
        if (i + 2 < argc && argv[i + 2][0] != '-') delayMs = std::atoi(argv[i + 2]);
        QTimer::singleShot(delayMs, &window, [&window, path]() {
            const QPixmap shot = window.grab();
            printf("SCREENSHOT=%s ok=%d\n", path.toUtf8().constData(),
                   shot.save(path) ? 1 : 0);
            fflush(stdout);
        });
        break;
    }

    // Headless render smoke test (--render-test <dir>): after the footage
    // has had time to decode and the GL context to initialise, render one
    // frame of plate 0 into <dir> and quit.
    const QString renderTestDir = resolveRenderTestDir(argc, argv);
    if (!renderTestDir.isEmpty()) {
        QDir().mkpath(renderTestDir);
        QTimer::singleShot(5000, &window, [&window, renderTestDir]() {
            // The render (GL readback + OIIO save) lives in MainWindow's
            // TU, which can touch the viewport's GL context and the bridge
            // without pulling glad into this Qt entry-point TU.
            const int n = window.runHeadlessRenderTest(renderTestDir);
            printf("RENDER-TEST: wrote %d frame(s) to %s\n",
                   n, renderTestDir.toLocal8Bit().constData());
            fflush(stdout);
            // OIIO has already flushed/closed the output files. Skip Qt's
            // global teardown (it trips a pre-existing trace trap in
            // gfcPlaybackGUI's destructor on macOS) so the harness gets a
            // deterministic exit code.
            std::_Exit(n > 0 ? 0 : 2);
        });
    }

    // Headless FX-stack proof (--fx-test <image>): load the image, render a
    // baseline, add a shader FX, render again, and report whether the output
    // changed. Self-loads the image (does not rely on --open-file).
    const QString fxTestFile = resolveFXTestFile(argc, argv);
    if (!fxTestFile.isEmpty()) {
        QTimer::singleShot(5000, &window, [&window, fxTestFile]() {
            const int code = window.runHeadlessFXTest(fxTestFile);
            fflush(stdout);
            // Skip Qt's global teardown (pre-existing trace trap in a
            // destructor on macOS) so the harness gets a deterministic
            // exit code — same workaround as --render-test.
            std::_Exit(code);
        });
    }

    // Headless colour-correction render proof (--cc-test <image>): render a
    // baseline, apply exposure+gamma, render again, and report whether the
    // rendered output changed (i.e. the super-shader CC/LUT reaches renders).
    const QString ccTestFile = resolveCCTestFile(argc, argv);
    if (!ccTestFile.isEmpty()) {
        QTimer::singleShot(5000, &window, [&window, ccTestFile]() {
            const int code = window.runHeadlessCCTest(ccTestFile);
            fflush(stdout);
            std::_Exit(code);
        });
    }

    const QString fxMultiTestFile = resolveFXMultiTestFile(argc, argv);
    if (!fxMultiTestFile.isEmpty()) {
        QTimer::singleShot(5000, &window, [&window, fxMultiTestFile]() {
            const int code = window.runHeadlessFXMultiTest(fxMultiTestFile);
            fflush(stdout);
            std::_Exit(code);
        });
    }

    // Headless video-export test (--video-test <dir>): render the in/out
    // range to a temp PNG sequence, encode an mp4, and quit.
    const QString videoTestDir = resolveVideoTestDir(argc, argv);
    if (!videoTestDir.isEmpty()) {
        QDir().mkpath(videoTestDir);
        QTimer::singleShot(5000, &window, [&window, videoTestDir]() {
            const int ok = window.runHeadlessVideoTest(videoTestDir);
            fflush(stdout);
            std::_Exit(ok ? 0 : 2);
        });
    }

    return application.run();
}
