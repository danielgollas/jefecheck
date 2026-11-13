// Qt entry point for the USE_QT build: QApplication, MainWindow_Qt, GlViewport_Qt, and the
// headless --*-test harness dispatch.
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
#include "auth/gfcTokenStore.h"
#include "auth/gfcAuthSession.h"
#include "gfcTestCoordinator.h"
#include "gfcSignaling.h"
#include "gfcStructures.h"
#include "gfcSha1.h"
#include "gfcTarArchive.h"
#include "gfcMediaFingerprint.h"
#include "gfcSessionPaths.h"
#include "gfcnote.h"
#include "gfcNoteStore.h"
#include "gfcNoteMerge.h"
#include "gfcNoteOverlay.h"
#include "gfcNoteStamp.h"
#include "gfcReviewSummary.h"
#include "gfcWireTest.h"
#include "qt/iapplication_qt.h"
#include "qt/ieventsystem_qt.h"
#include "qt/MainWindow_qt.h"
#include "qt/SequenceLoadBridge_qt.h"
#include "qt/ReviewSummaryPdf_qt.h"
#include "qt/ReviewPackage_qt.h"

extern gfcSettings sett;

static void applyDarkTheme(QApplication& qapp) {
    // Look for the theme next to the binary (`./theme/jefecheck_dark.qss`), falling back to
    // the source tree; silent miss if neither exists.
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

// Resolve --config-dir <path> from argv (fallback: JEFECHECK_CONFIG_DIR). UI tests use it to
// keep QSettings, session XML and LUT autoload inside a temp dir.
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

// Resolve all --open-file <path> occurrences from argv; each loads into the next plate
// (0..3). Used by UI tests to seed the viewport before screenshot diffs.
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

// Resolve --render-test <dir>. Headless smoke test: render one frame of plate 0 into <dir> in
// every output format (0 = written, 2 = nothing). Use a multi-frame sequence, not a single still.
static QString resolveRenderTestDir(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--render-test") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return QString();
}

// Resolve --video-test <dir>. Headless video-export test: render the in/out range to a temp
// PNG sequence and encode an H.264 mp4 into <dir> (0 = ok, 2 = failed).
static QString resolveVideoTestDir(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--video-test") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return QString();
}

// Resolve --playlist-test <image>. Headless .jpl round-trip: add <image>, save to a temp
// .jpl, clear, reload, and verify the entry count survives (no GL).
static QString resolvePlaylistTestFile(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--playlist-test") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return QString();
}

// Resolve --fx-test <image>. Headless FX-stack proof: render a baseline PNG, add a shader FX
// to the active plate via the UI's bridge call, re-render; exits 0 if the pixels differ.
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

// Resolve --fx-multitest <image>. Headless multiplate FX state-leak probe: load the image
// into plates 0 and 1, add an FX to plate 0 only, and compare left/right brightness.
static QString resolveFXMultiTestFile(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--fx-multitest") == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return QString();
}

// --aspect-test : headless regression: asserts every aspect preset string maps to the
// renderer's height/width convention (no GL). Takes no argument.
static bool hasAspectTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--aspect-test") == 0) return true;
    return false;
}

// --notes-test : headless note model, sidecar store, overlay geometry and EXR stamp
// self-tests (no GL or window). Runs all even if one fails. Takes no argument.
static bool hasNotesTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--notes-test") == 0) return true;
    return false;
}

// --wire-test : headless jefe::wire round-trip/bounds-safety self-test. Pure data; dispatched
// at the top of main(), before QApplication is constructed. Takes no argument.
static bool hasWireTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--wire-test") == 0) return true;
    return false;
}

// --signal-test : headless WebSocket signaling-stub loopback self-test; runs and exits
// before QApplication is constructed.
static bool hasSignalTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--signal-test") == 0) return true;
    return false;
}

// --coord-signal-test : headless cloud-coordinator codec + loopback self-test; runs before
// QApplication.
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

// --remote-test-webrtc : orchestrator/server role. Like --remote-test but forces
// JEFECHECK_TRANSPORT=webrtc so host and peer use a real libdatachannel data channel.
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

// --stats-test : orchestrator role. Same two-process WebRTC harness as --remote-test-webrtc,
// then asserts the per-peer stats getter returns REAL WebRTC stats.
static bool hasStatsTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--stats-test") == 0) return true;
    return false;
}

// --asset-test : orchestrator role. Loads a fixture LUT (and an FX when a GL context exists)
// into the host and asserts a spawned peer hot-loads it via the late-join sync (over RakNet).
static bool hasAssetTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--asset-test") == 0) return true;
    return false;
}
// --asset-test-peer <ip> <port> <lutHash> <fxHash> : child/joiner role. Joins, lets the
// sync run, and asserts the fixture hashes now live in its managers ("-" = no usable FX).
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

// --asset-test-webrtc : orchestrator role. Drives a LARGE LUT over the WebRTC assets channel
// (chunking/backpressure) and asserts the joiner's LUT hash matches the host's.
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
// Generate a multi-MB Truelight-Cube v2.0 .cube fixture at `path` (`cubeSize`^3 identity ramp;
// 45 -> ~2.4 MB, past the chunk size and backpressure mark). Returns the size in bytes, or 0.
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

// --coord-test : orchestrator role, WebRTC through a test-double coordinator. Spawns a peer
// that joins by code and asserts the P2P session mirrors play (JEFECHECK_TRANSPORT=webrtc re-exec).
static bool hasCoordTest(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--coord-test") == 0) return true;
    return false;
}
// --coord-test-server : runs ONLY the test-double coordinator on an ephemeral port, printing
// "COORD-URL=ws://127.0.0.1:<port>/". It MUST be its own process (libdatachannel misroutes otherwise).
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

// --coord-live-test <coordUrl> [authToken] : bring a cloud host up against a REAL coordinator
// and assert its loopback client reaches the participant list (sees host POLICY, unlike the test double).
static bool resolveCoordLiveTest(int argc, char* argv[], std::string& url,
                                 std::string& token) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--coord-live-test") == 0) {
            url = argv[i + 1];
            token = (i + 2 < argc && argv[i + 2][0] != '-') ? argv[i + 2] : "";
            return true;
        }
    }
    return false;
}

int main(int argc, char* argv[]) {
    // WebRTC-harness re-exec: `networkManager` builds its transports during STATIC init, before
    // main(), so qputenv() is too late; set JEFECHECK_TRANSPORT=webrtc and re-exec (once per process).
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
                // Live-coordinator harness: coordinator mode is WebRTC, so it needs the same
                // re-exec (transports are built during static init).
                std::strcmp(argv[i], "--coord-live-test") == 0 ||
                std::strcmp(argv[i], "--coord-live-peer") == 0 ||
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

    // Headless jefe::wire self-test (--wire-test): pure data, runs and exits before
    // QApplication is even constructed.
    if (hasWireTest(argc, argv)) {
        return jefe::wire::selfTest();
    }

    // Headless PKCE codec self-test (--pkce-test): pure string/crypto work; runs and exits
    // before QApplication exists.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--pkce-test") == 0) {
            return jefe::auth::pkceSelfTest();
        }
        // --loopback-test: binds a real socket, so it needs an event loop; it constructs its
        // own QCoreApplication and still runs before QApplication/GL exist.
        if (std::strcmp(argv[i], "--loopback-test") == 0) {
            return jefe::auth::loopbackSelfTest(argc, argv);
        }
        // --tokenstore-test: exercises the FILE backend (compiled everywhere); the native
        // Keychain/Credential Manager backends are verified by hand.
        if (std::strcmp(argv[i], "--auth-test") == 0) {
            return jefe::auth::authSelfTest(argc, argv);
        }
        if (std::strcmp(argv[i], "--tokenstore-test") == 0) {
            return jefe::auth::tokenStoreSelfTest();
        }
    }

    // Headless signaling-stub self-test (--signal-test): round-trips hello/offer JSON through
    // a local WebSocket SignalingServer + Client. Runs before QApplication.
    if (hasSignalTest(argc, argv)) {
        return jefe::net::signalingSelfTest();
    }

    // Headless cloud-coordinator self-test (--coord-signal-test): codec round-trip plus a
    // bounded loopback against a scripted rtc::WebSocketServer. Runs before QApplication.
    if (hasCoordSignalTest(argc, argv)) {
        return jefe::net::coordinatorSignalingSelfTest();
    }

    // Test-double coordinator process (--coord-test-server): runs ONLY the
    // rtc::WebSocketServer on an ephemeral port, in its own process.
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

    // Make Qt's accessibility bridge live before QApplication touches anything (on macOS
    // it routes QAccessible to NSAccessibility, which Appium's mac2 driver introspects).
    qputenv("QT_ACCESSIBILITY", "1");

    // Sets gMacExecutablePath, which getApplicationDataPath uses on macOS to find the
    // bundled Resources directory (LUT/FX autoload).
    if (argc > 0 && argv[0]) {
        setMacExecutablePath(argv[0]);
    }

    // Assign a writable per-user directory (getApplicationDataPath()/received/) for P2P-received
    // assets; otherwise received LUT/FX files land in the CWD. Runs after setMacExecutablePath.
    {
        std::string received = getApplicationDataPath() + "received/";
        std::error_code ec;
        std::filesystem::create_directories(received, ec);
        sett.receivedPath = received;   // gfcSettings global (extern above)
    }
    // gfcPlate needs the fixed-function pipeline, which macOS exposes only as legacy GL 2.1;
    // Qt's default Core profile removes it (untextured white quad). Use NoProfile + 2.1 there.
    QSurfaceFormat fmt;
    fmt.setProfile(QSurfaceFormat::NoProfile);
    fmt.setVersion(2, 1);
    fmt.setDepthBufferSize(24);
    // Explicit vsync (1): Qt's default on macOS is platform-dependent, and drag-pan feel was
    // inconsistent without it.
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication qapp(argc, argv);
    qapp.setApplicationName("JefeCheck");
    qapp.setOrganizationName("JefeCheck");

    // Test-mode isolation: redirect QSettings + LUT autoload path into a caller-supplied
    // directory. Must run BEFORE any QSettings is constructed.
    const QString configDir = resolveConfigDir(argc, argv);
    if (!configDir.isEmpty()) {
        QDir().mkpath(configDir);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           configDir);
        // sett.lutPath drives the install-LUT autoload in SequenceLoadBridge_qt; point it at
        // the test fixture dir if seeded.
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
        // GUI stubs must exist before addCurrentAsPlaylistItem() calls getLoadParamsFromGUI()
        // / getPlateStateInfo(); MainWindow_Qt isn't constructed yet.
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

    // Headless aspect-conversion regression (--aspect-test): pure string->float check.
    if (hasAspectTest(argc, argv)) {
        const bool ok = jefe::qt::runAspectSelfTest();
        std::_Exit(ok ? 0 : 2);
    }

    // Headless note self-tests (--notes-test): pure data/arithmetic. Runs all three
    // regardless of earlier failures and exits non-zero if any failed.
    if (hasNotesTest(argc, argv)) {
        const int modelFail   = noteModelSelfTest();
        const int storeFail   = noteStoreSelfTest();
        const int overlayFail = noteOverlaySelfTest();
        const int stampFail   = noteStampSelfTest();
        const int summaryFail = reviewSummarySelfTest();
        const int pdfFail     = jefe::qt::reviewSummaryPdfSelfTest();
        const int sha1Fail    = sha1SelfTest();
        const int tarFail     = tarSelfTest();
        const int fingerprintFail = mediaFingerprintSelfTest();
        const int sessionPathsFail = sessionPathsSelfTest();
        const int mergeFail = noteMergeSelfTest();
        const int packageFail = jefe::qt::package::packageSelfTest();
        const int packageOpenFail = jefe::qt::package::packageOpenSelfTest();
        // The self-tests print via std::printf without flushing, and std::_Exit skips the
        // stdio flush, so an unflushed buffer would drop all three lines.
        std::fflush(stdout);
        std::_Exit((modelFail == 0 && storeFail == 0 && overlayFail == 0 &&
                    stampFail == 0 && summaryFail == 0 && pdfFail == 0 && sha1Fail == 0 &&
                    tarFail == 0 && fingerprintFail == 0 && sessionPathsFail == 0 &&
                    mergeFail == 0 && packageFail == 0 && packageOpenFail == 0) ? 0 : 2);
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
        // Forward the child's output into this process's, so a failing two-process test is diagnosable.
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

    // --asset-test-peer <ip> <port> <lutHash> <fxHash>: joiner child role. Asserts the fixture
    // hashes live in this process's managers; prints "ASSET-PEER: lut=.. fx=.." and exits 0 iff the LUT transferred.
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
            // Connect + hold + pump; the sync fires automatically on join (4 s is ample).
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
        // loadLUT/loadFX create GL objects, so bring up an offscreen GL context first or the
        // fixture loads call null GLAD pointers.
        if (!jefe::qt::setupOffscreenTestGL()) {
            printf("ASSET-TEST: gl setup failed\n");
            fflush(stdout);
            std::_Exit(3);
        }
        const std::string fxDir = getApplicationDataPath() + "FX/";
        const std::string lutFixture = fxDir + "invert.lut";
        const std::string fxFixture  = fxDir + "ADD.jfx";
        // Load the fixtures into the HOST before the peer joins; if the offscreen context can't
        // compile the shader the FX hash is empty and the harness reports fx=na (LUT mandatory).
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
        // CAPTURE the child's stdout (SeparateChannels) to read its ASSET-PEER marker;
        // ForwardedChannels would leave readAllStandardOutput empty. Forward stderr.
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

        // Parse the peer's ASSET-PEER marker from its stdout and echo it.
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
        // FX is N/A when the host had no GL context to compile it (the common headless
        // case); report a verdict only when the host had a usable FX to push.
        if (fxApplicable)
            printf("ASSET-TEST: lut=%d fx=%d\n", lutOk ? 1 : 0, peerFx);
        else
            printf("ASSET-TEST: lut=%d fx=na\n", lutOk ? 1 : 0);
        fflush(stdout);
        std::_Exit(lutOk && (!fxApplicable || peerFx == 1) ? 0 : 2);
    }

    // --remote-test-webrtc-peer <ip> <port>: WebRTC child client role (transport forced by the
    // re-exec). holdMs/connectTimeoutMs are large: a send before the channel opens is dropped.
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

        // Bring the host's own loopback client fully up BEFORE the peer exists: the peer's play
        // is one-shot, and a WebRTC loopback can open later, losing the forward.
        if (!jefe::qt::remoteTestServerStart(port, /*loopbackTimeoutMs=*/10000)) {
            printf("REMOTE-TEST-WEBRTC: loopback client failed to come up\n");
            fflush(stdout);
            std::_Exit(3);
        }

        // Now spawn the peer; its single play toggle is guaranteed a live loopback to mirror to.
        QProcess peer;
        peer.setProgram(QCoreApplication::applicationFilePath());
        peer.setArguments({"--remote-test-webrtc-peer", "127.0.0.1", QString::number(port)});
        // Spell the env var out so the child's transport never depends on inheritance.
        QProcessEnvironment childEnv = QProcessEnvironment::systemEnvironment();
        childEnv.insert("JEFECHECK_TRANSPORT", "webrtc");
        peer.setProcessEnvironment(childEnv);
        if (qEnvironmentVariableIsSet("JEFECHECK_REMOTE_TEST_DEBUG"))
            peer.setProcessChannelMode(QProcess::ForwardedChannels);
        peer.start();
        if (!peer.waitForStarted(2000)) { printf("REMOTE-TEST-WEBRTC: child failed to start: %s\n", peer.errorString().toUtf8().constData()); fflush(stdout); std::_Exit(3); }
        // WebRTC establishment (signaling, ICE, DTLS, SCTP) is far slower than RakNet, so the
        // settle budget is generous (10 s).
        const bool sawPlay = jefe::qt::remoteTestServerSettleForPlay(/*settleMs=*/10000);
        const int  peak    = (int)jefe::qt::remoteParticipants().size();
        peer.waitForFinished(6000);
        if (peer.state() != QProcess::NotRunning) peer.kill();
        printf("REMOTE-TEST-WEBRTC: participants=%d mirrored_play=%d\n", peak, sawPlay ? 1 : 0);
        fflush(stdout);
        std::_Exit((peak >= 1 && sawPlay) ? 0 : 2);
    }

    // --stats-test: reuse the --remote-test-webrtc harness, then assert the per-peer stats
    // getter returns REAL stats (>=1 connected peer, bytes>0, resolved path; rtt not required).
    if (hasStatsTest(argc, argv)) {
        jefe::qt::initializeRenderingChain();
        const int port = 60127;   // distinct from the other harness ports

        // Bring the host + its loopback client fully up (handshake/sync traffic is enough for stats).
        if (!jefe::qt::remoteTestServerStart(port, /*loopbackTimeoutMs=*/10000)) {
            printf("STATS-TEST: loopback client failed to come up\n");
            fflush(stdout);
            std::_Exit(3);
        }

        // Spawn a second WebRTC peer (reuses the remote-test child role) for cross-process traffic.
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

        // Poll the stats getter with a bounded retry until the bytes counters and selected
        // candidate pair have settled; accept the first peer with real stats.
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

    // --asset-test-webrtc-peer <ip> <port> <lutHash>: WebRTC joiner child role. Joins the
    // host, holds for the chunked LUT transfer, then asserts the LUT hash is in lutManager.
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
        // Bring the host + its loopback client fully up over WebRTC (the loopback shares this
        // process's managers, so it already has the LUT and is never pushed to).
        if (!jefe::qt::remoteTestServerStart(port, /*loopbackTimeoutMs=*/12000)) {
            printf("ASSET-TEST-WEBRTC: loopback client failed to come up\n");
            fflush(stdout);
            std::_Exit(3);
        }

        // Spawn the joiner; it establishes its own WebRTC session and receives the large LUT.
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
        // Pump the host while the peer connects and the chunked transfer runs (bounded at 20 s).
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

    // --coord-test-peer <coordUrl> <code>: WebRTC joiner child role for the coordinator
    // harness. holdMs/connectTimeoutMs mirror the LAN WebRTC peer.
    {
        std::string coordUrl, code;
        if (resolveCoordTestPeer(argc, argv, coordUrl, code)) {
            jefe::qt::initializeRenderingChain();
            jefe::qt::coordTestPeerJoin(coordUrl, code, /*holdMs=*/6000,
                                        /*play=*/true, /*connectTimeoutMs=*/12000);
            std::_Exit(0);
        }
    }
    {
        // --coord-live-peer <coordUrl> <code>: the joiner half.
        std::string url, code;
        for (int i = 1; i + 2 < argc; ++i) {
            if (std::strcmp(argv[i], "--coord-live-peer") == 0) {
                url = argv[i + 1]; code = argv[i + 2];
                break;
            }
        }
        if (!url.empty()) {
            jefe::qt::initializeRenderingChain();
            jefe::qt::coordLivePeerJoin(url, code, /*timeoutMs=*/25000);
            std::_Exit(0);
        }
    }
    {
        std::string liveUrl, liveToken;
        if (resolveCoordLiveTest(argc, argv, liveUrl, liveToken)) {
            jefe::qt::initializeRenderingChain();
            const bool up = jefe::qt::coordLiveHostStart(liveUrl, liveToken,
                                                         /*loopbackTimeoutMs=*/15000);
            printf("COORD-LIVE-TEST: code=%s loopback=%d\n",
                   jefe::qt::coordTestGetCode().c_str(), up ? 1 : 0);
            fflush(stdout);
            if (!up) {
                // Print WHY: a refusal (e.g. auth-required for anonymous hosting) is a
                // legitimate outcome, and the reason code says whether the coordinator behaved.
                printf("COORD-LIVE-TEST: refused code=[%s] message=[%s]\n",
                       jefe::qt::remoteCoordinatorErrorCode().c_str(),
                       jefe::qt::remoteCoordinatorErrorMessage().c_str());
                fflush(stdout);
                std::_Exit(2);
            }
            // Hold open so a joiner can knock, then admit it. Prints "decided", not "admitted":
            // with JEFE_LIVE_DENY=1 the same count is refusals.
            const int decided = jefe::qt::coordLiveAwaitAndAdmit(15000);
            printf("COORD-LIVE-TEST: decided=%d participants=%d\n", decided,
                   (int)jefe::qt::remoteParticipants().size());
            fflush(stdout);
            std::_Exit(0);
        }
    }
    if (hasCoordTest(argc, argv)) {
        // WebRTC is already forced by the re-exec at the top of main().
        jefe::qt::initializeRenderingChain();

        // 1. Spawn the test-double coordinator as its OWN process (see --coord-test-server):
        //    a co-located WebSocketServer trips a libdatachannel message-routing bug.
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

        // 2. Bring the host up in coordinator mode (create-session) and wait for its loopback
        //    client to fully register BEFORE the peer exists (one-shot-play race).
        if (!jefe::qt::coordTestHostStart(coordUrl, /*loopbackTimeoutMs=*/12000)) {
            printf("COORD-TEST: host/loopback failed to come up (code=%s)\n",
                   jefe::qt::coordTestGetCode().c_str());
            fflush(stdout);
            std::_Exit(3);
        }
        const std::string code = jefe::qt::coordTestGetCode();

        // 3. Spawn the peer child: it joins the same coordinator by the assigned code and
        //    toggles play, mirrored over the P2P channel to the loopback (isPlaying()).
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

    // Also keep review-package extraction inside the caller-supplied config dir
    // (--config-dir only redirects QSettings on its own).
    if (!configDir.isEmpty()) {
        window.setPackageCacheRoot(configDir + "/packages");
    }

    // --ui-preview: open the Remote dock with the Cloud and admission layouts filled with
    // sample data. Deferred so docks are laid out first.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ui-preview") == 0) {
            // Optional "knock" selects the joiner's waiting screen instead of the host's lobby.
            const bool knocking =
                i + 1 < argc && std::strcmp(argv[i + 1], "knock") == 0;
            QTimer::singleShot(0, &window, [&window, knocking]() {
                window.showRemoteUiPreview(knocking);
            });
            break;
        }
    }

    // --auto-cloud-host / --auto-cloud-join <code>: drive the Remote panel from the command
    // line; the host prints "CLOUD-CODE=<code>". Delayed 1200ms so docks have laid out.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--auto-cloud-host") == 0) {
            QTimer::singleShot(1200, &window, [&window]() {
                window.autoCloudHost();
                // Poll for the assigned code: hosting round-trips through the
                // coordinator on a worker thread, so it is not ready on return.
                auto* t = new QTimer(&window);
                t->setInterval(200);
                QObject::connect(t, &QTimer::timeout, &window, [&window, t]() {
                    const QString code = window.cloudSessionCode();
                    if (code.isEmpty()) return;
                    printf("CLOUD-CODE=%s\n", code.toUtf8().constData());
                    fflush(stdout);
                    t->stop();
                });
                t->start();
            });
            break;
        }
        if (std::strcmp(argv[i], "--auto-cloud-join") == 0 && i + 1 < argc) {
            const QString code = QString::fromUtf8(argv[i + 1]);
            QTimer::singleShot(1200, &window,
                               [&window, code]() { window.autoCloudJoin(code); });
            break;
        }
    }

    // --auto-admit <delayMs>: press Admit on everyone waiting, once.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--auto-admit") == 0) {
            const int delayMs = std::atoi(argv[i + 1]);
            QTimer::singleShot(delayMs, &window, [&window]() {
                printf("AUTO-ADMIT=%d\n", window.autoAdmitPending());
                fflush(stdout);
            });
            break;
        }
    }

    // --screenshot <path> [delayMs]: the app saves a PNG of its own main window and keeps
    // running, so the result is the window alone.
    for (int i = 1; i + 1 < argc; ++i) {
        const bool whole  = std::strcmp(argv[i], "--screenshot") == 0;
        const bool remote = std::strcmp(argv[i], "--screenshot-remote") == 0;
        if (!whole && !remote) continue;
        const QString path = QString::fromUtf8(argv[i + 1]);
        int delayMs = 4000;
        if (i + 2 < argc && argv[i + 2][0] != '-') delayMs = std::atoi(argv[i + 2]);
        QTimer::singleShot(delayMs, &window, [&window, path, remote]() {
            QWidget* target = remote ? window.remotePanelWidget() : &window;
            if (target == nullptr) target = &window;
            const QPixmap shot = target->grab();
            printf("SCREENSHOT=%s ok=%d\n", path.toUtf8().constData(),
                   shot.save(path) ? 1 : 0);
            fflush(stdout);
        });
        break;
    }

    // Load each --open-file into the matching plate once the event loop has spun up the
    // GL context (deferred so paintGL has initialized GLAD before the texture upload).
    const QStringList openFiles = resolveOpenFiles(argc, argv);
    for (int i = 0; i < openFiles.size() && i < 4; ++i) {
        const int plateIdx = i;
        const QString path = openFiles.at(i);
        QTimer::singleShot(0, &window, [&window, plateIdx, path]() {
            window.loadFileIntoPlate(plateIdx, path);
        });
    }

    // --notes-demo: put one of each note type on plate 0 at fixed coordinates, so the
    // overlay can be screenshotted and compared run to run.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--notes-demo") == 0) {
            QTimer::singleShot(2500, &window, [&window]() {
                jefe::qt::addDemoNotes(0);
                // The demo bypasses the mouse, so it must refresh the dock itself (real
                // drawing does so via plateStateChanged on mouse-up).
                window.refreshNotesForLoadedMedia();
            });
            break;
        }
    }

    // --stamp-notes <out.exr>: stamp the active plate's current frame into <out.exr> and
    // report; needs the real GL context only a live window provides.
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

    // --summary-test <image>: end-to-end proof of File -> Export Review Summary.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--summary-test") != 0) continue;
        const QString image = QString::fromUtf8(argv[i + 1]);
        QTimer::singleShot(5000, &window, [&window, image]() {
            const int code = window.runHeadlessSummaryTest(image);
            fflush(stdout);
            std::_Exit(code);
        });
        break;
    }

    // --export-summary <out>: write the review summary of what is loaded, then quit.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--export-summary") != 0) continue;
        const QString out = QString::fromUtf8(argv[i + 1]);
        QTimer::singleShot(6000, &window, [&window, out]() {
            MainWindow_Qt::ReviewSummaryStats s;
            QString msg;
            const bool ok = window.exportReviewSummary(out, &s, &msg);
            if (ok) {
                printf("SUMMARY: wrote=%s media=%d rounds=%d notes=%d thumbs=%d thumbfail=%d\n",
                       qPrintable(out), s.media, s.rounds, s.notes, s.thumbs, s.thumbFail);
            } else {
                printf("SUMMARY: FAIL %s\n", qPrintable(msg));
            }
            fflush(stdout);
            std::_Exit(ok ? 0 : 2);
        });
        break;
    }

    // --package-test <image>: end-to-end proof of the review package.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--package-test") != 0) continue;
        const QString image = QString::fromUtf8(argv[i + 1]);
        QTimer::singleShot(5000, &window, [&window, image]() {
            const int code = window.runHeadlessPackageTest(image);
            fflush(stdout);
            std::_Exit(code);
        });
        break;
    }

    // --export-package <out> [--no-media]: package what is loaded, then quit.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--export-package") != 0) continue;
        const QString out = QString::fromUtf8(argv[i + 1]);
        bool includeMedia = true;
        for (int k = 1; k < argc; ++k) {
            if (std::strcmp(argv[k], "--no-media") == 0) includeMedia = false;
        }
        QTimer::singleShot(6000, &window, [&window, out, includeMedia]() {
            MainWindow_Qt::PackageStats s;
            QString msg;
            const bool ok = window.exportReviewPackage(out, includeMedia, &s, &msg);
            if (ok) {
                printf("PACKAGE: wrote=%s media=%d included=%d bytes=%lld\n", qPrintable(out), s.media,
                       s.mediaIncluded ? 1 : 0, static_cast<long long>(s.bytes));
            } else {
                printf("PACKAGE: FAIL %s\n", qPrintable(msg));
            }
            fflush(stdout);
            std::_Exit(ok ? 0 : 2);
        });
        break;
    }

    // --relink-test <image>: a lean package finds moved media by fingerprint.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--relink-test") != 0) continue;
        const QString image = QString::fromUtf8(argv[i + 1]);
        QTimer::singleShot(5000, &window, [&window, image]() {
            const int code = window.runHeadlessRelinkTest(image);
            fflush(stdout);
            std::_Exit(code);
        });
        break;
    }

    // --open-package <file>: open a review package and keep running.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--open-package") != 0) continue;
        const QString file = QString::fromUtf8(argv[i + 1]);
        QTimer::singleShot(3000, &window, [&window, file]() {
            MainWindow_Qt::PackageStats s;
            QString msg;
            if (window.openReviewPackage(file, false, &s, &msg)) {
                printf("PACKAGE: opened=%s media=%d resolved=%d missing=%d\n", qPrintable(file), s.media,
                       s.resolved, s.missing);
            } else {
                printf("PACKAGE: FAIL %s\n", qPrintable(msg));
            }
            fflush(stdout);
        });
        break;
    }

    // --package-dialog-test <image>: the export dialog writes, cancels and refuses.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--package-dialog-test") != 0) continue;
        const QString image = QString::fromUtf8(argv[i + 1]);
        QTimer::singleShot(5000, &window, [&window, image]() {
            const int code = window.runHeadlessPackageDialogTest(image);
            fflush(stdout);
            std::_Exit(code);
        });
        break;
    }

    // --ui-density-test [outDir]: every dock and dialog against the shared density rules.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ui-density-test") != 0) continue;
        QString outDir;
        if (i + 1 < argc && std::strncmp(argv[i + 1], "--", 2) != 0)
            outDir = QString::fromUtf8(argv[i + 1]);
        QTimer::singleShot(3000, &window, [&window, outDir]() {
            const int code = window.runHeadlessUiDensityTest(outDir);
            fflush(stdout);
            std::_Exit(code);
        });
        break;
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

    // --load-all: what the Load Sequence Manager's Load All does (leave preview mode, load
    // every track), so a plate filled by --open-file shows its decoded frame.
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

    // --demo-host <port> / --demo-join <ip> <port>: start a LAN review session without the
    // Remote dialog. Nicknames differ on purpose (the server refuses a duplicate).
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

    // --grab-frames <dir> <caption> <startMs> <endMs>: save the window as a captioned JPEG
    // every 66 ms, named by wall-clock bucket so two instances can be stitched side by side.
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

    // Headless render smoke test (--render-test <dir>): after the footage has decoded,
    // render one frame of plate 0 into <dir> and quit.
    const QString renderTestDir = resolveRenderTestDir(argc, argv);
    if (!renderTestDir.isEmpty()) {
        // The destination must be a usable DIRECTORY before anything renders (an image path
        // passed by mistake would otherwise fail every write yet report success).
        if (!QDir().mkpath(renderTestDir)) {
            printf("RENDER-TEST FAIL: cannot use %s as an output directory\n",
                   renderTestDir.toLocal8Bit().constData());
            fflush(stdout);
            return 2;
        }
        // Snapshot first, so pre-existing files in the directory can't be
        // counted as this run's output.
        const QStringList before =
            QDir(renderTestDir).entryList(QDir::Files | QDir::NoDotAndDotDot);
        QTimer::singleShot(5000, &window, [&window, renderTestDir, before]() {
            // The render (GL readback + OIIO save) lives in MainWindow's TU, which can
            // touch the viewport's GL context without pulling glad into this TU.
            const int claimed = window.runHeadlessRenderTest(renderTestDir);

            // Count what actually landed on disk, not the render path's own frame count:
            // a run where every OIIO write failed must not pass.
            const QStringList after =
                QDir(renderTestDir).entryList(QDir::Files | QDir::NoDotAndDotDot);
            int landed = 0;
            for (const QString& f : after)
                if (!before.contains(f)) ++landed;

            printf("RENDER-TEST: %d frame(s) claimed, %d file(s) on disk in %s\n",
                   claimed, landed, renderTestDir.toLocal8Bit().constData());
            if (landed == 0)
                printf("RENDER-TEST FAIL: nothing was written\n");
            else if (claimed > 0 && landed < claimed)
                printf("RENDER-TEST FAIL: %d frame(s) claimed but only %d written\n",
                       claimed, landed);
            else
                printf("RENDER-TEST PASS\n");
            fflush(stdout);
            // Skip Qt's global teardown (trace trap in gfcPlaybackGUI's destructor on
            // macOS) so the harness gets a deterministic exit code.
            std::_Exit((landed > 0 && landed >= claimed) ? 0 : 2);
        });
    }

    // Headless FX-stack proof (--fx-test <image>): load, render a baseline, add a shader
    // FX, re-render, and report whether the output changed. Self-loads the image.
    const QString fxTestFile = resolveFXTestFile(argc, argv);
    if (!fxTestFile.isEmpty()) {
        QTimer::singleShot(5000, &window, [&window, fxTestFile]() {
            const int code = window.runHeadlessFXTest(fxTestFile);
            fflush(stdout);
            // Skip Qt's global teardown (trace trap in a destructor on macOS) for a
            // deterministic exit code, as in --render-test.
            std::_Exit(code);
        });
    }

    // Headless colour-correction render proof (--cc-test <image>): render a baseline,
    // apply exposure+gamma, re-render, and report whether the output changed.
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
