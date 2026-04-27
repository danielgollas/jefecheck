// Minimal Qt entry point for the USE_QT build. This is intentionally tiny —
// just enough to prove the toolchain end-to-end (QApplication + a main window
// + GlViewport_Qt + IApplication_Qt). The full feature port lives in later
// phases (2E onward).
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QSurfaceFormat>

#include "qt/iapplication_qt.h"
#include "qt/ieventsystem_qt.h"
#include "qt/MainWindow_qt.h"

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

int main(int argc, char* argv[]) {
    // gfcPlate's renderer relies on the fixed-function GL pipeline:
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
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication qapp(argc, argv);
    qapp.setApplicationName("JefeCheck");
    qapp.setOrganizationName("JefeCheck");

    static IApplication_Qt application(&qapp);
    static IEventSystem_Qt events;
    jefe::ui::IApplication::setInstance(&application);
    jefe::ui::IEventSystem::setInstance(&events);

    applyDarkTheme(qapp);

    MainWindow_Qt window;
    window.show();

    return application.run();
}
