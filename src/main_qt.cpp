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

#include "gfcStructures.h"
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
        jefe::qt::clearPlaylist();
        jefe::qt::addPlaylistFile(playlistTestFile.toStdString());
        const int added = int(jefe::qt::getPlaylistItemNames().size());
        const std::string jpl =
            (QDir::tempPath() + "/jefecheck_playlist_test.jpl").toStdString();
        jefe::qt::savePlaylistFile(jpl);
        jefe::qt::clearPlaylist();
        const int afterClear = int(jefe::qt::getPlaylistItemNames().size());
        jefe::qt::loadPlaylistFile(jpl);
        const int afterLoad = int(jefe::qt::getPlaylistItemNames().size());
        printf("PLAYLIST-TEST: added=%d afterClear=%d afterLoad=%d file=%s\n",
               added, afterClear, afterLoad, jpl.c_str());
        fflush(stdout);
        const bool ok = (added == 1 && afterClear == 0 && afterLoad == 1);
        std::_Exit(ok ? 0 : 2);
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

    return application.run();
}
