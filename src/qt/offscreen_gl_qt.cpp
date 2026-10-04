// Offscreen OpenGL context bring-up for the headless --asset-test harness
// (JEF-28 Task 2). Loading a LUT or FX creates GL objects (LUT: glTexImage3D;
// FX: ARB shader objects), which need a *current* GL context with GLAD
// resolved. The --fx-test/--cc-test harnesses get one by showing MainWindow +
// its QOpenGLWidget; --asset-test is a two-process network harness where a full
// window is unnecessary, so it stands up a headless QOffscreenSurface instead.
//
// This lives in its own TU because it includes Qt's OpenGL headers
// (QOpenGLContext), which CANNOT share a translation unit with glad.h — glad's
// header hard-errors if any real GL header was already included (see
// developer_notes §1, and gfcStructures.h pulls in glad). main_qt.cpp includes
// glad-tainted headers, so the Qt-OpenGL bring-up is confined here and exposed
// as a plain jefe::qt:: function. jefecheck_loadGladGL() (glad_loader.cpp) is
// referenced by name only — no glad header needed here.

#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QSurfaceFormat>

// Defined in glad_loader.cpp (the sole glad TU). Resolves GLAD's function
// pointers against the current context.
extern bool jefecheck_loadGladGL();

namespace jefe::qt {

// Bring up an offscreen GL context matching the viewport's format (macOS 2.1
// NoProfile compatibility, set as the default QSurfaceFormat in main()), make
// it current, and resolve GLAD. Returns false if any step fails. The context +
// surface are intentionally leaked: the callers are short-lived test processes
// that exit via std::_Exit, and the context must stay current for the whole
// run.
bool setupOffscreenTestGL() {
    auto* ctx = new QOpenGLContext();
    ctx->setFormat(QSurfaceFormat::defaultFormat());
    if (!ctx->create()) return false;

    auto* surf = new QOffscreenSurface();
    surf->setFormat(ctx->format());
    surf->create();
    if (!surf->isValid()) return false;

    if (!ctx->makeCurrent(surf)) return false;
    return jefecheck_loadGladGL();
}

}  // namespace jefe::qt
