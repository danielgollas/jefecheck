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
#include <QTimer>

#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <filesystem>

#ifndef _WIN32
#include <unistd.h>   // execv, for the WebRTC-harness transport re-exec
#endif

#include <QProcess>
#include <QProcessEnvironment>

#include "gfcCoordinatorSignaling.h"
#include "auth/gfcOAuthPkce.h"
#include "auth/gfcLoopbackServer.h"
#include "gfcTestCoordinator.h"
#include "gfcSignaling.h"
#include "gfcStructures.h"
#include "gfcWireTest.h"
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

// --wire-test : headless jefe::wire round-trip/bounds-safety self-test
// (JEF-23). Pure data, no Qt/GL/rendering-chain dependency at all — takes
// no argument. Dispatched at the very top of main(), before QApplication
// is even constructed, since the test needs nothing that setup provides.
static bool hasWireTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--wire-test") == 0) return true;
    return false;
}

// --signal-test : headless WebSocket signaling-stub loopback self-test
// (JEF-24 Task 2). Like --wire-test, needs nothing setup provides, so it
// runs and exits before QApplication is constructed.
static bool hasSignalTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--signal-test") == 0) return true;
    return false;
}

// --coord-signal-test : headless cloud-coordinator codec + loopback self-test
// (JEF-27 Task 1). Pure codec assertions plus a bounded rtc::WebSocketServer
// loopback; needs nothing setup provides, so it runs before QApplication.
static bool hasCoordSignalTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--coord-signal-test") == 0) return true;
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

// --remote-test-webrtc : orchestrator/server role, WebRTC transport (JEF-24
// Task 5). Identical to --remote-test but forces JEFECHECK_TRANSPORT=webrtc so
// host + peer establish a real DTLS-encrypted libdatachannel data channel over
// the localhost signaling stub instead of a RakNet connection.
static bool hasRemoteTestWebrtc(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--remote-test-webrtc") == 0) return true;
    return false;
}
// --remote-test-webrtc-peer <ip> <port> : child/client role for the WebRTC
// harness.
static bool resolveRemoteWebrtcPeer(int argc, char* argv[], std::string& ip, int& port) {
    for (int i = 1; i + 2 < argc; ++i) {
        if (std::strcmp(argv[i], "--remote-test-webrtc-peer") == 0 && i + 2 < argc) {
            ip = argv[i + 1]; port = std::atoi(argv[i + 2]); return true;
        }
    }
    return false;
}

// --stats-test : orchestrator/server role (JEF-30 Task 1). Same two-process
// WebRTC harness as --remote-test-webrtc, but after the session establishes +
// traffic flows, it asserts the per-peer stats getter returns REAL WebRTC stats
// (≥1 connected peer, bytes>0, a resolved Direct/Relay path). Forces
// JEFECHECK_TRANSPORT=webrtc via the same top-of-main re-exec.
static bool hasStatsTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--stats-test") == 0) return true;
    return false;
}

// --asset-test : orchestrator role (JEF-28 Task 2). Loads a fixture LUT (and an
// FX when a GL context exists) into the host, brings up the RakNet server,
// spawns a peer child, and asserts the peer received + hot-loaded the asset via
// the automatic late-join FX/LUT sync. RakNet: simplest reliable transport for
// the gate; the sync is transport-agnostic.
static bool hasAssetTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--asset-test") == 0) return true;
    return false;
}
// --asset-test-peer <ip> <port> <lutHash> <fxHash> : child/joiner role. Joins,
// lets the sync run, and asserts the fixture hashes now live in its managers.
// fxHash "-" means "host had no usable FX" (FX scoped out — see the harness).
static bool resolveAssetPeer(int argc, char* argv[], std::string& ip, int& port,
                             std::string& lutHash, std::string& fxHash) {
    for (int i = 1; i + 4 < argc; ++i) {
        if (std::strcmp(argv[i], "--asset-test-peer") == 0) {
            ip = argv[i + 1]; port = std::atoi(argv[i + 2]);
            lutHash = argv[i + 3]; fxHash = argv[i + 4];
            return true;
        }
    }
    return false;
}

// --asset-test-webrtc : orchestrator role (JEF-28 Task 4). WebRTC variant of
// --asset-test that drives a LARGE LUT (multi-MB .cube) over the WebRTC assets
// channel so the transport-level chunking + bufferedAmount backpressure are
// actually exercised, then asserts the joiner's received LUT content hash
// matches the host's (byte-integrity through chunk/reassembly). Forces
// JEFECHECK_TRANSPORT=webrtc via the re-exec above.
static bool hasAssetTestWebrtc(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--asset-test-webrtc") == 0) return true;
    return false;
}
// --asset-test-webrtc-peer <ip> <port> <lutHash> : joiner child role. Joins over
// WebRTC, lets the sync + chunked transfer run, asserts the LUT hash arrived.
static bool resolveAssetWebrtcPeer(int argc, char* argv[], std::string& ip,
                                   int& port, std::string& lutHash) {
    for (int i = 1; i + 3 < argc; ++i) {
        if (std::strcmp(argv[i], "--asset-test-webrtc-peer") == 0) {
            ip = argv[i + 1]; port = std::atoi(argv[i + 2]);
            lutHash = argv[i + 3];
            return true;
        }
    }
    return false;
}
// Generate a multi-MB Truelight-Cube v2.0 .cube fixture at `path` with a
// `cubeSize`^3 identity ramp. cubeSize=45 → 91125 triads ≈ 2.4 MB, comfortably
// past the 60 KB chunk payload AND the ~256 KB single-SCTP-message limit (≈42
// chunks) and past the 1 MB backpressure high-water mark. Returns the file size
// in bytes, or 0 on failure. Deterministic bytes (the peer receives them
// verbatim, so cross-platform float formatting is irrelevant to hashmatch).
static long generateLargeCube(const std::string& path, int cubeSize) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return 0;
    std::fprintf(f, "# Truelight Cube v2.0\n");
    std::fprintf(f, "# iDims 3\n");
    std::fprintf(f, "# oDims 3\n");
    std::fprintf(f, "# width %d height %d depth %d\n", cubeSize, cubeSize, cubeSize);
    std::fprintf(f, "# InputLUT\n");
    std::fprintf(f, "# Cube\n");
    const double d = (cubeSize > 1) ? (cubeSize - 1) : 1;
    for (int i = 0; i < cubeSize; ++i)
        for (int j = 0; j < cubeSize; ++j)
            for (int k = 0; k < cubeSize; ++k)
                std::fprintf(f, "%f %f %f\n", i / d, j / d, k / d);
    std::fflush(f);
    const long sz = std::ftell(f);
    std::fclose(f);
    return sz;
}

// --coord-test : orchestrator role, WebRTC transport through a self-contained
// test-double coordinator (JEF-27 Task 3). Starts the coordinator on an
// ephemeral ws port, brings up the host in coordinator mode (create-session),
// spawns a peer child that joins by code, and asserts the P2P session mirrors
// play. Forces JEFECHECK_TRANSPORT=webrtc via the same re-exec as the LAN
// WebRTC harness so the host + loopback transports pick WebRtcTransport.
static bool hasCoordTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--coord-test") == 0) return true;
    return false;
}
// --coord-test-server : runs ONLY the test-double coordinator (a
// rtc::WebSocketServer) on an ephemeral port, in its OWN process. It prints
// "COORD-URL=ws://127.0.0.1:<port>/" on stdout and then serves until killed.
// The coordinator MUST be its own process: libdatachannel misroutes/drops
// inbound messages when a WebSocketServer and multiple client rtc::WebSockets
// (the host's coordinator socket + its loopback client's socket) share one
// process — matching real-world usage where the coordinator is remote.
static bool hasCoordTestServer(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--coord-test-server") == 0) return true;
    return false;
}
// --coord-test-peer <coordUrl> <code> : child/joiner role for the coord harness.
static bool resolveCoordTestPeer(int argc, char* argv[], std::string& url,
                                 std::string& code) {
    for (int i = 1; i + 2 < argc; ++i) {
        if (std::strcmp(argv[i], "--coord-test-peer") == 0 && i + 2 < argc) {
            url = argv[i + 1]; code = argv[i + 2]; return true;
        }
    }
    return false;
}

int main(int argc, char* argv[]) {
    // WebRTC-harness transport re-exec (JEF-24 Task 5). The transport factory
    // reads JEFECHECK_TRANSPORT, but `networkManager` is a global whose client
    // and server construct their transports during STATIC initialization —
    // before main() runs. So a qputenv() here would be too late for THIS
    // process: its transports are already RakNet. To force WebRTC for the
    // --remote-test-webrtc host (and a manually-launched peer), set the env and
    // re-exec: the fresh process image re-runs static init with the var already
    // in the environment, so both client and server pick WebRtcTransport. The
    // spawned peer child already inherits the var at spawn time, so this only
    // ever fires once per process (guarded on the var already being "webrtc").
#ifndef _WIN32
    {
        bool webrtcHarness = false;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--remote-test-webrtc") == 0 ||
                std::strcmp(argv[i], "--remote-test-webrtc-peer") == 0 ||
                std::strcmp(argv[i], "--asset-test-webrtc") == 0 ||
                std::strcmp(argv[i], "--asset-test-webrtc-peer") == 0 ||
                std::strcmp(argv[i], "--coord-test") == 0 ||
                std::strcmp(argv[i], "--coord-test-peer") == 0 ||
                std::strcmp(argv[i], "--stats-test") == 0) {
                webrtcHarness = true;
                break;
            }
        }
        if (webrtcHarness) {
            const char* cur = std::getenv("JEFECHECK_TRANSPORT");
            if (!cur || std::strcmp(cur, "webrtc") != 0) {
                setenv("JEFECHECK_TRANSPORT", "webrtc", 1);
                execv(argv[0], argv);         // fresh static init, env now set
                perror("execv (webrtc re-exec)");  // only reached on failure
                return 3;
            }
        }
    }
#endif

    // Headless jefe::wire self-test (--wire-test, JEF-23): pure data, no
    // GUI/GL/Qt-application dependency, so it runs and exits before
    // QApplication is even constructed — same "as early as possible"
    // treatment as the other --*-test flags, just earlier since this one
    // truly needs nothing else set up first.
    if (hasWireTest(argc, argv)) {
        return jefe::wire::selfTest();
    }

    // Headless PKCE codec self-test (--pkce-test, JEF-31): pure string/crypto
    // work with no sockets and no event loop, so like --wire-test it runs and
    // exits before QApplication exists.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--pkce-test") == 0) {
            return jefe::auth::pkceSelfTest();
        }
        // --loopback-test (JEF-31): binds a real socket, so unlike --pkce-test
        // it needs an event loop — it constructs its own QCoreApplication and
        // still runs before QApplication/GL exist.
        if (std::strcmp(argv[i], "--loopback-test") == 0) {
            return jefe::auth::loopbackSelfTest(argc, argv);
        }
    }

    // Headless signaling-stub self-test (--signal-test, JEF-24): brings up a
    // local WebSocket SignalingServer + SignalingClient and round-trips
    // hello/offer JSON. Constructs rtc objects, so it brackets itself with
    // rtc::Preload()/Cleanup() internally. Runs before QApplication.
    if (hasSignalTest(argc, argv)) {
        return jefe::net::signalingSelfTest();
    }

    // Headless cloud-coordinator self-test (--coord-signal-test, JEF-27): pure
    // codec round-trip for the JEF-25 envelopes plus a bounded loopback against
    // a scripted rtc::WebSocketServer. Brackets rtc::Preload()/Cleanup()
    // internally. Runs before QApplication, same as --signal-test.
    if (hasCoordSignalTest(argc, argv)) {
        return jefe::net::coordinatorSignalingSelfTest();
    }

    // Test-double coordinator process (--coord-test-server, JEF-27 Task 3). Runs
    // ONLY the rtc::WebSocketServer coordinator on an ephemeral port; prints its
    // URL and serves until killed by the --coord-test orchestrator. Its own
    // process so it never shares libdatachannel state with the client sockets.
    if (hasCoordTestServer(argc, argv)) {
        jefe::net::TestCoordinator coordinator;
        if (!coordinator.start()) {
            std::fprintf(stderr, "COORD-URL=ERROR\n");
            return 3;
        }
        std::printf("COORD-URL=%s\n", coordinator.url().c_str());
        std::fflush(stdout);
        for (;;) std::this_thread::sleep_for(std::chrono::seconds(3600));
    }

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

    // JEF-28: assign a real, writable per-user directory for P2P-received
    // assets. `sett.receivedPath` was declared but never assigned, so received
    // LUT/FX files (unserializeLUT/unserializeFX) landed in the process CWD.
    // Point it at getApplicationDataPath()/received/ and create it. Runs here
    // (after setMacExecutablePath primes getApplicationDataPath on macOS, and
    // before any --remote-test/--coord-test networking dispatch below).
    {
        std::string received = getApplicationDataPath() + "received/";
        std::error_code ec;
        std::filesystem::create_directories(received, ec);
        sett.receivedPath = received;   // gfcSettings global (extern above)
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

    // --remote-test-peer <ip> <port>: child client role. Connects, holds,
    // exits. Headless; playback state is pure data (no GL needed).
    {
        std::string peerIp; int peerPort = 0;
        if (resolveRemotePeer(argc, argv, peerIp, peerPort)) {
            jefe::qt::initializeRenderingChain();
            jefe::qt::remoteTestPeerConnect(peerIp, peerPort, /*holdMs=*/2000, /*play=*/true);
            std::_Exit(0);
        }
    }
    if (hasRemoteTest(argc, argv)) {
        jefe::qt::initializeRenderingChain();
        const int port = 60123;
        QProcess peer;
        peer.setProgram(QCoreApplication::applicationFilePath());
        peer.setArguments({"--remote-test-peer", "127.0.0.1", QString::number(port)});
        peer.start();
        if (!peer.waitForStarted(2000)) { printf("REMOTE-TEST: child failed to start: %s\n", peer.errorString().toUtf8().constData()); fflush(stdout); std::_Exit(3); }
        const bool sawPlay = jefe::qt::remoteTestServerSawPlay(port, /*settleMs=*/4000);
        const int  peak    = (int)jefe::qt::remoteParticipants().size();
        peer.waitForFinished(3000);
        if (peer.state() != QProcess::NotRunning) peer.kill();
        printf("REMOTE-TEST: participants=%d mirrored_play=%d\n", peak, sawPlay ? 1 : 0);
        fflush(stdout);
        std::_Exit((peak >= 1 && sawPlay) ? 0 : 2);
    }

    // --asset-test-peer <ip> <port> <lutHash> <fxHash>: joiner child role
    // (JEF-28 Task 2). Connects to the host, holds while the automatic FX/LUT
    // sync runs, then asserts the fixture hashes now live in this process's
    // managers. Reports its result on stdout ("ASSET-PEER: lut=.. fx=..") and
    // via exit code (0 iff the LUT transferred).
    {
        std::string peerIp, lutHash, fxHash; int peerPort = 0;
        if (resolveAssetPeer(argc, argv, peerIp, peerPort, lutHash, fxHash)) {
            jefe::qt::initializeRenderingChain();
            // The peer hot-loads the received LUT/FX (loadLUT → glTexImage3D,
            // loadFX → shader compile), so it needs a current GL context.
            if (!jefe::qt::setupOffscreenTestGL()) {
                printf("ASSET-PEER: gl setup failed\n");
                fflush(stdout);
                std::_Exit(3);
            }
            // Connect + hold + pump; the sync fires automatically on join.
            // RakNet connects instantly; 4 s of pumping is ample for the few
            // handshake round-trips (FX sinc → LUT sinc → the LUT push).
            jefe::qt::remoteTestPeerConnect(peerIp, peerPort, /*holdMs=*/4000,
                                            /*play=*/false);
            const bool gotLut = jefe::qt::remoteHasLUTHash(lutHash);
            const bool fxApplicable = (fxHash != "-" && !fxHash.empty());
            const bool gotFx = fxApplicable && jefe::qt::remoteHasFXHash(fxHash);
            printf("ASSET-PEER: lut=%d fx=%d lutCount=%d fxCount=%d\n",
                   gotLut ? 1 : 0, gotFx ? 1 : 0,
                   jefe::qt::remoteLUTCount(), jefe::qt::remoteFXCount());
            fflush(stdout);
            std::_Exit(gotLut ? 0 : 2);
        }
    }
    if (hasAssetTest(argc, argv)) {
        jefe::qt::initializeRenderingChain();
        // loadLUT/loadFX create GL objects (LUT: glTexImage3D; FX: ARB shader
        // objects), so bring up an offscreen GL context first — without it the
        // fixture loads call null GLAD pointers and crash.
        if (!jefe::qt::setupOffscreenTestGL()) {
            printf("ASSET-TEST: gl setup failed\n");
            fflush(stdout);
            std::_Exit(3);
        }
        const std::string fxDir = getApplicationDataPath() + "FX/";
        const std::string lutFixture = fxDir + "invert.lut";
        const std::string fxFixture  = fxDir + "ADD.jfx";
        // Load the fixtures into the HOST before the peer joins. With the
        // offscreen GL context above, loadFX compiles+links the shaders and
        // assigns a content hash, so FX is asserted too; if a build's offscreen
        // context can't compile the shader, the FX hash comes back empty and the
        // harness reports fx=na (LUT stays mandatory). loadLUT assigns a portable
        // content hash from the .lut bytes and uploads a 3D texture.
        const std::string lutHash = jefe::qt::assetTestLoadLUT(lutFixture);
        const std::string fxHash  = jefe::qt::assetTestLoadFX(fxFixture);
        printf("ASSET-TEST: host lut hash=%s (lutCount=%d) fx hash=%s (fxCount=%d)\n",
               lutHash.c_str(), jefe::qt::remoteLUTCount(),
               fxHash.empty() ? "<none:no-GL>" : fxHash.c_str(),
               jefe::qt::remoteFXCount());
        fflush(stdout);
        if (lutHash.empty()) {
            printf("ASSET-TEST: host failed to load fixture LUT %s\n",
                   lutFixture.c_str());
            fflush(stdout);
            std::_Exit(3);
        }
        const bool fxApplicable = !fxHash.empty();

        const int port = 60125;   // distinct from the other harness ports
        jefe::qt::assetTestServerStart(port);

        QProcess peer;
        peer.setProgram(QCoreApplication::applicationFilePath());
        peer.setArguments({"--asset-test-peer", "127.0.0.1", QString::number(port),
                           QString::fromStdString(lutHash),
                           QString::fromStdString(fxApplicable ? fxHash : "-")});
        // Always CAPTURE the child's stdout (SeparateChannels, the QProcess
        // default) so we can read back its ASSET-PEER marker — ForwardedChannels
        // would route it to the terminal and leave readAllStandardOutput empty.
        // For debug visibility, forward the child's STDERR to ours (the sync
        // printfs go to stdout, which we still capture + echo below).
        if (qEnvironmentVariableIsSet("JEFECHECK_REMOTE_TEST_DEBUG"))
            peer.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        peer.start();
        if (!peer.waitForStarted(3000)) {
            printf("ASSET-TEST: child failed to start: %s\n",
                   peer.errorString().toUtf8().constData());
            fflush(stdout);
            std::_Exit(3);
        }
        // Pump the server while the peer connects + syncs. Bounded (9 s) so the
        // harness always terminates; the peer holds ~4 s + connect.
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(9000);
        while (peer.state() != QProcess::NotRunning &&
               std::chrono::steady_clock::now() < deadline) {
            jefe::qt::assetTestServerPump(50);
        }
        peer.waitForFinished(1000);
        if (peer.state() != QProcess::NotRunning) peer.kill();

        // Parse the peer's ASSET-PEER marker from its captured stdout and echo
        // the marker line so the peer's verdict is visible in the orchestrator's
        // own output.
        int peerLut = 0, peerFx = 0;
        const QString out = QString::fromUtf8(peer.readAllStandardOutput());
        for (const QString& line : out.split('\n')) {
            if (line.startsWith("ASSET-PEER:")) {
                printf("%s\n", line.toUtf8().constData());
                if (line.contains("lut=1")) peerLut = 1;
                if (line.contains("fx=1"))  peerFx = 1;
            }
        }
        const bool lutOk = (peerLut == 1);
        // FX is N/A when the host had no GL context to compile it (the common
        // headless case); only report a 0/1 verdict when the host actually had a
        // usable FX asset to push.
        if (fxApplicable)
            printf("ASSET-TEST: lut=%d fx=%d\n", lutOk ? 1 : 0, peerFx);
        else
            printf("ASSET-TEST: lut=%d fx=na\n", lutOk ? 1 : 0);
        fflush(stdout);
        std::_Exit(lutOk && (!fxApplicable || peerFx == 1) ? 0 : 2);
    }

    // --remote-test-webrtc-peer <ip> <port>: WebRTC child client role. The
    // WebRTC transport is already forced by the re-exec at the top of main()
    // (JEFECHECK_TRANSPORT=webrtc was in the environment before static init, so
    // the client's transport is a WebRtcTransport). holdMs is much larger than
    // the RakNet peer's (2000 ms): WebRTC needs signaling + ICE + DTLS +
    // datachannel-open before it is connected enough to send the play toggle,
    // and the peer only sends play once (on the toggle), so it must stay
    // connected first — connectTimeoutMs is bumped so the toggle fires AFTER the
    // channel opens (a pre-open send would be silently dropped).
    {
        std::string peerIp; int peerPort = 0;
        if (resolveRemoteWebrtcPeer(argc, argv, peerIp, peerPort)) {
            jefe::qt::initializeRenderingChain();
            jefe::qt::remoteTestPeerConnect(peerIp, peerPort, /*holdMs=*/6000,
                                            /*play=*/true, /*connectTimeoutMs=*/9000);
            std::_Exit(0);
        }
    }
    if (hasRemoteTestWebrtc(argc, argv)) {
        // WebRTC is already forced by the re-exec at the top of main().
        jefe::qt::initializeRenderingChain();
        // Distinct port from the RakNet harness (60123) to avoid collisions if
        // both run back-to-back and a socket lingers in TIME_WAIT.
        const int port = 60124;

        // Phase 1: bring the host's own loopback client fully up BEFORE the peer
        // exists. The peer's play is one-shot and the host mirrors it through the
        // loopback; a WebRTC loopback can open its channel later than a fast peer,
        // so if the peer played first the forward would reach 0 channels and be
        // lost. Waiting for the loopback here closes that race. (See the bridge.)
        if (!jefe::qt::remoteTestServerStart(port, /*loopbackTimeoutMs=*/10000)) {
            printf("REMOTE-TEST-WEBRTC: loopback client failed to come up\n");
            fflush(stdout);
            std::_Exit(3);
        }

        // Phase 2: now spawn the peer. It will establish its own WebRTC session,
        // and its single play toggle is guaranteed a live loopback to mirror to.
        QProcess peer;
        peer.setProgram(QCoreApplication::applicationFilePath());
        peer.setArguments({"--remote-test-webrtc-peer", "127.0.0.1", QString::number(port)});
        // QProcess inherits the parent env (which already has the var set), but
        // spell it out explicitly so the child's transport selection can never
        // depend on inheritance semantics.
        QProcessEnvironment childEnv = QProcessEnvironment::systemEnvironment();
        childEnv.insert("JEFECHECK_TRANSPORT", "webrtc");
        peer.setProcessEnvironment(childEnv);
        if (qEnvironmentVariableIsSet("JEFECHECK_REMOTE_TEST_DEBUG"))
            peer.setProcessChannelMode(QProcess::ForwardedChannels);
        peer.start();
        if (!peer.waitForStarted(2000)) { printf("REMOTE-TEST-WEBRTC: child failed to start: %s\n", peer.errorString().toUtf8().constData()); fflush(stdout); std::_Exit(3); }
        // WebRTC session establishment (signaling handshake → ICE → DTLS →
        // SCTP datachannel open) takes far longer than RakNet's instant connect,
        // so the settle budget is generous (10 s) to absorb the peer's own
        // handshake plus ICE/DTLS on a loaded machine before its play arrives.
        const bool sawPlay = jefe::qt::remoteTestServerSettleForPlay(/*settleMs=*/10000);
        const int  peak    = (int)jefe::qt::remoteParticipants().size();
        peer.waitForFinished(6000);
        if (peer.state() != QProcess::NotRunning) peer.kill();
        printf("REMOTE-TEST-WEBRTC: participants=%d mirrored_play=%d\n", peak, sawPlay ? 1 : 0);
        fflush(stdout);
        std::_Exit((peak >= 1 && sawPlay) ? 0 : 2);
    }

    // --stats-test (JEF-30 Task 1): reuse the --remote-test-webrtc two-process
    // WebRTC harness (host + loopback + a spawned peer child) to establish a real
    // libdatachannel session with traffic, then assert the per-peer stats getter
    // returns REAL stats. Success requires ≥1 connected peer with bytes>0 and a
    // resolved Direct/Relay path. rtt is NOT required (localhost rtt is often
    // 0/nullopt → -1). WebRTC forced by the top-of-main re-exec.
    if (hasStatsTest(argc, argv)) {
        jefe::qt::initializeRenderingChain();
        const int port = 60127;   // distinct from the other harness ports

        // Phase 1: host + its loopback client fully up (a real WebRTC peer with
        // handshake/sync traffic — enough for stats on its own).
        if (!jefe::qt::remoteTestServerStart(port, /*loopbackTimeoutMs=*/10000)) {
            printf("STATS-TEST: loopback client failed to come up\n");
            fflush(stdout);
            std::_Exit(3);
        }

        // Phase 2: spawn a second WebRTC peer (reuses the remote-test child role)
        // so there is cross-process traffic + a second selected candidate pair.
        QProcess peer;
        peer.setProgram(QCoreApplication::applicationFilePath());
        peer.setArguments({"--remote-test-webrtc-peer", "127.0.0.1", QString::number(port)});
        QProcessEnvironment childEnv = QProcessEnvironment::systemEnvironment();
        childEnv.insert("JEFECHECK_TRANSPORT", "webrtc");
        peer.setProcessEnvironment(childEnv);
        if (qEnvironmentVariableIsSet("JEFECHECK_REMOTE_TEST_DEBUG"))
            peer.setProcessChannelMode(QProcess::ForwardedChannels);
        peer.start();
        if (!peer.waitForStarted(2000)) {
            printf("STATS-TEST: child failed to start: %s\n",
                   peer.errorString().toUtf8().constData());
            fflush(stdout);
            std::_Exit(3);
        }

        // Let the peer's session + play toggle generate traffic on the host.
        jefe::qt::remoteTestServerSettleForPlay(/*settleMs=*/10000);

        // Poll the stats getter with a bounded retry so bytes counters + the
        // selected candidate pair have settled (both populate slightly after the
        // channel opens). Accept the first peer with real stats.
        bool ok = false;
        int  peersReport = 0;
        long rttReport = -1;
        unsigned long long bytesReport = 0;
        std::string pathReport = "unknown";
        for (int t = 0; t < 4000 && !ok; t += 50) {
            jefe::qt::pumpNetwork();
            auto stats = jefe::qt::remotePeerStats();
            peersReport = (int)stats.size();
            for (const auto& s : stats) {
                if (s.connected && s.bytes > 0 &&
                    (s.path == "direct" || s.path == "relay")) {
                    ok = true;
                    rttReport = s.rttMs;
                    bytesReport = s.bytes;
                    pathReport = s.path;
                    break;
                }
            }
            if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        peer.waitForFinished(6000);
        if (peer.state() != QProcess::NotRunning) peer.kill();
        printf("STATS-TEST: peers=%d rtt=%ld bytes=%llu path=%s\n",
               peersReport, rttReport, bytesReport, pathReport.c_str());
        fflush(stdout);
        std::_Exit(ok ? 0 : 2);
    }

    // --asset-test-webrtc-peer <ip> <port> <lutHash>: WebRTC joiner child role
    // (JEF-28 Task 4). Joins the host over WebRTC, holds while the late-join sync
    // pushes the LARGE LUT over the assets channel (chunked + reassembled by the
    // transport), then asserts the host's LUT content hash now lives in this
    // process's lutManager (byte-integrity proof). WebRTC needs the long hold to
    // absorb signaling + ICE + DTLS + the multi-MB chunked transfer.
    {
        std::string peerIp, lutHash; int peerPort = 0;
        if (resolveAssetWebrtcPeer(argc, argv, peerIp, peerPort, lutHash)) {
            jefe::qt::initializeRenderingChain();
            if (!jefe::qt::setupOffscreenTestGL()) {
                printf("ASSET-PEER-WEBRTC: gl setup failed\n");
                fflush(stdout);
                std::_Exit(3);
            }
            jefe::qt::remoteTestPeerConnect(peerIp, peerPort, /*holdMs=*/12000,
                                            /*play=*/false, /*connectTimeoutMs=*/12000);
            const bool gotLut = jefe::qt::remoteHasLUTHash(lutHash);
            printf("ASSET-PEER-WEBRTC: lut=%d hashmatch=%d lutCount=%d\n",
                   jefe::qt::remoteLUTCount() > 0 ? 1 : 0, gotLut ? 1 : 0,
                   jefe::qt::remoteLUTCount());
            fflush(stdout);
            std::_Exit(gotLut ? 0 : 2);
        }
    }
    if (hasAssetTestWebrtc(argc, argv)) {
        // WebRTC is already forced by the re-exec at the top of main().
        jefe::qt::initializeRenderingChain();
        // The host hot-loads the fixture LUT (glTexImage3D) so it needs GL.
        if (!jefe::qt::setupOffscreenTestGL()) {
            printf("ASSET-TEST-WEBRTC: gl setup failed\n");
            fflush(stdout);
            std::_Exit(3);
        }
        // Generate + load the LARGE LUT into the HOST before the peer joins.
        const std::string lutFixture =
            (QDir::tempPath() + "/jefecheck_large_lut.cube").toStdString();
        const long lutBytes = generateLargeCube(lutFixture, /*cubeSize=*/45);
        if (lutBytes <= 0) {
            printf("ASSET-TEST-WEBRTC: failed to generate fixture\n");
            fflush(stdout);
            std::_Exit(3);
        }
        const std::string lutHash = jefe::qt::assetTestLoadLUT(lutFixture);
        printf("ASSET-TEST-WEBRTC: host lut hash=%s bytes=%ld (lutCount=%d)\n",
               lutHash.empty() ? "<none>" : lutHash.c_str(), lutBytes,
               jefe::qt::remoteLUTCount());
        fflush(stdout);
        if (lutHash.empty()) {
            printf("ASSET-TEST-WEBRTC: host failed to load fixture LUT\n");
            fflush(stdout);
            std::_Exit(3);
        }

        const int port = 60126;  // distinct from the other harness ports
        // Phase 1: bring the host + its loopback client fully up over WebRTC
        // (the loopback shares this process's managers, so it already has the LUT
        // and is never pushed to — it just gates that the host is live).
        if (!jefe::qt::remoteTestServerStart(port, /*loopbackTimeoutMs=*/12000)) {
            printf("ASSET-TEST-WEBRTC: loopback client failed to come up\n");
            fflush(stdout);
            std::_Exit(3);
        }

        // Phase 2: spawn the joiner; it establishes its own WebRTC session and
        // receives the large LUT over the assets channel.
        QProcess peer;
        peer.setProgram(QCoreApplication::applicationFilePath());
        peer.setArguments({"--asset-test-webrtc-peer", "127.0.0.1",
                           QString::number(port), QString::fromStdString(lutHash)});
        QProcessEnvironment childEnv = QProcessEnvironment::systemEnvironment();
        childEnv.insert("JEFECHECK_TRANSPORT", "webrtc");
        peer.setProcessEnvironment(childEnv);
        peer.start();
        if (!peer.waitForStarted(3000)) {
            printf("ASSET-TEST-WEBRTC: child failed to start: %s\n",
                   peer.errorString().toUtf8().constData());
            fflush(stdout);
            std::_Exit(3);
        }
        // Pump the host while the peer connects + the chunked transfer runs.
        // Bounded (20 s) so the harness always terminates; the peer holds ~12 s
        // plus its own signaling/ICE/DTLS.
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(20000);
        while (peer.state() != QProcess::NotRunning &&
               std::chrono::steady_clock::now() < deadline) {
            jefe::qt::remoteTestServerSettleForPlay(/*settleMs=*/200);
        }
        peer.waitForFinished(2000);
        if (peer.state() != QProcess::NotRunning) peer.kill();

        int peerLut = 0, peerHash = 0;
        const QString out = QString::fromUtf8(peer.readAllStandardOutput());
        for (const QString& line : out.split('\n')) {
            if (line.startsWith("ASSET-PEER-WEBRTC:")) {
                printf("%s\n", line.toUtf8().constData());
                if (line.contains("lut=1"))       peerLut = 1;
                if (line.contains("hashmatch=1")) peerHash = 1;
            }
        }
        printf("ASSET-TEST-WEBRTC: lut=%d bytes=%ld hashmatch=%d\n",
               peerLut, lutBytes, peerHash);
        fflush(stdout);
        std::_Exit((peerLut == 1 && peerHash == 1) ? 0 : 2);
    }

    // --coord-test-peer <coordUrl> <code>: WebRTC joiner child role for the
    // cloud-coordinator harness. WebRTC is already forced by the re-exec at the
    // top of main(); this joins the coordinator by code and toggles play. holdMs
    // + connectTimeoutMs mirror the LAN WebRTC peer (coordinator + ICE + DTLS +
    // datachannel must all complete before the one-shot play toggle can ship).
    {
        std::string coordUrl, code;
        if (resolveCoordTestPeer(argc, argv, coordUrl, code)) {
            jefe::qt::initializeRenderingChain();
            jefe::qt::coordTestPeerJoin(coordUrl, code, /*holdMs=*/6000,
                                        /*play=*/true, /*connectTimeoutMs=*/12000);
            std::_Exit(0);
        }
    }
    if (hasCoordTest(argc, argv)) {
        // WebRTC is already forced by the re-exec at the top of main().
        jefe::qt::initializeRenderingChain();

        // 1. Spawn the test-double coordinator as its OWN process (see
        //    --coord-test-server) and read the ephemeral ws URL it prints. It
        //    MUST be a separate process: a co-located WebSocketServer + the two
        //    client rtc::WebSockets the host opens (its coordinator socket + the
        //    loopback client's) trip a libdatachannel message-routing bug. A
        //    remote coordinator is also what real usage looks like.
        QProcess coord;
        coord.setProgram(QCoreApplication::applicationFilePath());
        coord.setArguments({"--coord-test-server"});
        coord.start();
        if (!coord.waitForStarted(3000)) {
            printf("COORD-TEST: coordinator failed to start: %s\n",
                   coord.errorString().toUtf8().constData());
            fflush(stdout);
            std::_Exit(3);
        }
        std::string coordUrl;
        {
            QByteArray acc;
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(5);
            // The child emits unrelated stdout ("No timer") before COORD-URL=,
            // so scan every complete line for the marker until found or timeout.
            while (coordUrl.empty() &&
                   std::chrono::steady_clock::now() < deadline) {
                if (coord.waitForReadyRead(200)) acc += coord.readAllStandardOutput();
                int nl;
                while ((nl = acc.indexOf('\n')) >= 0) {
                    const QByteArray line = acc.left(nl);
                    acc = acc.mid(nl + 1);
                    if (line.startsWith("COORD-URL=")) {
                        coordUrl = line.mid(int(std::strlen("COORD-URL=")))
                                       .trimmed().toStdString();
                        break;
                    }
                }
            }
        }
        if (coordUrl.empty() || coordUrl == "ERROR") {
            printf("COORD-TEST: coordinator URL not received\n");
            fflush(stdout);
            if (coord.state() != QProcess::NotRunning) coord.kill();
            std::_Exit(3);
        }

        // 2. Bring the host up in coordinator mode (create-session → assigned
        //    code) and wait for its loopback client to fully register (its P2P
        //    channel open both ways) BEFORE the peer exists — same one-shot-play
        //    race fix as the LAN WebRTC harness.
        if (!jefe::qt::coordTestHostStart(coordUrl, /*loopbackTimeoutMs=*/12000)) {
            printf("COORD-TEST: host/loopback failed to come up (code=%s)\n",
                   jefe::qt::coordTestGetCode().c_str());
            fflush(stdout);
            std::_Exit(3);
        }
        const std::string code = jefe::qt::coordTestGetCode();

        // 3. Spawn the peer child. It joins the SAME coordinator by the assigned
        //    code and, once its P2P session is up, toggles play (mirrored to the
        //    host over the P2P channel → forwarded to the loopback → isPlaying()).
        QProcess peer;
        peer.setProgram(QCoreApplication::applicationFilePath());
        peer.setArguments({"--coord-test-peer",
                           QString::fromStdString(coordUrl),
                           QString::fromStdString(code)});
        QProcessEnvironment childEnv = QProcessEnvironment::systemEnvironment();
        childEnv.insert("JEFECHECK_TRANSPORT", "webrtc");
        peer.setProcessEnvironment(childEnv);
        if (qEnvironmentVariableIsSet("JEFECHECK_REMOTE_TEST_DEBUG"))
            peer.setProcessChannelMode(QProcess::ForwardedChannels);
        peer.start();
        if (!peer.waitForStarted(2000)) { printf("COORD-TEST: child failed to start: %s\n", peer.errorString().toUtf8().constData()); fflush(stdout); std::_Exit(3); }

        const bool sawPlay = jefe::qt::coordTestSettleForPlay(/*settleMs=*/12000);
        const int  peak    = (int)jefe::qt::remoteParticipants().size();
        peer.waitForFinished(8000);
        if (peer.state() != QProcess::NotRunning) peer.kill();
        if (coord.state() != QProcess::NotRunning) coord.kill();
        printf("COORD-TEST: participants=%d mirrored_play=%d\n", peak, sawPlay ? 1 : 0);
        fflush(stdout);
        std::_Exit((peak >= 1 && sawPlay) ? 0 : 2);
    }

    MainWindow_Qt window;
    window.setObjectName("MainWindow");
    window.show();

    // --ui-preview (JEF-31/37): open the Remote dock with the Cloud and
    // admission layouts filled with sample data, for design review before the
    // sign-in and lobby wiring exists. Deferred so docks are laid out first.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ui-preview") == 0) {
            QTimer::singleShot(0, &window, [&window]() {
                window.showRemoteUiPreview();
            });
            break;
        }
    }

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
